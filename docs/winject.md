# Radio architecture

How `winject-radio-realtek` turns an RTL8812AU USB dongle on a Linux host into a raw 802.11 radio for **winject-manager**: the process and its threads, bring-up, the inject and receive paths, radio configuration, and the counters that account for every frame.

The m-plane command protocol is in [mplane.md](./mplane.md); this document covers what sits behind it. The design history and bench measurements are in [implementation.md](./implementation.md).

## Overview

The radio is a bridge between three UDP sockets and one monitor-mode WiFi interface driven by `rtl88xxau_wfb` (svpcom's 8812AU driver used by wfb-ng). To the manager it looks like the ESP32 radio: the same m-plane commands and the same d-plane datagram formats. It does not associate, route, or read the payload of the frames it carries; the manager builds every MPDU and parses everything forwarded to it.

```
                      control thread (bfc epoll reactor)
 manager ──UDP console──▶ MplaneServer ─▶ mplane_commands ─▶ RealtekDeviceBackend ─▶ Settings (state.dir)
                                                         └─▶ RealtekRadioBackend ─▶ RealtekRadio ─▶ Nl80211
                                                                                         │
                                          publishes TxProfile, Addr3 filter (atomics) ───┤
                                                                                         ▼
                      data thread (own epoll)
 manager ──UDP 9000──▶ Injector ─▶ TX ring ─▶ PacketSocket sendmsg(radiotap ‖ MPDU) ─▶ wlx… ─▶ air
 manager ◀─UDP 9210── Forwarder ◀─ radiotap parse, Addr3 filter, FCS ◀─ PacketSocket recvmmsg ◀─ wlx… ◀─ air
```

| Plane | Transport | Purpose |
|-------|-----------|---------|
| m-plane | UDP `net.console_port` (2201) | Text commands: radio settings, Addr3 filter, counters, slots, reset ([mplane.md](./mplane.md)) |
| d-plane inject | UDP `net.inject_port` (9000) | One raw MPDU per datagram, host → air |
| d-plane forward | UDP `net.forward_port` (9210) | Peer registration; one received MPDU + 4-byte FCS per datagram, air → host |

One process drives one dongle. Run one copy per dongle with its own config file, on the manager's host (`net.bind = 127.0.0.1`) or on a separate board.

```bash
sudo ./build/src/radio/winject-radio-realtek --config configuration/radio-a.cfg
```

It needs `CAP_NET_RAW` (packet socket) and `CAP_NET_ADMIN` (nl80211, link up/down).

## Threads

| Thread | Event loop | Owns | Never does |
|--------|------------|------|------------|
| Control (main) | bfc `epoll_reactor` (`IOReactor.h`) | m-plane socket, `Nl80211`, `RealtekRadio`, `Settings`, shutdown eventfd, 50 ms reset poll timer | Touch the packet socket's data path |
| Data | Plain `epoll`, 500 ms timeout (`DataPlane.cpp`) | Inject and forward-registration UDP sockets, forward send socket, packet socket reads and writes, the TX ring, the retry `timerfd`, the forward peer | Block on nl80211 or the filesystem |

Two threads keep slow control work off the data path: a channel switch or a TX power change through nl80211 takes milliseconds, and slot files are `fsync`ed. The data thread takes no locks. It reads settings that the control thread publishes through atomics (`Counters.h`):

| Data | Writer | Reader | Mechanism |
|------|--------|--------|-----------|
| Radiotap TX template (`TxProfile`) | control | data | `TxProfileStore`: two slots and an `atomic<uint8_t>` index. The writer fills the inactive slot and release-stores its index; the reader copies the ≤16-byte template once per drain |
| Addr3 filter | control | data | `atomic<uint64_t>` (`mac_filter_pack`) |
| Counters | data | control | `atomic<uint32_t>`, relaxed, wrapping |
| Last RSSI | data | control | `atomic<int16_t>`; `INT16_MIN` means no frame yet |
| Kernel Addr3 BPF | control | kernel | `SO_ATTACH_FILTER` on the shared packet socket (atomic replace) |

## Bring-up

`App::bring_up()` (`src/radio/App.cpp`) runs in this order. Any failure logs an error and the process exits with status 1 (status 2 for a missing `--config`):

| Step | What |
|------|------|
| 1. Config | `AppConfig::load`: unknown keys, missing required keys and out-of-range values fail ([Configuration](#configuration)) |
| 2. Calibration | `PowerCal::load_csv(radio.txpower)` ([TX power](#tx-power)) |
| 3. Interface | `DeviceSelector` resolves `radio.device`, `radio.usb_port` (`/sys/bus/usb/devices/<port>:1.0/net/*`) or `radio.mac` (`/sys/class/net/*/address`), and keeps only an interface whose `device/driver` basename equals `radio.driver`. The error names every interface it saw and its driver |
| 4. NetworkManager | `nmcli device set <if> managed no`; failure is only a warning |
| 5. nl80211 | Open a generic netlink socket and resolve the `nl80211` family |
| 6. Regulatory domain | `NL80211_CMD_REQ_SET_REG` with `radio.regdom`; failure is only a warning |
| 7. Monitor mode | Link down, `NL80211_CMD_SET_INTERFACE` type monitor, link up, wait 300 ms, confirm the type with `NL80211_CMD_GET_INTERFACE`. Up to 5 tries: after `nmcli … managed no`, `wpa_supplicant` detaches asynchronously and can switch the interface back to managed after a successful set |
| 8. Channel list | `NL80211_CMD_GET_WIPHY` split dump; every frequency without `DISABLED` or `NO_IR` becomes a channel number. Empty list fails |
| 9. Packet socket | `AF_PACKET`/`SOCK_RAW`/`ETH_P_ALL` bound to the ifindex, `PACKET_QDISC_BYPASS`, `SO_RCVBUF = tune.sock_rcvbuf`, non-blocking |
| 10. Radio settings | Apply the current slot (radio settings and filter); if there is none or it does not apply, apply the defaults. Failure to apply the defaults fails bring-up |
| 11. Data thread | Bind the inject and forward-registration sockets on `net.bind`, bind the forward send socket to an ephemeral port, start the thread |
| 12. m-plane | Bind the console socket and register it with the reactor, then run the reactor |

**Shutdown.** `SIGINT`/`SIGTERM` write the shutdown eventfd; the reactor stops, the data thread is joined, sockets close. The interface is left in monitor mode so a restart does not flap it through managed mode. A signal during bring-up exits immediately.

**Reset.** `reset` sets a flag that the 50 ms reactor timer picks up. It then sleeps 200 ms so the reply leaves, and `execv`s `/proc/self/exe` with the original arguments. The new process repeats bring-up from step 1 and applies the current slot, which matches the ESP32's reboot.

## Configuration

`key = value`, `#` starts a comment. Samples: `configuration/radio-a.cfg`, `radio-b.cfg`.

| Key | Required | Default | Meaning |
|-----|----------|---------|---------|
| `radio.device` / `radio.usb_port` / `radio.mac` | exactly one | | Which dongle: interface name, USB port path (e.g. `3-1.1`), or permanent MAC |
| `radio.driver` | no | `rtl88xxau_wfb` | Required driver basename. Rejects the stock `88XXau`, which ignores the TX power override |
| `radio.regdom` | no | `BO` | Two-letter regulatory domain |
| `radio.bandwidth` | no | `20` | `20` (HT20) or `40` (HT40+, and the radiotap MCS `BW_40` flag) |
| `radio.txpower` | yes | | Path to the calibration CSV; relative to the config file's directory |
| `radio.rx_bpf` | no | `false` | Also filter Addr3 in the kernel ([below](#kernel-addr3-filter)) |
| `radio.tx_retry_us` | no | `50000` | How long a frame may wait on a busy driver before it counts as `dropped_wifi` |
| `net.bind` | no | `0.0.0.0` | Address for all UDP sockets |
| `net.console_port` | no | `2201` | m-plane |
| `net.inject_port` | no | `9000` | d-plane inject |
| `net.forward_port` | no | `9210` | d-plane forward registration |
| `net.trusted_ipv4` | no | empty | If set, datagrams from other sources are dropped on all three ports |
| `state.dir` | yes | | Slot files, `current`, `reset_id`. Created with mode 0700 when first needed |
| `tune.tx_queue_sz` | no | `20` | TX ring size, 1–64 |
| `tune.rx_batch` | no | `16` | Datagrams/frames per `recvmmsg`, 1–64 |
| `tune.sock_rcvbuf` | no | `4194304` | `SO_RCVBUF` on the packet socket |
| `log.level` | no | `info` | `error`, `warn`, `info`, `debug`. Logs go to stderr |

These replace the ESP32's m-plane `tune_*` and `network` commands, which reply `NOK ENOTSUP` here.

## Inject path (UDP → air)

`Injector` (`src/radio/Injector.cpp`) runs on the data thread.

```
on inject socket readable:
  n = recvmmsg(inject socket, up to tune.rx_batch datagrams of ≤1500 bytes)
  for each datagram:
    ether_pkt++
    untrusted source                        → dropped_invalid_frame
    length outside 24..1472, or truncated   → dropped_invalid_frame
    ring holds tune.tx_queue_sz frames      → dropped_tx_queue
    copy into the ring with its enqueue time
  drain_ring()

drain_ring():
  profile = current TxProfile
  while ring not empty:
    sendmsg(packet socket, iov = [profile radiotap, MPDU])
      ok                  → air_pkt++, pop
      ENOBUFS / EAGAIN    → if waited > radio.tx_retry_us: dropped_wifi++, pop
                            else arm a 200 µs timerfd and return (retry on expiry)
      other error         → dropped_wifi++, pop
```

- **Radiotap template.** The header comes from the current `TxProfile`, rebuilt only when `radio_tx` changes the modulation. It is sent as its own iovec, so the MPDU is never re-assembled. See [Modulation](#modulation) for its bytes.
- **No ACKs.** Every template sets `TX_FLAGS = NOACK`. Addr1 of winject frames carries manager data and can look unicast; without NOACK the driver retries each frame about 6 times.
- **`air_pkt` means accepted by the driver.** The driver gives no TX status in monitor mode, so `in_flight` is always 0 and nothing confirms the frame left the antenna.
- **Backpressure.** By default the driver drops frames silently when it runs out of transmit buffers, and `sendmsg` still succeeds. Loading the module with `MaxTxBufLen=<n>` makes it return busy instead, which reaches the radio as `ENOBUFS` through `PACKET_QDISC_BYPASS` and triggers the retry above. Recommended: `options 88XXau_wfb MaxTxBufLen=32` in `/etc/modprobe.d/`. Without it, driver drops are invisible in `tx_info`.
- **Pacing.** The radio does not pace; it sends as fast as the driver accepts. The manager paces with its airtime model.

## Receive path (air → UDP)

`Forwarder` (`src/radio/Forwarder.cpp`) runs on the data thread.

```
on packet socket readable:
  dropped_rx_queue += new kernel drops (PACKET_STATISTICS tp_drops)
  n = recvmmsg(packet socket, up to tune.rx_batch frames of ≤2048 bytes)
  for each frame:
    air_pkt++
    radiotap does not parse                  → dropped_filter_mismatched
    radiotap has TX_FLAGS (our own frame)    → dropped_filter_mismatched
    MPDU ‖ FCS: as received if F_FCS, else append crc32(MPDU) little-endian
    length outside 28..1504 (with FCS)       → dropped_filter_mismatched
    Addr3 != rx_filter_addr3 (when set)      → dropped_filter_mismatched
    last RSSI = DBM_ANTSIGNAL
    no registered peer                       → dropped_no_peer
    queue for the peer
  sendmmsg(queued) → ether_pkt += sent, dropped_send_failed += the rest
```

- **Radiotap parsing** uses the radiotap-library iterator (`src/vendor/radiotap/`). The driver emits extended present bitmaps and vendor namespaces, so fixed offsets would break.
- **Own frames.** The driver loops about 90 % of the frames this radio injects back into its own RX path, marked with radiotap `TX_FLAGS`. They are dropped and show up in `dropped_filter_mismatched`.
- **FCS.** `rtl88xxau_wfb` delivers every frame with `F_FCS` set and the real on-air FCS appended, so the trailer is forwarded as received. The driver frees frames with a CRC or ICV error before monitor mode (`usb_ops_linux.c`), so on-air corruption shows up as missing frames, not as FCS failures at the manager. Frames with `F_BADFCS` would be forwarded unchanged if a patched driver passed them up.
- **Peer registration.** Any datagram arriving on `net.forward_port` (from `net.trusted_ipv4` when set) makes its source the peer; the most recent sender wins. The manager sends one about every second.

### Kernel Addr3 filter

With `radio.rx_bpf = true`, `rx_filter_addr3` also attaches a classic BPF program (`RxFilterBpf.cpp`) to the packet socket. It reads the little-endian radiotap length from bytes 2–3 and compares the 6 Addr3 bytes behind it, so only matching frames are copied to user space. Attaching replaces the old program atomically; a cleared filter detaches it.

This saves CPU on a busy channel, but frames the kernel drops are never counted: `rx_info air_pkt` then counts only matching frames, and `dropped_filter_mismatched` only counts radiotap, own-frame and length drops.

## Radio configuration

`RealtekRadio` (`src/radio/RealtekRadio.cpp`) holds the current `radio_config` and the Addr3 filter. The m-plane reaches it through `RealtekRadioBackend` (`radio_tx`, `radio_tx_info`, `rx_filter_addr3`) and `RealtekDeviceBackend` (`save`, `load`).

`set_radio` merges the patch into a copy of the current settings, validates the whole result, then applies only what changed: channel, then TX power, then modulation. If channel or power fails, it re-applies the previous values and replies `EIO`. The new settings become current only when every step succeeded.

### Channel and bandwidth

`NL80211_CMD_SET_CHANNEL` with `NL80211_CHAN_HT20`, or `NL80211_CHAN_HT40PLUS` when `radio.bandwidth = 40`. Valid channels are the ones the wiphy reported at startup under `radio.regdom` (2.4 GHz 1–14 and 5 GHz 36–177 as allowed). HT40 needs channel + 4 to be valid too. HT40 works on air but the manager's airtime model does not know it yet; keep `radio.bandwidth = 20` with the current manager.

### Modulation

The driver applies the radiotap rate to each frame (`rtw_monitor_xmit_entry` → descriptor with `USE_RATE`, `DISABLE_FB`), so no `iw … set bitrates` is needed and the modulation is purely a property of the TX template (`Radiotap.cpp`). Names match `winject-l3`'s `PhyAirtime` so the manager's pacing works unchanged.

| Names | Template |
|-------|----------|
| `DSS_1M_L`, `DSS_2M_L`, `CCK_5M_L`, `CCK_11M_L`, `OFDM_6M` … `OFDM_54M` | 12 bytes: `00 00 0c 00 04 80 00 00 <rate> 00 08 00`. Present = `RATE` + `TX_FLAGS`; `rate` in 500 kbit/s units (2, 4, 11, 22, 12 … 108); `TX_FLAGS = NOACK` |
| `OFDM_MCS0_LGI` … `OFDM_MCS7_SGI` | 13 bytes: `00 00 0d 00 00 80 08 00 08 00 <known> <flags> <mcs>`. Present = `TX_FLAGS` + `MCS`; known = `HAVE_BW\|HAVE_MCS\|HAVE_GI\|HAVE_FEC\|HAVE_STBC`; flags = `SGI` for `_SGI`, `BW_40` with `radio.bandwidth = 40`; index 0–7 |

What the driver cannot do, verified on the bench (details in [implementation.md](./implementation.md) §1, §6.2):

| Feature | Status | Consequence |
|---------|--------|-------------|
| Short preamble | Radiotap `FLAGS` is ignored on TX | `DSS_2M_S`, `CCK_5M_S`, `CCK_11M_S` reply `EINVAL` |
| LDPC | Sent correctly, but an 8812AU in monitor mode cannot decode HT LDPC | Always off (`HAVE_FEC` set, flag clear = BCC). No config option |
| STBC | No receiver decodes the 8812AU's STBC output | Always off (`HAVE_STBC` set, value 0). No config option |
| MCS 8–15, VHT | Possible on the 8812AU | Not offered until `PhyAirtime` knows them |

### TX power

The m-plane gives dBm (2–20). `rtl88xxau_wfb` takes a power index 0–63 through the svpcom override: `NL80211_CMD_SET_WIPHY` with `TX_POWER_SETTING = FIXED` and `TX_POWER_LEVEL = -idx × 100` mBm (the same as `iw … set txpower fixed -<idx×100>`). After setting it, the radio reads the interface back and expects `txpower = -idx × 100`; anything else is `EIO`.

**Calibration.** `radio.txpower` points at a CSV of measured output per index (`configuration/txpower.csv`: `idx,tx_power`, header and `#` comments allowed, sparse rows allowed, at least 2 rows with index 1–63). Bad numbers, indices outside 0–63 and duplicates fail startup with the file and line.

**Mapping** (`PowerCal::dbm_to_idx`): the index 1–63 whose measured value is closest to the request; ties go to the lower index. This copes with the shipped table not being monotonic. Index 0 is never chosen: it means "no override" and gives the driver default, about 10 dB above index 35. A request more than 0.5 dB outside the measured range is clamped to the nearest end and logged once. Every change logs `tx_power <n> dBm -> idx <i> (measured <x> dBm)`. The m-plane reports the requested value. With the shipped table, 20 dBm → index 63 (18.8 dBm) and 2 dBm → index 29 (1.5 dBm).

**Caveats:**

- **The override is global across dongles.** `rtw_tx_pwr_idx_override` is one module-wide variable. Setting power on one dongle takes effect on every other `rtl88xxau_wfb` dongle the next time it recomputes power (for example on a channel change), and the read-back cannot see this. Give every radio on one host the same `tx_power`, or run them on separate hosts.
- **Per-frame power is not possible.** The driver ignores radiotap `DBM_TX_POWER`.
- **One table for all dongles and channels.** It was measured on one dongle on channel 6. Point a radio's `radio.txpower` at its own CSV if it differs.

### CCA

The Realtek driver has no CCA control. `cca=true` is accepted and changes nothing; `cca=false` replies `EINVAL`. Remove `winject.cca = false` from manager configs used with this radio.

## Settings and state

`Settings` (`src/radio/Settings.cpp`) keeps everything in `state.dir`:

| File | Contents |
|------|----------|
| `slot0` … `slot9` | Radio settings and `rx_filter_addr3` ([format](./mplane.md#device)), written via `.tmp` + `fsync` + `rename` |
| `current` | Number of the last slot saved or loaded; applied at startup |
| `reset_id` | Last accepted `reset id=` |

There is no network or tune state to save: those come from the config file.

## Addressing

The radio does not route on addresses; it only reads Addr3 to filter RX.

- **Addr3** of winject frames is `CA:FE:BA:BE` followed by a 2-byte domain set by the manager. Each radio's `rx_filter_addr3` selects the traffic it forwards, so two radio pairs can share a channel.
- A **cleared** filter forwards every frame the driver delivers, including other networks' traffic.
- Addr1, Addr2 and the sequence number of injected frames are sent as the manager wrote them; the driver keeps the host's sequence number.

## Frame accounting

Every frame that enters a path ends in exactly one counter, so a host can diff `tx_info` / `rx_info` across a run and locate loss:

```
Inject:  ether_pkt (tx) = dropped_invalid_frame + dropped_tx_queue + dropped_wifi + air_pkt (tx)
                          + tx_queue_sz (still in the ring)
Forward: air_pkt (rx)   = dropped_filter_mismatched + dropped_no_peer + dropped_send_failed + ether_pkt (rx)
```

Differences from the ESP32's identities:

- `in_flight` and `rx_queue_sz` are always 0.
- `dropped_rx_queue` counts frames the kernel dropped **before** the radio read them, so it is outside the forward identity. Total frames offered by the driver ≈ `air_pkt (rx) + dropped_rx_queue`.
- Driver-internal TX drops without `MaxTxBufLen` and on-air FCS failures are invisible: the first counts as `air_pkt (tx)`, the second never reaches `air_pkt (rx)`.
- With `radio.rx_bpf = true`, frames with the wrong Addr3 are not counted at all.

| Where | Counter | Meaning |
|-------|---------|---------|
| Inject socket | `tx_info ether_pkt` | Datagrams read |
| Injector | `dropped_invalid_frame` | Bad length, truncated, or untrusted source |
| Injector | `dropped_tx_queue` | Ring full |
| `drain_ring` | `dropped_wifi` | `sendmsg` error, or busy past `radio.tx_retry_us` |
| `drain_ring` | `tx_info air_pkt` | `sendmsg` accepted |
| Kernel | `dropped_rx_queue` | Packet socket buffer full |
| Packet socket | `rx_info air_pkt` | Frames read |
| Forwarder | `dropped_filter_mismatched` | Radiotap, own frame, length, or Addr3 filter |
| Forwarder | `dropped_no_peer`, `dropped_send_failed` | No peer registered; `sendmmsg` failed |
| Forwarder | `rx_info ether_pkt` | Datagrams sent to the peer |

## Host setup

| Item | Why |
|------|-----|
| `rtl88xxau_wfb` bound to the dongle | Required for injection with per-frame rate and the TX power override. If `88XXau` is also loaded, blacklist it so a replugged dongle does not bind to it |
| `options 88XXau_wfb MaxTxBufLen=32` | Turns silent driver drops into `ENOBUFS` backpressure ([Inject path](#inject-path-udp--air)) |
| NetworkManager not managing the dongle | Avoids the monitor-mode race in bring-up step 7 (a udev rule setting `NM_UNMANAGED=1` for the driver avoids it entirely) |
| `CAP_NET_RAW`, `CAP_NET_ADMIN` | Packet socket; nl80211 and link flags |
| `libnl-3`, `libnl-genl-3` | nl80211 |

## Source map

| Path | Contents |
|------|----------|
| `src/radio/Main.cpp`, `App.*` | Process entry, bring-up, signals, reset by `execv` |
| `src/radio/Config.*` | Config file parser |
| `src/radio/DeviceSelector.*` | Interface lookup through sysfs (root path injectable for tests), driver check |
| `src/radio/NetLink.*` | Link up/down (`SIOCSIFFLAGS`), `nmcli` release |
| `src/radio/Nl80211.*` | libnl-genl: monitor mode, channel, TX power, regdom, channel list, interface info; channel ↔ frequency |
| `src/radio/Modulation.*` | Modulation table, channel/modulation validity |
| `src/radio/PowerCal.*` | Calibration CSV, dBm → index |
| `src/radio/Radiotap.*`, `TxProfile.h` | TX templates, RX parse; the lock-free template store |
| `src/radio/RealtekRadio.*` | Apply and roll back radio settings, Addr3 filter, BPF attach |
| `src/radio/RealtekBackends.*` | m-plane device and radio backends |
| `src/radio/Settings.*` | Slot files, current slot, reset id |
| `src/radio/MplaneServer.*` | m-plane UDP socket |
| `src/radio/DataPlane.*` | Data thread, its sockets and epoll loop |
| `src/radio/Injector.*` | Inject ring, `sendmsg`, busy retry |
| `src/radio/Forwarder.*` | RX batch, filters, FCS, peer, `sendmmsg` |
| `src/radio/PacketSocket.*` | `AF_PACKET` socket, statistics, BPF attach/detach |
| `src/radio/RxFilterBpf.*` | Addr3 cBPF program and a small interpreter for tests |
| `src/radio/Fcs.*` | CRC-32 FCS |
| `src/radio/Counters.h` | Shared atomics between the threads |
| `src/vendor/mplane/` | m-plane parser from `winject-radio-esp32`, plus the `config.h` / `frame.h` shims. Re-sync with `scripts/sync_mplane.sh <commit>` |
| `src/vendor/radiotap/` | radiotap-library iterator (ISC) |
| `src/test/` | GoogleTest target `winject-radio-realtek-tests`: config, device selector, modulation, radiotap, power calibration, BPF, FCS, settings, and the vendored m-plane tests. No root or hardware needed |
| `configuration/` | `radio-a.cfg`, `radio-b.cfg` (bench pair on one host), `txpower.csv` |
