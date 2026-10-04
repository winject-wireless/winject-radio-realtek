# winject-radio-realtek: implementation details

**Status:** Design (not implemented)  
**Date:** 2026-10-02  
**Builds on:** [plan.md](plan.md)

This document turns the plan into concrete modules, wire formats, algorithms, and tests. Statements marked *(verified)* were checked against source code on 2026-10-02. Statements marked *(bench)* still need a measurement on real hardware.

## 0. Decisions assumed here

The plan left four decisions open. This document uses the plan's recommendations. Each can be changed without affecting the rest of the design.

| # | Decision | Assumed | Where it matters |
|---|---|---|---|
| 1 | Language | C++17, CMake, bfc reactor, GoogleTest (same as `winject-l3`) | Everything |
| 2 | Where it runs | Both. `net.bind` picks the address. Bring-up and tests run on the same host as the manager over `127.0.0.1` | §3, §12 |
| 3 | `cca=false` | Rejected with `NOK EINVAL`; `cca=true` is accepted and has no effect | §6.3 |
| 4 | Calibration | An external `txpower.csv` (index → measured dBm), referenced from each config with `radio.txpower` | §7 |

## 1. What changed since plan.md

Reading the current sources turned up five facts that change the plan:

1. **The m-plane has `radio_caps_info`.** The manager sends `radio_caps_info` first, then `radio_tx`, `rx_filter_addr3`, and `save <slot>` (`winject-l3/src/manager/console/ConsoleClient.cpp:452`). This radio answers `fcs=ACTUAL`.
2. **The ESP32 m-plane parser can be reused.** `winject-radio-esp32/src/winject-esp32/mplane/` is split into parsing (`mplane_commands`, `mplane_args`, `mplane_reply`, `mplane_req_id`) and backend interfaces (`mplane_backend.h`). Its only ESP32 dependency is `config.h` macros. It already builds on a host under `src/host_test/`. We vendor it instead of writing a new parser (§5).
3. **The driver drops bad-FCS frames** *(verified)*. `rtl8812au/os_dep/linux/usb_ops_linux.c:1108` frees every frame with `crc_err` or `icv_err` unless `mp_mode` is set, so monitor mode never sees them. Every frame that does arrive has `F_FCS` set and the real on-air FCS appended (`core/rtw_recv.c:3741`). As a result, the manager's `fcs_error_pkt` counts only corruption after the driver, and real on-air errors show up as missing frames. An optional driver patch is in §13.
4. **Short preamble cannot be requested** *(verified)*. The monitor TX path (`core/rtw_xmit.c:4320-4395`) reads only `RATE`, `TX_FLAGS`, `MCS` and `VHT` from radiotap. It ignores `FLAGS`, so `F_SHORTPRE` has no effect. The `*_S` modulations are rejected (§6.2).
5. **The driver keeps the host's sequence number** *(verified)*. `GetSequence(pwlanhdr)` is copied into `pattrib->seqnum`. Also, `TX_FLAGS & 0x08` (NOACK) clears `retry_ctrl`, as the plan assumed.

Also note:
- **Calibration.** `wfb_ng_power_cal.json` is gone, and its linear model was wrong (it put index 30 at 21 dBm, but it measures 3.9 dBm). It is replaced by a measured index → dBm table in `configuration/txpower.csv` (§7).
- **Bench config change.** `configuration/winject-tests/bw_a.cfg` sets `winject.radio_fcs = signal`. With this radio it must be `actual`, or left unset so the manager auto-detects it.

## 2. Process overview

One process drives one dongle:

```
winject-radio-realtek --config /etc/winject/radio-a.cfg
```

```
                        control thread (bfc epoll reactor)
 manager ──UDP console──▶ MplaneServer ─▶ mplane_commands ─▶ RealtekDevice/Radio/Test backends
                                                                 │
                                                       Nl80211 ◀─┤ Settings (slots)
                                                                 │
                                    publishes TxProfile / RxFilter (atomics)
                                                                 ▼
                        data thread (own epoll)
 manager ──UDP 9000────▶ Injector ── TxRing ──▶ PacketSocket (sendmsg: radiotap ‖ MPDU) ──▶ wlx…
 manager ◀─UDP 9210───── Forwarder ◀── RxParser ◀── PacketSocket (recvmmsg, radiotap)  ◀── wlx…
```

There are two threads, so a slow nl80211 call (a channel switch can take several ms) never stalls the data path. The data thread never takes a lock. It reads settings through atomics that the control thread publishes (§9).

## 3. Configuration

The file uses the manager's `key = value` format, and `#` starts a comment. The program fails at startup on an unknown key, a missing required key, or a value out of range.

| Key | Required | Default | Meaning |
|---|---|---|---|
| `radio.device` | one of three | | Interface name, e.g. `wlx00c0cabce06f` |
| `radio.usb_port` | one of three | | USB path below the root, e.g. `3-1.1`. Resolved through `/sys/bus/usb/devices/<port>:1.0/net/*` |
| `radio.mac` | one of three | | Permanent MAC. Matches `/sys/class/net/*/address` |
| `radio.driver` | no | `rtl88xxau_wfb` | Expected `ethtool -i` driver name (`/sys/class/net/<if>/device/driver` basename). Startup fails if it doesn't match |
| `radio.regdom` | no | `BO` | Two-letter regulatory domain, applied with `NL80211_CMD_REQ_SET_REG` |
| `radio.bandwidth` | no | `20` | `20` or `40`. 40 means HT40+ and the radiotap MCS `BW_40` flag |
| `radio.txpower` | yes | | Path to `txpower.csv` (§7). A relative path resolves against the config file's directory. Startup fails if the file is missing or invalid |
| `radio.rx_bpf` | no | `false` | Filter Addr3 in the kernel (§8.4). Lowers CPU use but loses the `dropped_filter_mismatched` count |
| `radio.tx_retry_us` | no | `50000` | How long a frame may wait for the driver to accept it before it counts as `dropped_wifi` (§8.2) |
| `net.bind` | no | `0.0.0.0` | Address for all three UDP sockets |
| `net.console_port` | no | `2201` | m-plane |
| `net.inject_port` | no | `9000` | d-plane inject |
| `net.forward_port` | no | `9210` | d-plane forward registration |
| `net.trusted_ipv4` | no | empty | If set, datagrams from other sources are dropped on all three ports |
| `state.dir` | yes | | Slot files. Created with mode 0700 if missing |
| `tune.tx_queue_sz` | no | `20` | TX ring size, 1–64 |
| `tune.rx_batch` | no | `16` | Frames per `recvmmsg`, 1–64 |
| `tune.sock_rcvbuf` | no | `4194304` | `SO_RCVBUF` on the packet socket |
| `log.level` | no | `info` | `error`, `warn`, `info`, `debug` |

