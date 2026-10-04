# Plan: winject-radio-realtek

## Overview

The new radio is a Linux program, `winject-radio-realtek`, that drives one RTL8812AU dongle. To the manager it looks exactly like the ESP32 radio: the same UDP console (the m-plane in `winject-radio-esp32/docs/mplane.md`) and the same data ports (inject on 9000, forward on 9210).

The manager already lets you set the address and all three ports (`winject.device`, `winject.console`, `winject.inject_port`, `winject.forward_port` in `Config.cpp:115-263`). So phases 0–3 need no manager changes. You'd run one copy per dongle, either on the same host as the manager (`device = 127.0.0.1`) or on a separate board.

```
manager ──UDP console──▶ m-plane ──▶ WifiDevice (nl80211: monitor mode, channel, txpower)
        ──UDP 9000────▶ Injector  ──▶ AF_PACKET + radiotap ──▶ wlx… (rtl88xxau_wfb)
        ◀─UDP 9210───── Capture   ◀── AF_PACKET + BPF filter + radiotap parse
```

## Radio config (one file per dongle)

Same `key = value` format as the manager config:

```ini
# Which dongle to open: set exactly one of these
radio.device       = wlx00c0cabce06f      # interface name (the wlx… name comes from the MAC)
# radio.usb_port   = 3-1.1                # physical port, matched like iface_for_port in wfb_ng_bench.sh
# radio.mac        = 00:c0:ca:bc:e0:6f
radio.driver       = rtl88xxau_wfb        # refuse to start on 88XXau, rtw88, etc.
radio.regdom       = BO
radio.bandwidth    = 20                   # 20 | 40 (HT40+)
radio.power_cal    = /etc/winject/power_cal.json   # same schema as wfb_ng_power_cal.json

net.bind           = 0.0.0.0              # 127.0.0.1 when the manager is on the same host
net.console_port   = 2201                 # not 22: sshd already uses it on a Linux host
net.inject_port    = 9000
net.forward_port   = 9210
net.trusted_ipv4   = 192.168.253.10       # empty = accept any source

state.dir          = /var/lib/winject-radio-realtek/wlx00c0cabce06f   # holds save/load slots
```

On startup the program takes over the bring-up steps that `wfb_ng_bench.sh` does now:
- find the interface;
- check the driver;
- tell NetworkManager to stop managing it;
- set monitor mode, bring it up, set the channel, and apply the TX power index.

## How m-plane commands map onto the Realtek driver

| Command | Implementation |
|---|---|
| `ping`, `help`, `cmd:<u8>` replies | Write the parser to match `mplane.md`. `mplane.md` describes the ESP32 repo's `mplane/` split, but that code isn't in the tree. |
| `radio_tx channel=` | nl80211 set channel (HT20 or HT40+). Phase 0 can simply run `iw`. |
| `radio_tx modulation=` | Set per frame in the radiotap header, not on the device: `RATE` for DSS/CCK/OFDM, the `MCS` field (plus SGI) for `OFDM_MCSn_*`. DSS/CCK are rejected on 5 GHz. |
| `radio_tx tx_power=` (dBm) | Converted to a power index using the calibration file (index 30 ≈ 21 dBm, 0.5 dB per step). Applied with `iw … txpower fixed -idx*100`, the svpcom driver workaround; writing only to sysfs doesn't change the actual output. |
| `radio_tx cca=` | The Realtek driver has no control for this. Proposal: accept only `true`, and reject `false` with `EINVAL`. |
| `radio_tx_info` | Current settings plus `radio_rx rssi=` taken from the radiotap signal strength of the last frame that passed the filter. |
| `rx_filter_addr3` | Rebuilds the kernel BPF filter (`wlan addr3 ca:fe:ba:be:hh:ll`), so frames from other domains never reach the program. |
| `save` / `load` | Slot files in `state.dir`. |
| `reset` | Reply `OK`, then re-initialise the interface or `exec` itself again. Clients confirm a lost reply via `ts` in `tx_info` (see [aidocs/remove-restart-id.md](../aidocs/remove-restart-id.md)). |
| `tx_info` / `rx_info` | Bytes queued on the TX socket (`SIOCOUTQ`) and on the RX socket (`SIOCINQ`). |
| `network`, `tune_*` | `ENOSYS`, since the host OS owns networking. Alternatively, map `tune_*` onto socket buffer sizes. |
| `test_wifi_tx`/`rx`(`_stat`) | Phase 3, using the same inject and capture paths. |

