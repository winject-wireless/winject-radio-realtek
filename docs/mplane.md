# Radio management plane (m-plane)

The **m-plane** is a text command protocol over **UDP**. It is the same protocol the ESP32 radio speaks (`winject-radio-esp32/docs/mplane.md`), so **winject-manager** drives both radios with one client. This radio implements the device and radio commands; it does not implement networking, tuning or the built-in traffic tests, because the Linux host owns those.

Implementation:

| Layer | Files |
|-------|-------|
| UDP socket, source filter, reply buffering | `src/radio/MplaneServer.cpp` |
| Parsing, dispatch, reply format (vendored from `winject-radio-esp32`, commit in `src/vendor/mplane/VERSION`) | `src/vendor/mplane/mplane_commands.cpp`, `mplane_args.cpp`, `mplane_reply.cpp`, `mplane_req_id.cpp` |
| Backend interfaces | `src/vendor/mplane/mplane_backend.h` |
| Realtek backends | `src/radio/RealtekBackends.cpp` (device, radio), `RealtekRadio.cpp` (radio settings), `Settings.cpp` (slots) |
| Build-time limits (shim for the vendored code) | `src/vendor/mplane/config.h` |

The **d-plane** (frame inject and RX forward) uses its own UDP ports and is not configured through the m-plane; see [D-plane](#d-plane).

How the radio behind these commands works (threads, inject and receive paths, counters): [winject.md](./winject.md).

## Transport

| Item | Value |
|------|--------|
| Protocol | UDP/IPv4 |
| Port | `net.console_port` in the radio config, default **2201** (not 22: `sshd` owns that port on a Linux host) |
| Bind | `net.bind`, default `0.0.0.0`. Use `127.0.0.1` when the manager runs on the same host |
| Source filter | If `net.trusted_ipv4` is set, datagrams from any other source are dropped without a reply |
| Request size | Up to 1499 bytes per datagram; longer datagrams are truncated |
| Reply size | All reply lines for one request are collected, then sent in datagrams of at most 16384 bytes |
| Reply routing | `sendto` back to the request's source IP and port |

A request datagram holds one or more newline-separated command lines; each line gets its own reply line(s), all returned together. No line terminator is required. Leading/trailing blanks and CR are ignored; empty lines and lines starting with `#` produce no reply.

Command names, aliases, and argument keys are matched **case-insensitively**. Arguments are `key=value` tokens separated by spaces or tabs; unknown or repeated keys are rejected with `NOK EINVAL`.

### Request correlation (`cmd:<u8>`)

**winject-manager** prefixes each line with **`cmd:<u8>`** so replies can be paired with requests. The id is only for correlating replies with requests.

| Direction | Wire format |
|-----------|-------------|
| Client → radio | `cmd:<u8> <m-plane line>` |
| Radio → client (success) | `OK:<u8> …` on every reply line for that command |
| Radio → client (failure) | `NOK:<u8> <code>` |

Info replies (lines that do not start with `OK`) get `OK:<u8> ` prepended. Lines without the `cmd:` prefix get uncorrelated replies. A malformed prefix (non-numeric, above 255, or with no command after it) replies an uncorrelated `NOK EINVAL`.

| Request | Reply |
|---------|--------|
| `cmd:1 radio_caps_info` | `OK:1 radio_caps_info fcs=ACTUAL` |
| `cmd:2 radio_tx channel=6 modulation=OFDM_24M` | `OK:2 radio_tx channel=6 tx_power=20 modulation=OFDM_24M cca=true` |
| `cmd:3 rx_filter_addr3 addr=ca:fe:ba:be:04:d2` | `OK:3 rx_filter_addr3 addr=ca:fe:ba:be:04:d2` |
| `cmd:4 save 0` | `OK:4` |
| `cmd:5 tx_info` | `OK:5 tx_info tx_queue_sz=0 in_flight=0 …` |
| `cmd:6 bogus` | `NOK:6 ENOSYS` |

### Value formats

| Type | Format |
|------|--------|
| `<bool>` | `1` `true` `on` `yes` / `0` `false` `off` `no` |
| `<mac>` | `aa:bb:cc:dd:ee:ff`, `aa-bb-cc-dd-ee-ff`, or `aabbccddeeff` (hex, any case); printed lowercase with `:` |
| `<u8>` | unsigned decimal |

## Replies

| Reply | Meaning |
|-------|---------|
| `OK` | Success, no body |
| `OK <command> key=value ...` | Success; echoes the resulting state |
| `<command> key=value ...` | Info reply of a query (`tx_info`, `rx_info`, `radio_tx_info`) |
| `pong` | Reply to `ping` |
| `NOK <code>` | Failure, see below |

| Code | Meaning on this radio |
|------|-----------------------|
| `EINVAL` | Malformed command, unknown or repeated key, value out of range, unsupported `radio_tx` combination (see [`radio_tx`](#radio_tx)), `cca=false`, `reset mode=OTA` |
| `ENOSYS` | Unknown command |
| `ENOTSUP` | `network`, `tune_param`, `tune_tx_param`, `tune_rx_param` with arguments |
| `ENODEV` | Every `test_*` command (no test backend) |
| `ENOENT` | `load` of a slot with no file |
| `EIO` | nl80211 failure while applying `radio_tx` or `load`, TX power read-back mismatch, BPF attach failure, or a slot/state file that cannot be written |

## Modes

There is one mode. `reset mode=WINJECT` is accepted and behaves like a plain `reset`; `reset mode=OTA` replies `NOK EINVAL`. There is no OTA HTTP server: update the binary on the host.

## Commands

`help` lists every command of the vendored parser with its alias and usage, followed by `modulations: <names>` with the names this radio accepts. It still lists the commands this radio does not support.

### Device

| Command | Alias | Arguments | Reply |
|---------|-------|-----------|-------|
| `help` | `?` | | usage lines, then `modulations: …` |
| `ping` | `p` | | `pong` |
| `reset` | `r` | `[mode=WINJECT]` | `OK`, then the process restarts itself |
| `save` | | `<slot 0-9>` | `OK`; stores the radio settings and `rx_filter_addr3`, and makes the slot current |
| `load` | | `<slot 0-9>` | `OK`; applies the slot's radio settings and filter now, and makes the slot current. `NOK ENOENT` if the slot is empty |
| `network` | `sn` | | Not supported, see below |
| `tune_param`, `tune_tx_param`, `tune_rx_param` | `tp`, `ttp`, `trp` | | Not supported, see below |

**Reset.** The reply is sent first. Within about 250 ms the process `execv`s `/proc/self/exe` with its original arguments, so it re-reads the config file, repeats bring-up, and applies the current slot, like the ESP32's reboot. Bring-up waits about 1 s for a stable device when the dongle was missing (same debounce as USB replug recovery). The radio also restarts itself when its dongle is replugged while running; `ts` in `tx_info` restarts near zero in both cases. The interface stays in monitor mode across the restart. If a client is unsure whether `reset` completed (no reply or timeout), it polls `tx_info` and compares `ts` (uptime in µs, which restarts near zero after every reset) with the elapsed time since the request; it resends `reset` only when `ts` shows the radio did not restart.

**Boot settings.** At startup the radio applies the current slot (`state.dir/current`, the last slot saved or loaded). A missing slot or one that does not apply falls back to the defaults: `channel=1 tx_power=20 modulation=OFDM_6M cca=true`, filter cleared. Changes made with `radio_tx` or `rx_filter_addr3` are not persisted until `save`.

**Slot files.** `state.dir/slot<n>` is plain text, written via a temporary file, `fsync` and `rename`:

```
version=1
channel=6
tx_power=15
modulation=OFDM_24M
cca=true
rx_filter_addr3=ca:fe:ba:be:04:d2
```

`rx_filter_addr3` is omitted when the filter is cleared. Unknown keys are ignored and missing keys keep their default.

**Not supported.** The host OS owns networking, and buffer sizes come from the radio config file (`tune.*`, see [winject.md](./winject.md#configuration)).

| Command | With arguments | Without arguments |
|---------|----------------|-------------------|
| `network` | `NOK ENOTSUP` (or `NOK EINVAL` if the arguments do not parse) | `OK network ip=0.0.0.0/24 type=dhcp timeout=5`: placeholder values, not the host's |
| `tune_param` | `NOK ENOTSUP` | `OK tune_param set_eth_dma_burst_len=32`: placeholder |
| `tune_tx_param` | `NOK ENOTSUP` | `OK tune_tx_param eth_rx_ring_sz=0 tx_queue_sz=20 wifi_tx_ring_sz=0`: placeholder, not `tune.tx_queue_sz` |
| `tune_rx_param` | `NOK ENOTSUP` | `OK tune_rx_param eth_tx_ring_sz=0 rx_queue_sz=8 wifi_rx_ring_sz=0`: placeholder |

### Radio

| Command | Alias | Arguments | Reply |
|---------|-------|-----------|-------|
| `tx_info` | `ti` | | `tx_info tx_queue_sz=<n> in_flight=0 dropped_invalid_frame=<u32> dropped_tx_queue=<u32> dropped_wifi=<u32> ether_pkt=<u32> air_pkt=<u32> ts=<u64>` |
| `rx_info` | `ri` | | `rx_info rx_queue_sz=0 dropped_filter_mismatched=<u32> dropped_rx_queue=<u32> dropped_no_peer=<u32> dropped_send_failed=<u32> ether_pkt=<u32> air_pkt=<u32> ts=<u64>` |
| `radio_tx` | `rt` | `channel=<n> tx_power=<2-20> modulation=<name> cca=true` | `OK radio_tx channel=<n> tx_power=<dBm> modulation=<name> cca=true` |
| `radio_tx_info` | `rti` | | `radio_tx channel=… tx_power=… modulation=… cca=true` and, once a frame has passed the filter, `radio_rx rssi=<dBm>` |
| `radio_caps_info` | `rci` | (none) | `OK radio_caps_info fcs=ACTUAL` |
| `rx_filter_addr3` | `rf3` | `addr=<mac>` or `addr=` | `OK rx_filter_addr3 addr=<mac or empty>` |

#### `radio_tx`

Any subset of the keys may be given; omitted keys keep their value, and no keys at all prints the current settings. The new settings are checked as a whole before anything is applied:

| Check | Rule |
|-------|------|
| `channel` | Parsed as 1–177, then must be in the channel list read from the wiphy at startup (channels flagged `DISABLED` or `NO_IR` under `radio.regdom` are left out) |
| `tx_power` | 2–20 dBm |
| `modulation` | A name from the table below, case-insensitive. Short-preamble names (`*_S`) are rejected: the driver ignores the radiotap short-preamble flag |
| Channel / modulation pair | DSSS/CCK only on channels 1–14; channel 14 only DSSS/CCK; with `radio.bandwidth = 40`, channel + 4 must also be in the list (HT40+) |
| `cca` | Only `true`. There is no CCA control in the Realtek driver, so `cca=false` replies `NOK EINVAL` |

Then changed values are applied in order: channel (nl80211), TX power (nl80211, then read back), modulation (new radiotap template for the inject path). If a step fails, the previous values are re-applied and the reply is `NOK EIO`.

`tx_power` is reported as requested, not as measured: the radio picks the power index whose measured output is closest (see [winject.md](./winject.md#tx-power)). The modulation is echoed as it was sent, so send it in upper case to get canonical names back. Defaults: `channel=1 tx_power=20 modulation=OFDM_6M cca=true`.

| Name | PHY | Rate (Mbit/s) |
|------|-----|---------------|
| `DSS_1M_L`, `DSS_2M_L` | DSSS, long preamble | 1, 2 |
| `CCK_5M_L`, `CCK_11M_L` | CCK, long preamble | 5.5, 11 |
| `OFDM_6M` … `OFDM_54M` | 802.11a/g OFDM | 6, 9, 12, 18, 24, 36, 48, 54 |
| `OFDM_MCS0_LGI` … `OFDM_MCS7_LGI` | 802.11n HT, 1 stream, 800 ns GI | 6.5 … 65 at 20 MHz |
| `OFDM_MCS0_SGI` … `OFDM_MCS7_SGI` | 802.11n HT, 1 stream, 400 ns GI | 7.2 … 72.2 at 20 MHz |

`DSS_2M_S`, `CCK_5M_S` and `CCK_11M_S` exist on the ESP32 but reply `NOK EINVAL` here.

#### `radio_tx_info`

`radio_rx rssi=` is the radiotap `DBM_ANTSIGNAL` of the last received frame that passed the Addr3 filter. It is absent until such a frame arrives.

#### `radio_caps_info`

Always `fcs=ACTUAL`: forwarded frames carry the real on-air FCS. Read-only, not stored in slots; any argument replies `NOK EINVAL`.

#### `rx_filter_addr3`

`addr=<mac>` forwards only received frames whose Addr3 equals `<mac>`. `addr=` (empty) clears the filter and forwards every frame the driver delivers. With no argument it prints the current filter. With `radio.rx_bpf = true` the filter also runs in the kernel; see [winject.md](./winject.md#kernel-addr3-filter).

#### `tx_info` / `rx_info`

Occupancy fields are current values; the other fields are counters since process start (`u32`, wrapping at 2³²; diff two readings). `ts` is process uptime in µs (`CLOCK_MONOTONIC`). Counters restart from zero after `reset`.

**`tx_info` fields**

| Field | Meaning |
|---|---|
| `tx_queue_sz` | Frames in the inject ring waiting for the driver |
| `in_flight` | Always 0: the driver gives no TX status in monitor mode |
| `dropped_invalid_frame` | Inject datagrams outside 24–1472 bytes, truncated, or from a source other than `net.trusted_ipv4` |
| `dropped_tx_queue` | Inject datagrams dropped because the ring was full |
| `dropped_wifi` | Frames the packet socket refused (any error, or `ENOBUFS`/`EAGAIN` for longer than `radio.tx_retry_us`) |
| `ether_pkt` | Datagrams read from the d-plane port |
| `air_pkt` | Frames the driver accepted. Not a confirmation that they went on air |
| `ts` | Uptime in µs |

**`rx_info` fields**

| Field | Meaning |
|---|---|
| `rx_queue_sz` | Always 0: frames are forwarded in the same batch they are read |
| `dropped_filter_mismatched` | Frames read but not forwarded: bad radiotap, own injected frames looped back, length outside 28–1504 bytes with FCS, or Addr3 filter miss |
| `dropped_rx_queue` | Frames the kernel dropped because the packet socket's receive buffer was full (`PACKET_STATISTICS`). These were never read, so they are not in `air_pkt` |
| `dropped_no_peer` | Frames that passed the filter while no host had registered on the d-plane port |
| `dropped_send_failed` | Forward `sendmmsg` failures |
| `ether_pkt` | Frames sent to the registered host |
| `air_pkt` | Frames read from the packet socket. With `radio.rx_bpf = true`, only frames the kernel filter kept |
| `ts` | Uptime in µs |

### Tests

`test_ether_rx`, `test_ether_tx`, `test_ether_rx_stat`, `test_wifi_rx`, `test_wifi_tx` and `test_wifi_rx_stat` always reply `NOK ENODEV`. They measure Ethernet/WiFi DMA contention inside the ESP32, which this radio does not have. The manager does not send them.

## D-plane

One UDP port (`net.dplane_port`, default **9000**) on `net.bind` serves inject, peer registration, and forward (same length rules as the ESP32).

| Payload size | Meaning |
|--------------|---------|
| 1–23 bytes | Registration: source becomes the forward peer; most recent sender wins (manager sends a 1-byte datagram about once per second) |
| 24–1472 bytes | MPDU to inject (header included, no FCS). With `net.trusted_ipv4`, other sources are dropped as `dropped_invalid_frame` |
| 0 bytes, over 1472, truncated | Dropped (counted as today where a counter exists) |

Injected MPDUs are sent to the air with a radiotap header for the current modulation (`TX_FLAGS = NOACK`). Accepted received frames are sent to the registered peer as **MPDU ‖ 4-byte FCS**, from the same d-plane socket.

The 4-byte trailer is the on-air FCS (CRC-32, little-endian), so `radio_caps_info` reports `fcs=ACTUAL` and the receiver checks `crc32(MPDU) == trailer`. If the driver does not include the FCS (radiotap `F_FCS` clear), the radio computes it.

`rtl88xxau_wfb` drops frames that fail the FCS check before monitor mode sees them, so on-air errors show up as missing frames, not as FCS failures at the manager.

Manager settings for this radio: `winject.radio_fcs = actual` or unset (the manager then asks `radio_caps_info`), and no `winject.cca = false`.

## Example session

```bash
RADIO=127.0.0.1 PORT=2201
mp() { echo -n "$*" | nc -u -w1 "$RADIO" "$PORT"; }

mp ping
mp radio_caps_info
mp radio_tx channel=6 modulation=OFDM_24M tx_power=15
mp rx_filter_addr3 addr=ca:fe:ba:be:00:01
mp radio_tx_info
mp save 1
mp tx_info
mp rx_info
mp reset           # restarts; confirm with tx_info ts if the OK reply is lost
```