Sample files go in `configuration/radio-a.cfg` and `radio-b.cfg` (§12).

## 4. Repository layout

The layout follows `winject-l3`'s conventions: PascalCase file names, Allman braces, and the `.clang-format` copied from `winject-l3`.

```
CMakeLists.txt                 top level: add_subdirectory(src/radio), option(WINJECT_BUILD_TESTS)
src/BfcFetch.cmake             copied from winject-l3 (same pinned bfc commit)
src/vendor/mplane/             vendored from winject-radio-esp32 (§5), untouched except the shim
src/vendor/mplane/config.h     shim: macros the vendored code needs, with Realtek values
src/vendor/radiotap/           radiotap.c, radiotap.h, radiotap_iter.h (ISC, from radiotap-library)
src/radio/
  Main.cpp                     args, signals, start/stop threads
  App.{h,cpp}                  owns everything below; wires control and data threads
  Config.{h,cpp}               §3
  DeviceSelector.{h,cpp}       device/usb_port/mac → ifname, driver check
  Nl80211.{h,cpp}              §6.1 (libnl-genl-3)
  NetLink.{h,cpp}              link up/down (SIOCSIFFLAGS), NetworkManager release
  Modulation.{h,cpp}           §6.2 table, channel/modulation validity
  PowerCal.{h,cpp}             §7
  Radiotap.{h,cpp}             TX header build, RX parse (§8.1, §8.3)
  TxProfile.h                  immutable radiotap template + its version
  PacketSocket.{h,cpp}         AF_PACKET socket, QDISC_BYPASS, BPF attach
  RxFilterBpf.{h,cpp}          §8.4
  Injector.{h,cpp}             §8.2
  Forwarder.{h,cpp}            §8.3
  DataPlane.{h,cpp}            data thread: epoll loop owning Injector and Forwarder
  Counters.h                   atomics for tx_info / rx_info (§10)
  Settings.{h,cpp}             slot files, current slot (§6.5)
  MplaneServer.{h,cpp}         UDP console: datagram → mplane_commands → reply datagrams (no test backend)
  RealtekBackends.{h,cpp}      mplane_device_backend / mplane_radio_backend
  Fcs.{h,cpp}                  CRC-32 (same polynomial as winject-l3 WifiFcs)
src/test/                      GoogleTest, one *Test.cpp per module (§11)
configuration/                 radio-a.cfg, radio-b.cfg, txpower.csv
scripts/
  winject-radio-realtek@.service
  70-winject-radio-realtek.rules   udev: NM_UNMANAGED for rtl88xxau_wfb
  bench_two_radios.sh          §12
  sync_mplane.sh               re-vendor the ESP32 m-plane at a given commit
docs/  plan.md, implementation.md, radio-realtek.md (user docs, P3)
```

Dependencies: `libnl-3-dev` (installed) and `libnl-genl-3-dev` (**not installed**; `sudo apt install libnl-genl-3-dev`). Also pthread, and GoogleTest via FetchContent. libpcap is not needed.

## 5. Vendored m-plane

Copy these files from `winject-radio-esp32/src/winject-esp32/` at a pinned commit, and record that commit in `src/vendor/mplane/VERSION`:

```
mplane/mplane_args.{h,cpp}   mplane/mplane_commands.{h,cpp}   mplane/mplane_reply.{h,cpp}
mplane/mplane_req_id.{h,cpp} mplane/mplane_backend.h          config_types.{h,cpp}
```

`mplane_backend.h` includes `frame.h` only for `WinjectMode`. The shim adds a 6-line `frame.h` that defines `enum class WinjectMode : uint8_t { winject, ota };`, so the vendored files stay unchanged.

The `config.h` shim supplies every macro the vendored files use:

| Macro | Realtek value | Note |
|---|---|---|
| `WIFI_CHANNEL_MIN` / `MAX` | `1` / `177` | Coarse parse range only. The backend checks the channel against the wiphy (§6.1) |
| `WIFI_TX_POWER_DBM_MIN` / `MAX` | `2` / `20` | Same as the manager's `winject.power` range. Widen both together later |
| `WIFI_DEFAULT_CHANNEL`, `WIFI_DEFAULT_TX_POWER_DBM`, `WIFI_DEFAULT_MODULATION` | `1`, `20`, `"OFDM_6M"` | Defaults when no slot is saved |
| `SETTINGS_SLOT_COUNT`, `SETTINGS_MODULATION_MAX` | `10`, `16` | |
| `WIFI_RADIO_INJECT_MIN` / `MAX` | `24` / `1472` | |
| `ETHER_TEST_MTU_MAX` | `1472` | Only used by `test_ether_*`, which is not implemented here |
| `WIFI_TX_QUEUE_DEFAULT` / `MAX`, `WIFI_RX_QUEUE_*`, `WIFI_*_RING_*`, `NETWORK_*` | ESP32 values | Only reached through `tune_*` / `network`, which reply `ENOTSUP` here |

`sync_mplane.sh <commit>` re-copies the files and runs the tests. The vendored code keeps its own snake_case style and is listed in `.clang-format-ignore`.

`MplaneServer` (ours) owns the UDP socket. For each datagram it truncates to 1499 bytes, NUL-terminates it, and checks `net.trusted_ipv4`. It then calls `mplane_commands::handle_text()` with a `mplane_reply` that appends to a 16 KiB buffer, and sends the buffer back to the source address in pieces of at most 16384 bytes. The `cmd:<u8>` prefix and `OK:<id>` decoration are handled inside the vendored `handle_line` / `mplane_req_id_reply`.

## 6. Control plane