## Data path details to handle

1. **Disable ACKs on TX.** Address 1 carries packed slot lengths, so it can look like a unicast address. The driver would then wait for ACKs and retry, which hurts throughput. Every radiotap header must set `TX_FLAGS = NOACK`, as wfb-ng's `init_radiotap_header` does. Use `PACKET_QDISC_BYPASS`, with one `sendmsg` per MPDU: the radiotap header and the manager's MPDU go in as two buffers, so nothing is copied.
2. **FCS on RX.** The manager expects each forwarded frame to be the MPDU followed by a 4-byte little-endian FCS, and it checks that FCS (`WifiUdp.cpp:157`).
   - If radiotap reports `F_FCS`, forward the bytes as they arrive.
   - Otherwise, compute the CRC-32 and append it.

   Still unverified: whether `88XXau_wfb` passes bad-FCS frames up at all. If it drops them, the manager's `fcs_error_pkt` count will read low.
3. **Dropping our own frames.** Monitor mode usually doesn't loop injected frames back, but confirm this on the bench.

## Proposed repo layout

This copies the `winject-l3` style: Google C++ with Allman braces, the bfc epoll reactor pulled in via `BfcFetch.cmake`, and GoogleTest.

```
src/radio/   Main, Config, Mplane{Parse,Commands}, WifiDevice (nl80211/sysfs),
             Radiotap (build/parse), Injector, Capture (+BPF), Dplane, Settings, PowerCal, Fcs
src/test/    parser, radiotap round-trip, power cal, config/device-selector, FCS
configuration/  radio-a.cfg, radio-b.cfg, power_cal.json
scripts/     winject-radio-realtek@.service (CAP_NET_RAW, CAP_NET_ADMIN), bench helpers
docs/        radio-realtek.md
```

## Phases

1. **P0, bring-up:** config file, choosing the dongle, monitor setup, and m-plane `ping`/`help`/`radio_tx_info`.
2. **P1, data path:** inject and forward with FCS, `rx_filter_addr3`. Test with both local dongles: two radio instances (consoles 2201 and 2202, separate inject/forward ports) and two managers using `configuration/winject-tests/bw_a.cfg` and `bw_b.cfg`, plus `manager_bw_test.sh`.
3. **P2, radio settings:** full `radio_tx`, `save`/`load`, and `reset`. Compare power settings with the `wfb_ng_power_test` TUI.
4. **P3:** test commands, `tx_info`/`rx_info`, systemd unit, docs.
5. **P4, manager changes in `../winject-l3`:**
   - **Channels:** allow 5 GHz channels. The config parser currently rejects anything outside 1–14 (`Config.cpp:128`), and the DSSS check in `modulation_ok_for_channel` must also cover 5 GHz.
   - **Width:** optional HT40.
   - **TX queue depth:** `k_radio_tx_queue_depth = 20` matches the ESP32 firmware; the Realtek radio should probably set `winject.tx_burst_size` per radio instead.

## Decisions for you

1. **Language:** I recommend C++, to share the reactor, style and tests with `winject-l3`. Python with scapy would be quicker to prototype but too slow for this injection rate.
2. **Where it runs:** on the same host as the manager over loopback, or on a separate board over Ethernet like the ESP32? I'd support both through `net.bind`, and test the same-host setup first.
3. **`cca=false`:** reject it, or accept it and ignore it?
4. **Calibration:** the current file is from channel 6 on one dongle. Should we keep one calibration per dongle, or one shared table?