### 6.1 Bring-up (`App::start`, in order)

| Step | How | On failure |
|---|---|---|
| 1. Load config | `Config::load` | exit 2 |
| 2. Resolve interface | `DeviceSelector`; checks the `radio.driver` basename | exit 3, naming the interfaces it saw |
| 3. Release from NetworkManager | The udev rule sets `NM_UNMANAGED=1` for the driver. As a fallback, run `nmcli device set <if> managed no` (best effort) | warn only |
| 4. Regulatory domain | `NL80211_CMD_REQ_SET_REG` with `NL80211_ATTR_REG_ALPHA2` | warn; channels limited to what the wiphy allows |
| 5. Monitor mode | Link down (`SIOCSIFFLAGS`), `NL80211_CMD_SET_INTERFACE` `NL80211_ATTR_IFTYPE=MONITOR`, link up, wait 300 ms, then confirm the type with `get_interface`. Up to 5 tries. `wpa_supplicant` can switch the type back after a successful set (§12) | exit 4 |
| 6. Read the channel list | `NL80211_CMD_GET_WIPHY` (split dump): every `NL80211_FREQUENCY_ATTR_FREQ` without `DISABLED` or `NO_IR` becomes a channel number | exit 4 if empty |
| 7. Load settings | `Settings::load_current()`, or defaults (§5) | defaults + warn |
| 8. Apply radio | `RealtekRadio::apply(full)`, see §6.3 | exit 5 |
| 9. Open sockets | packet socket (§8), UDP 9000, UDP 9210, console | exit 6 |
| 10. Start threads | data thread, then the control reactor `run()` | |

On `SIGTERM`/`SIGINT`, stop both loops, close the sockets, and leave the interface in monitor mode. Leaving it there avoids a `managed`↔`monitor` flap on every restart.

**Nl80211 class.** It owns one `nl_sock` with `genl_connect` and the resolved `nl80211` family id. Calls are synchronous with a 1 s receive timeout, and each method returns `int` (0 or `-errno`):

```cpp
class Nl80211
{
public:
    int open();
    int set_monitor(int ifindex);
    int set_channel(int ifindex, uint32_t freq_mhz, ChannelWidth width);  // HT20 | HT40PLUS
    int set_tx_power_fixed_mbm(int ifindex, int32_t mbm);                // §7: mbm = -idx*100
    int set_regdom(const char alpha2[2]);
    int get_channels(uint32_t wiphy, std::vector<ChannelInfo>* out);
    int get_interface(int ifindex, InterfaceInfo* out);                  // type, freq, txpower
};
```

`set_channel` uses `NL80211_CMD_SET_CHANNEL` (on the interface) with `NL80211_ATTR_WIPHY_FREQ` and `NL80211_ATTR_WIPHY_CHANNEL_TYPE` (`NL80211_CHAN_HT20` or `NL80211_CHAN_HT40PLUS`). `set_tx_power_fixed_mbm` uses `NL80211_CMD_SET_WIPHY` with `NL80211_ATTR_WIPHY_TX_POWER_SETTING = NL80211_TX_POWER_FIXED` and `NL80211_ATTR_WIPHY_TX_POWER_LEVEL = mbm`. These are the same calls `iw … set channel` and `iw … set txpower fixed` make.

### 6.2 Modulations

`Modulation.cpp` holds one table. Names match `winject-l3` `PhyAirtime` (`phy_canonical_name`) so the manager's pacing works unchanged.

| Name | Radiotap | Bands |
|---|---|---|
| `DSS_1M_L`, `DSS_2M_L` | `RATE` = 2, 4 (500 kbit/s units) | 2.4 GHz |
| `CCK_5M_L`, `CCK_11M_L` | `RATE` = 11, 22 | 2.4 GHz |
| `DSS_2M_S`, `CCK_5M_S`, `CCK_11M_S` | **rejected, `EINVAL`** (driver ignores `F_SHORTPRE`, §1) | |
| `OFDM_6M` … `OFDM_54M` | `RATE` = 12, 18, 24, 36, 48, 72, 96, 108 | both; not channel 14 |
| `OFDM_MCS0_LGI` … `OFDM_MCS7_LGI` | `MCS` known = `HAVE_MCS\|HAVE_BW\|HAVE_GI\|HAVE_FEC\|HAVE_STBC`, flags = `BW_40` if `radio.bandwidth=40`, never `FEC_LDPC` or STBC; index 0–7 | both; not channel 14 |
| `OFDM_MCS0_SGI` … `OFDM_MCS7_SGI` | same, plus flag `SGI` (0x04) | both; not channel 14 |

**Driver applies radiotap per frame** *(verified 2026-10-03, source and on air)*. `rtw_monitor_xmit_entry` (`core/rtw_xmit.c:4258`) copies `RATE`, `MCS` (index, and BW/SGI/FEC/STBC only when the matching `HAVE_*` bit is set in `known`), `VHT` and `TX_FLAGS` into `pkt_attrib`. It tags the frame `inject = 0xa5`, and `rtl8812au_xmit.c:117-155` then writes the descriptor with `USE_RATE=1`, `DISABLE_FB=1`, the rate, `DATA_SHORT` (SGI), LDPC, STBC and `DATA_BW`. No `iw … set bitrates` is needed. Bench result, dongle A injecting and B capturing on channel 6:

| Sent | Received |
|---|---|
| `RATE` 2 / 22 / 12 / 108 | 1 / 11 / 6 / 54 Mb/s |
| MCS0 LGI, MCS7 SGI (HT20) | `MCS 0 20 MHz long GI`, `MCS 7 20 MHz short GI` |
| MCS0 + `BW_40` on HT40+ | `MCS 0 40 MHz` |
| `TX_FLAGS` NOACK, unicast Addr1 | 1 copy per frame (no retries) |
| no NOACK, unicast Addr1 | about 6 retries per frame |
| MCS1 + STBC 1, MCS3 + LDPC (HT20 and HT40) | **nothing received** |

STBC and LDPC were rechecked with a third receiver, a TP-Link Archer T3U (RTL8812BU, in-kernel `rtw_8822bu`, advertises HT RX LDPC and RX STBC 1-stream), on channel 6 HT20 with 30 frames per case:

| Sent by | MCS3 + LDPC | MCS1 + STBC 1 | MCS4 + LDPC + STBC |
|---|---|---|---|
| 8812AU A → 8812AU B | 0 | 0 | 0 |
| 8812AU A → T3U | **30** | 0 | 0 |
| 8812AU B → 8812AU A | 0 | 0 | 0 |
| 8812AU B → T3U | **26** | 0 | 0 |

- **LDPC:** the 8812AU sends it correctly but cannot decode HT LDPC in monitor mode. Both ends of a winject link are 8812AU (or ESP32), so LDPC is unusable.
- **STBC:** no receiver decodes STBC frames from either 8812AU, so the 8812AU does not send usable STBC from this driver path. Its STBC receive could not be tested: injection through `rtw88` sends nothing.

LDPC and STBC are therefore always off, and there is no config option for them. The MCS header keeps `HAVE_FEC` and `HAVE_STBC` set with those flags clear, so the driver always sends BCC without STBC. Setting `radio.ldpc` or `radio.stbc` in a config fails at startup with `unknown key`.

`modulation_list()` returns the accepted names, separated by spaces. MCS 8–15 (2×2) and VHT are possible on the 8812AU but are left out until `PhyAirtime` knows them.

Validity is checked as a pair: the channel must be in the wiphy list from §6.1; DSS/CCK need 1–14; channel 14 allows only DSS/CCK; `radio.bandwidth=40` needs channel+4 in the list as well.

### 6.3 `radio_tx` (`RealtekRadio::set_radio(const radio_patch&)`)

1. Merge the patch into a copy of the current `radio_config`.
2. Validate: modulation known (§6.2), channel/modulation pair valid, `cca_enabled == true` (otherwise `invalid`).
3. Apply in this order. If a step fails, roll back the steps before it and return `io_error`:
   - channel, only if it changed (`Nl80211::set_channel`);
   - power, only if it changed (§7);
   - modulation: build a new `TxProfile` (§8.1) and publish it (§9). This step cannot fail.
4. Store the new `radio_config` as current. It is not persisted until `save`.

The vendored `print_radio` echoes the reply exactly as the ESP32 does: `OK radio_tx channel=<n> tx_power=<dBm> modulation=<NAME> cca=true`.

`radio_tx_info`: the same line without `OK`, followed by `radio_rx rssi=<dBm>` once any frame has passed the Addr3 filter. The RSSI is the last frame's `DBM_ANTSIGNAL` (first antenna field), stored in an `atomic<int16_t>` by the data thread, with `INT16_MIN` meaning none yet.

`radio_caps_info` → `fcs=ACTUAL` (`radio_caps{ fcs_mode::actual }`).

### 6.4 `rx_filter_addr3`

Pack the filter with `mac_filter_pack` and store it in `RxFilter` (`atomic<uint64_t>`). With `radio.rx_bpf=true`, also rebuild and re-attach the kernel filter (§8.4).

### 6.5 Device commands

| Command | Behaviour |
|---|---|
| `ping`, `help` | Handled by the vendored code |
| `save <n>` | Write `state.dir/slot<n>` (§6.6) with the current radio and filter, then make it current |
| `load <n>` | Read the slot (`ENOENT` if missing, `EIO` if it doesn't parse), apply the radio settings (§6.3) and the filter, make it current |
| `reset` | Reply `OK`, wait 200 ms for the reply to flush, then `execv("/proc/self/exe", argv)`. The new process reruns bring-up and applies the current slot, which matches the ESP32's reboot behaviour. All fds are close-on-exec, so the new image starts with only stdin/stdout/stderr. Clients confirm a lost reply via `ts` in `tx_info` ([mplane.md](mplane.md#device)) |
| `reset mode=OTA` | `EINVAL`; there is no OTA mode. `mode=WINJECT` is accepted and does nothing |
| `test_wifi_*`, `test_ether_*` | `NOK ENODEV`; not implemented (§10) |
| `network`, `tune_param`, `tune_tx_param`, `tune_rx_param` | `NOK ENOTSUP` (`mplane_status::unsupported`). The host OS owns networking, and sizing comes from the config file |

`uptime_us()` returns `CLOCK_MONOTONIC` minus the start time.

### 6.6 Slot file format

Plain text, one `key=value` per line, written with tmp + `fsync` + `rename`:

```
version=1
channel=6
tx_power=15
modulation=OFDM_24M
cca=true
rx_filter_addr3=ca:fe:ba:be:04:d2
```

`state.dir/current` holds the slot number. Unknown keys are ignored and a missing key falls back to its default, so the format can grow.

## 7. TX power

The m-plane value is in dBm (2–20). The 8812AU with `rtl88xxau_wfb` is driven by a power index (0–63, `rtw_tx_pwr_idx_override`). A measured table maps one to the other.

**`txpower.csv`.** This is an external file with one row per index and the measured output power in dBm. There is no built-in table, and `radio.txpower` is required:

```
idx,tx_power
0,12.90
1,-11.50
2,-11.00
…
63,18.80
```

- A non-numeric first line is a header. Blank lines and lines starting with `#` are skipped.
- Row `0` is kept for reference and ignored, because index 0 means "no override" and gives the driver's default power (12.9 dBm).
- Rows may be sparse, but at least 2 rows with index 1–63 are needed. A bad number, an index outside 0–63, or a duplicate index fails startup and names the file and line.

The shipped table ranges from −11.5 dBm (index 1) to 18.8 dBm (index 63). It is **not monotonic**: index 21 is −0.6 dBm, but 22–24 are −0.9, and 40 is lower than 38–39.

**Mapping.** Pick the index 1–63 whose measured `tx_power` is closest to the requested dBm. Ties go to the lower index. This handles the non-monotonic rows and never chooses index 0. With the shipped table, 2 dBm → index 29 (1.5 dBm), 10 → 42 (10.8), 17 → 58 (17.0), and 20 → 63 (18.8). The last one is reported as clamped, which means more than 0.5 dB outside the measured range. As on the ESP32 at 64-QAM, `radio_tx` reports the requested dBm, not the measured value. Each change is logged at `info` as `tx_power <n> dBm -> idx <i> (measured <x> dBm)`, and clamping is logged once.

**Apply.** Call `Nl80211::set_tx_power_fixed_mbm(ifindex, -idx * 100)`. This is svpcom's driver hook (`ioctl_cfg80211.c:3692` sets `rtw_tx_pwr_idx_override = -value`). Writing only to sysfs does not change the output. After applying, read back with `get_interface`: the driver should report `txpower -<idx>.00 dBm`. If it doesn't, return `io_error`. This check also catches the stock `88XXau` driver, which ignores the call *(bench)*.

**Bench results** *(verified 2026-10-03, source and on air, both dongles as transmitter)*. This is the median RSSI at the other dongle, 40 frames per case, OFDM 6M on channel 6:

| Setting | A → B | B → A |
|---|---|---|
| `txpower fixed` index 5 / 15 / 25 / 35 | −26 / −22 / −16 / −12 dBm | −26 / −22 / −16 / −14 dBm |
| `txpower fixed 0` (index 0) | **−2 dBm** | **−2 dBm** |
| Radiotap `DBM_TX_POWER` 0 vs 30 dBm, at index 15 | −22 / −22 | −22 / −22 |
| Transmitter at index 5, then the *other* dongle set to 35 | −26 (unchanged) | −26, then **−14 after the transmitter re-tunes** |

What this means:
1. **Power index control works.** It gives about 0.4–0.6 dB per index step at this level.
2. **Radiotap `DBM_TX_POWER` is ignored.** `rtw_monitor_xmit_entry` never reads it. Per-frame power is not possible, so power stays a per-device nl80211 setting.
3. **Index 0 means "no override"** (`get_overridden_tx_power_index` in `include/drv_types.h:445`), which falls back to the driver's default power, about 10 dB above index 35. A computed index of 0 must never be sent. Clamp to 1 instead (§7 mapping).
4. **The override is global across all dongles.** `rtw_tx_pwr_idx_override` is one module-wide variable (`os_intfs.c:587`), and `CurrentTxPwrIdx` is unused on the 8812A. Setting power on one dongle takes effect on another the next time that dongle recomputes its power, for example on a channel change. Both dongles also report the last value written, so the read-back check in this section cannot detect the problem. Until the driver is patched to keep the override per adapter, every radio on one host must use the same `tx_power`, or run on separate hosts.

Note: both `88XXau` and `88XXau_wfb` are loaded on this host today. The driver check in step 2 of §6.1 makes sure the interface is bound to the `_wfb` module.

## 8. Data plane

### 8.1 Radiotap TX headers

`Radiotap::build_tx(const ModulationEntry&, const RadioOptions&) → TxProfile`. There are two templates, both little-endian.

Legacy rate, 12 bytes:

```
00 00 | 0c 00 | 04 80 00 00 | <rate> | 00 | 08 00
ver pad  len    present: RATE(2) | TX_FLAGS(15)   rate   pad   TX_FLAGS = F_TX_NOACK
```

The pad byte aligns `TX_FLAGS` to 2 bytes.

HT MCS, 13 bytes (the same as wfb-ng's `radiotap_header_ht`):

```
00 00 | 0d 00 | 00 80 08 00 | 08 00 | <known> <flags> <mcs>
               present: TX_FLAGS(15) | MCS(19)   NOACK
```

`TxProfile` is immutable: `uint8_t bytes[16]; uint8_t len; uint32_t version;`.

### 8.2 Inject (`Injector`, data thread)

Sockets:
- UDP: bound to `net.bind:net.inject_port`, `SO_RCVBUF` 1 MiB, non-blocking.
- `AF_PACKET`/`SOCK_RAW`/`SOCK_CLOEXEC`/`htons(ETH_P_ALL)`: bound to the ifindex, `PACKET_QDISC_BYPASS=1`, non-blocking. One socket serves both TX and RX; see §8.3.

Loop, run when the UDP socket is readable and the ring has room:

```
n = recvmmsg(udp, batch of tune.rx_batch, MSG_DONTWAIT), each buffer 1500 bytes, flags MSG_TRUNC
for each datagram:
    tx.ether_pkt++
    if trusted_ipv4 set and src != trusted: tx.dropped_invalid_frame++ ; continue
    if len < 24 or len > 1472 or truncated: tx.dropped_invalid_frame++ ; continue
    if ring full: tx.dropped_tx_queue++ ; continue
    ring.push({buf, len, t_enqueue = now})
drain_ring()
```

`drain_ring()` sends frames to the driver:

```
profile = tx_profile.load(acquire)
while ring not empty:
    f = ring.front()
    sendmsg(pkt, iov = [profile.bytes,profile.len] + [f.buf,f.len])
    ok                 → tx.air_pkt++, ring.pop()
    ENOBUFS / EAGAIN   → if now - f.t_enqueue > tx_retry_us: tx.dropped_wifi++, ring.pop(), continue
                         else arm a 200 µs timerfd and return   (driver is busy)
    other errno        → tx.dropped_wifi++, ring.pop(), log rate-limited
```

- **No copy.** The radiotap header and the MPDU go to the kernel as two iovecs. Ring buffers come from a fixed pool of `tune.tx_queue_sz` × 1500 bytes, allocated once.
- **Backpressure.** By default the driver silently drops frames when it runs out of xmit frames, and `sendmsg` still succeeds (`rtw_xmit.c`: `tx_drop++`, returns `NETDEV_TX_OK`). Loading the module with `MaxTxBufLen=<n>` makes it return `NETDEV_TX_BUSY` instead. Through `PACKET_QDISC_BYPASS` that becomes `ENOBUFS`, which the ring above handles. Recommended: `options 88XXau_wfb MaxTxBufLen=32` in `/etc/modprobe.d/winject.conf` *(bench: confirm the errno and find the best value)*. Without it, `air_pkt` counts frames given to the driver, and the driver's own `tx_drop` is lost.
- `air_pkt` means "accepted by the driver", not "confirmed on air". This driver has no TX status in monitor mode. `in_flight` is always 0.

### 8.3 Receive and forward (`Forwarder`, data thread)

The packet socket uses `SO_RCVBUF = tune.sock_rcvbuf`, with `PACKET_STATISTICS` read every second to update counters.

```
n = recvmmsg(pkt, batch, MSG_DONTWAIT), buffers 2048 bytes
for each frame:
    rx.air_pkt++
    p = Radiotap::parse_rx(buf, len)          // radiotap-library iterator
    if !p.ok:                     rx.dropped_filter_mismatched++ ; continue
    if p.has_tx_flags:            rx.dropped_filter_mismatched++ ; continue   // own frame looped back
    mpdu = buf + p.rt_len ; mlen = len - p.rt_len
    if p.flags & F_FCS: (trailer already present)  else: append crc32(mpdu) LE ; mlen += 4
    if mlen < 28 or mlen > 1504:  rx.dropped_filter_mismatched++ ; continue
    if !rx_filter.matches(mpdu + 16): rx.dropped_filter_mismatched++ ; continue
    last_rssi = p.dbm_antsignal
    if no peer:                   rx.dropped_no_peer++ ; continue
    queue for sendmmsg to peer
sendmmsg(fwd_udp, queued)  → ether_pkt += sent, dropped_send_failed += failed
```

- Frames with `F_BADFCS` are forwarded unchanged; the manager's CRC check counts them. Today the driver drops them before monitor mode (§1), so they appear only with the §13 patch.
- **Peer registration.** Any datagram arriving on `net.forward_port` (from `net.trusted_ipv4` when set) makes its source address the peer. The manager sends a 1-byte datagram from its inject socket about once per second (`RadioManager::k_register_interval_ticks`). The most recent sender wins, as on the ESP32.
- `rx.dropped_rx_queue` = the increase in `tp_drops` from `PACKET_STATISTICS`, i.e. frames the kernel dropped because the socket buffer was full.
- The radiotap parser returns `{rt_len, flags, has_tx_flags, dbm_antsignal, rate/mcs}`. It must handle extended present bitmaps and per-field alignment. The driver emits `EXT` and vendor namespaces (`rtw_recv.c:3681,3712`), so a hand-written fixed-offset parser would break.

### 8.4 Optional kernel Addr3 filter (`RxFilterBpf`)

With `radio.rx_bpf=true`, attach a classic BPF program (`SO_ATTACH_FILTER`) that keeps a frame only if Addr3 equals the filter. The radiotap length is little-endian, so it is assembled from two bytes:

```
ldb [3]        ; A = rt_len high byte
lsh #8
tax
ldb [2]
or x           ; A = rt_len
tax            ; X = rt_len
ld  [x+16]     ; addr3 bytes 0-3 (big-endian load)
jne #<a0a1a2a3>, drop
ldh [x+20]     ; addr3 bytes 4-5
jne #<a4a5>, drop
ret #65535
drop: ret #0
```

An empty filter detaches the program (`SO_DETACH_FILTER`). In this mode `dropped_filter_mismatched` counts only frames the program kept, and `rx_info air_pkt` stops matching the ESP32's meaning; `help` notes this. To swap the program without a gap, attach the new one with `SO_ATTACH_FILTER`, which replaces the old one atomically.

## 9. Sharing state between threads

| Data | Writer | Reader | Mechanism |
|---|---|---|---|
| `TxProfile` | control | data | Two slots and `atomic<uint8_t> active`. The writer fills the inactive slot, then does a release-store of its index. Only one writer exists, and a reader copies the 16 bytes before use, so no ABA problem |
| Addr3 filter | control | data | `atomic<uint64_t>` (`mac_filter_pack`) |
| Counters | data | control | `atomic<uint32_t>` relaxed, wrapping like the ESP32's `u32` |
| Last RSSI | data | control | `atomic<int16_t>` |
| Forward peer | data | data | plain (data thread only) |
| Shutdown | main | both | `eventfd` registered in both epoll loops |

## 10. Counters → `tx_info` / `rx_info`

| Field | Source |
|---|---|
| `tx_queue_sz` | current ring occupancy |
| `in_flight` | `0` (no TX status from the driver) |
| `dropped_invalid_frame` | §8.2 length / truncation / untrusted source |
| `dropped_tx_queue` | ring full |
| `dropped_wifi` | `sendmsg` error, or retry budget exceeded |
| `ether_pkt` | datagrams read from UDP 9000 |
| `air_pkt` | `sendmsg` accepted |
| `rx_queue_sz` | `0` |
| `dropped_filter_mismatched` | §8.3 (bad radiotap, own frames, length, Addr3) |
| `dropped_rx_queue` | `PACKET_STATISTICS.tp_drops` |
| `dropped_no_peer`, `dropped_send_failed`, `ether_pkt` | §8.3 |
| `air_pkt` (rx) | frames read from the packet socket |
| `ts` | `uptime_us()` |

The accounting identities in `winject-radio-esp32/docs/winject.md` ("Frame accounting") still hold. On TX, `in_flight` = 0 and frames still in the ring count as `tx_queue_sz`.

**Test commands are not implemented.** `test_wifi_*` and `test_ether_*` exist on the ESP32 to measure Wi-Fi/EMAC DMA contention, which this radio does not have. `MplaneServer` passes no test backend to the vendored `mplane_commands`, so they reply `NOK ENODEV`. `help` still lists them because the vendored code is unchanged. The manager does not send them.

## 11. Tests (`src/test`, target `winject-radio-realtek-tests`)

| Test file | Covers |
|---|---|
| `ConfigTest.cpp` | Each key: defaults, ranges, unknown key, exactly one selector |
| `DeviceSelectorTest.cpp` | Resolving through a fake sysfs tree in a temp dir (the selector takes a sysfs root path) |
| `ModulationTest.cpp` | Every name → radiotap bytes; `_S` rejected; channel/modulation matrix incl. ch 14 and 5 GHz; HT40 pair |
| `RadiotapTest.cpp` | TX header bytes match wfb-ng's `radiotap_header_ht` for MCS; RX parse of captured driver headers (EXT + vendor namespace, `F_FCS`, `F_BADFCS`, `TX_FLAGS`) |
| `PowerCalTest.cpp` | Shipped `txpower.csv` loads all 63 indices; nearest match incl. non-monotonic rows and ties; index 0 never chosen; clamping; CSV header/comments/sparse rows; bad index, bad value, duplicate, too few rows, missing file |
| `RxFilterBpfTest.cpp` | Run the generated program with a small cBPF interpreter in the test on frames with different `rt_len` |
| `FcsTest.cpp` | Same vectors as `winject-l3/src/test/WifiFcsTest.cpp` |
| `SettingsTest.cpp` | save/load round trip, missing slot → `not_found`, corrupt → `io_error` |
| `MplaneRealtekTest.cpp` | The vendored `mplane_commands` with `RealtekBackends` on fake `Nl80211` and `DataPlane` interfaces: the manager's exact startup sequence (`cmd:1 radio_caps_info` … `cmd:4 save 0`) and expected replies |
| `InjectorTest.cpp` | Injector on a socketpair and a fake packet sink: counters, ring full, ENOBUFS retry and expiry |
| `ForwarderTest.cpp` | Captured frames in, `MPDU‖FCS` out; peer registration; trusted source |

The vendored m-plane's own ESP32 tests (`mplane_args_test.cpp`, `mplane_commands_test.cpp`) are copied too and run against the shim.

`Nl80211`, `PacketSocket` and the sysfs root are behind small interfaces, so every test runs without root or hardware.

## Device loss and recovery

The radio identifies its dongle by the interface name from config (`DeviceSelector::resolve`) and the ifindex at bring-up. After that, `if_nametoindex(ifname)` is compared to the stored ifindex: zero means unplugged, a different non-zero value means replugged (same `wlx…` name, new netdev). Recovery also requires the configured driver (`rtl88xxau_wfb` by default); a dongle on stock `88XXau` is ignored until the right driver binds.

**While running**, `DeviceWatch` listens for rtnetlink link events and runs the same check every 1 s. When the device has been back on the right driver with a stable ifindex for 1 s, the process `execv`s itself (same path as m-plane `reset`). m-plane keeps answering while the dongle is gone; `radio_tx` returns `io_error`.

**At startup**, if the device is missing, the process waits forever (rtnetlink + 1 s scan, same debounce as recovery) instead of exiting. m-plane is not up until bring-up completes. Config errors still exit 1 immediately. If the device disappears during bring-up after the wait, the process restarts and waits again.

Use a `wlx<mac>` selector (or `usb_port` / `mac` that resolves to one): plain `wlan0` names can change on replug. Recovery after replug adds about 1–2 s (debounce) on top of USB enumeration.

## 12. Bench procedure (P1 exit test)

Hardware: Orange Pi 5, two RTL8812AU dongles, `wlx00c0cabce06f` and `wlx00c0cabce072`, attenuated RF path (43 dB pad).

| | radio-a | radio-b |
|---|---|---|
| selector | `radio.device = wlx00c0cabce06f` | `radio.device = wlx00c0cabce072` |
| `net.bind` | `127.0.0.1` | `127.0.0.1` |
| console / inject / forward | 2201 / 9000 / 9210 | 2202 / 9003 / 9213 |

Manager configs: copies of `bw_a.cfg` / `bw_b.cfg` with `winject.device = 127.0.0.1`, `winject.console = 2201|2202`, the matching `winject.inject_port` / `winject.forward_port`, `winject.radio_fcs = actual`, and `winject.local_ip` removed. Radio-b avoids 9001 and 9002 because `bw_test.py` listens on `127.0.0.1:9001` (B→A) and `127.0.0.1:9002` (A→B), which are the upstream addresses in `bw_a.cfg` / `bw_b.cfg`. The manager configs also need `manager.console_in` / `console_out` (2400/2401 and 2410/2411) so that `tools/bw_test.py` can drive them.

The radios are selected by interface name, not by USB port, because the `wlx…` name follows the MAC and survives replugging into another port. `DeviceWatch` recovery relies on the same stable name.

`scripts/bench_two_radios.sh` runs the whole test:
1. Starts both radios with `sudo`.
2. Starts one `winject-manager` per radio with `configuration/manager-{a,b}.cfg` (built through winject-l3's `ensure_manager.sh`).
3. Runs `tools/bw_test.py --udp --drop-stages`, passing on any extra arguments.
4. On exit, stops everything and returns both dongles to NetworkManager in managed mode.

```bash
./scripts/bench_two_radios.sh                                    # channel 1, OFDM_24M
./scripts/bench_two_radios.sh --channel 13 --modulation OFDM_24M,OFDM_54M,OFDM_MCS7_SGI
./scripts/bench_two_radios.sh --modulation OFDM_MCS7_SGI --kbps 40000 --test-ab
```

`WINJECT_L3` points at the winject-l3 checkout (default `../winject-l3`), and logs go to `LOG_DIR` (default `$TMPDIR/winject-bench-<pid>`). `manager_bw_test.sh` can't be used here: it keeps the console port and data ports from its own configs, but two radios on one host need different ones.

Exit criteria:
1. Both managers log `radio programmed`.
2. Traffic flows both ways at `OFDM_24M`, channel 6.
3. The accounting identities hold within one second's worth of frames.
4. `fcs_error_pkt` and `fcs_unknown_pkt` stay at 0.

**First run** *(2026-10-03, `winject-l3` 982348d, `tools/bw_test.py --udp`, OFDM_24M, 1400-byte payload, 16.5 Mbit/s offered, 5 s per direction)*:

| Channel | `winject.power` | RX level | A→B | B→A |
|---|---|---|---|---|
| 1 | 20 (idx 63) | +4 / +8 dBm | 15.6 Mbit/s, 5.3 % loss | 15.1 Mbit/s, 8.3 % loss |
| 1 | 2 (idx 29) | −16 dBm | 15.5 Mbit/s, 6.0 % loss | 14.9 Mbit/s, 10.0 % loss |
| 13 | 2 (idx 29) | | 16.2 Mbit/s, 2.0 % loss | 15.6 Mbit/s, 5.5 % loss |

**Rate sweep** *(channel 13, `winject.power = 2`, `winject.max_rate_kbps = 80000`, offer from `bw_test.py`'s `auto_offer_kbps`)*:

| Modulation | Offer | A→B | B→A |
|---|---|---|---|
| OFDM_24M | 16.5 | 16.2 (2.0 %) | 15.6 (5.5 %) |
| OFDM_36M | 21.7 | 19.5 (10.1 %) | 20.6 (5.2 %) |
| OFDM_48M | 25.7 | 23.6 (8.2 %) | 24.6 (4.3 %) |
| OFDM_54M | 27.4 | 25.9 (5.2 %) | 26.2 (4.2 %) |
| OFDM_MCS5_LGI | 26.8 | 24.6 (8.1 %) | 25.4 (5.2 %) |
| OFDM_MCS6_LGI | 28.5 | 26.2 (8.3 %) | 27.3 (4.4 %) |
| OFDM_MCS7_LGI | 30.1 | 27.2 (9.6 %) | 28.2 (6.2 %) |
| OFDM_MCS7_SGI | 31.6 | 28.7 (9.0 %) | 29.5 (6.5 %) |
| OFDM_MCS7_SGI, `--kbps 40000` | 40.0 | 36.7 (8.2 %) | |

Rates are in Mbit/s, with loss in brackets. Throughput follows the offer, and the radios add no drops of their own: on the 40 Mbit/s run, TX `ether_pkt` = `air_pkt` with zero `dropped_tx_queue`/`dropped_wifi`, and RX `dropped_rx_queue` = 0. `bw_a.cfg`/`bw_b.cfg` cap the manager at `max_rate_kbps = 20000`, which must be raised for anything above OFDM_24M. OFDM_24M itself tops out at about 17.9 Mbit/s of payload for 1400-byte frames (626 µs airtime).

- Both managers log `radio programmed`. Every stage except air shows zero drops, and both radio residual checks pass (criteria 1–3). The manager logs show no errors or warnings.
- All the loss is on air. It does not change with power, so it is not receiver overload. It is lower on channel 13 than on channel 1, where other networks were seen at −72 dBm. Compare against the ESP32 on the same channel before reading more into it.
- About 90 % of each radio's own transmissions loop back into its RX path. They are dropped as `dropped_filter_mismatched`, which matches the §14 risk.
- **Monitor mode race.** If the dongle was managed by NetworkManager, `wpa_supplicant` detaches asynchronously after `nmcli … managed no` and can switch the interface back to managed after `set_monitor` returned 0. Bring-up therefore checks `iftype` with `get_interface` after a 300 ms settle, retrying up to 5 times. Both radios needed a second attempt on this run. The udev rule (`NM_UNMANAGED=1`, P3) avoids the race altogether.

## 13. Phases and exit criteria

| Phase | Work | Done when |
|---|---|---|
| **P0** bring-up | Repo skeleton, CMake, vendored m-plane + shim, `Config`, `DeviceSelector`, `Nl80211` (monitor, channel, regdom), `MplaneServer` with `ping`, `help`, `radio_caps_info`, `radio_tx_info` | `mp 127.0.0.1:2201 radio_caps_info` → `OK radio_caps_info fcs=ACTUAL`; `iw dev` shows monitor on the configured channel; unit tests pass |
| **P1** data path | `Radiotap`, `PacketSocket`, `Injector`, `Forwarder`, `rx_filter_addr3`, `Fcs`, counters, `tx_info`/`rx_info` | §12 exit criteria |
| **P2** settings | Full `radio_tx` incl. power (§7) and rollback, `save`/`load`, `reset`, `MaxTxBufLen` backpressure | Manager restart re-programs from scratch; reset confirm via `ts`; power steps visible on `wfb_ng_power_test` (radio-b as receiver) |
| **P3** ops | systemd template unit (`AmbientCapabilities=CAP_NET_RAW CAP_NET_ADMIN`, `User=winject`, `StateDirectory=winject-radio-realtek/%i`), udev rule, `docs/radio-realtek.md`, README | `systemctl start winject-radio-realtek@a` works after a reboot with no manual steps |
| **P4** manager (`winject-l3`) | `Config.cpp:128` channel check → a list incl. 36–165; `modulation_ok_for_channel` rejects DSS/CCK above 14; `PhyAirtime` for 5 GHz; optional `winject.bandwidth`; document `winject.tx_burst_size` for this radio | Manager tests pass; a 5 GHz run on the bench |
| **P4b** driver (optional) | Patch `usb_ops_linux.c:1108` to keep `crc_err` frames when the interface is in monitor mode (they already get `F_BADFCS`) | `fcs_error_pkt` rises when the attenuator is turned up |

## 14. Risks and open points

| Item | Risk | Plan |
|---|---|---|
| ENOBUFS backpressure with `MaxTxBufLen` | Not measured; another errno or a silent drop would break §8.2's retry | Measure in P2; fall back to pacing with `winject.tx_burst_size` |
| Injected frames looping back to RX | wfb-ng sees them (marked with `TX_FLAGS`); dropped in §8.3, but they cost CPU | Count in `dropped_filter_mismatched`; check the rate on the bench |
| `88XXau` vs `88XXau_wfb` both loaded | A dongle may bind to the stock driver after replug | Driver check at startup and on recovery; blacklist `88XXau` in `/etc/modprobe.d` |
| m-plane power range 2–20 dBm vs measured −11.5 to 18.8 dBm | 19–20 dBm clamp to index 63 (18.8 dBm), and −11.5 to 2 dBm cannot be requested | Clamping is logged, and the reported value is the requested one, as on the ESP32. Widen `WIFI_TX_POWER_DBM_MIN` together with the manager's `winject.power` range if lower power is needed |
| TX power override is driver-global | Two radios on one host overwrite each other's power (§7) | Same `tx_power` on all radios per host, or patch the driver for a per-adapter override (P4b) |
| `cca=false` from an existing manager config | Manager fails to program the radio | Remove `winject.cca` from configs used with this radio |
| HT40 | Works on air (§6.2), but untested with the manager's airtime model | Keep `radio.bandwidth = 20` until P4 |
| STBC / LDPC | STBC TX does not work; LDPC TX works but the 8812AU cannot receive it (§6.2) | Disabled permanently, with no config option |
| One `txpower.csv` for all dongles | Other channels, bands and individual dongles may differ | Measure per dongle or band if needed, and point that radio's `radio.txpower` at its own CSV |
