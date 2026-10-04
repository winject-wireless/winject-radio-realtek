# Plan: USB unplug/replug recovery

## Goal

1. When the dongle is unplugged, the radio notices, logs it and waits.
2. When the same dongle is back (same interface name, right driver), the radio
   restarts itself with `execv`, reruns bring-up and applies the current slot.
   No m-plane `reset` or manager action is needed.
3. Restarting leaves nothing behind from the old image.
4. If the dongle is missing at startup, the radio waits for it instead of
   exiting.

Three steps, in order. Each depends on the one before.

| Step | Change |
|---|---|
| 1 | `SOCK_CLOEXEC` on every socket, so `execv` gives a clean process |
| 2 | `DeviceWatch`: rtnetlink link events plus a fixed 1 s scan by interface name; restart when the device is back |
| 3 | Startup waits for the device instead of exiting, using the same events and 1 s scan |

Out of scope: a watchdog for a hung driver whose netdev stays registered, and
the P3 systemd unit (still needed for crashes and for config errors, which keep
exiting 1).

# Step 1: close sockets on `execv` (`SOCK_CLOEXEC`)

1. Every socket the radio opens is closed when the process `execv`s itself
   (m-plane `reset` today, device recovery in step 2).
2. No behaviour change otherwise: same ports, same bring-up, same `ts` reset
   confirmation (`docs/mplane.md:103`).

## Today (step 1)

`reset` sets `reset_pending_` (`src/radio/RealtekBackends.cpp:30`). `App::run`
polls it, waits 200 ms, then calls
`execv("/proc/self/exe", argv_copy_.data())` (`src/radio/App.cpp:246`). The new
image reruns `bring_up()`.

`execv` replaces the address space, so heap and threads are gone, but open fds
are inherited unless they are close-on-exec. These sockets are not:

| Socket | Created at | Effect after `reset` |
|---|---|---|
| AF_PACKET raw socket | `src/radio/PacketSocket.cpp:24` | Still bound to the interface. The kernel keeps queueing a copy of every RX frame into it until `tune.sock_rcvbuf` fills. Nobody reads it. One more per reset |
| m-plane UDP | `src/radio/MplaneServer.cpp:24` | Still bound to `net.console_port` |
| inject UDP | `src/radio/DataPlane.cpp:54` | Still bound to `net.inject_port` |
| forward-registration UDP | `src/radio/DataPlane.cpp:55` | Still bound to `net.forward_port` |
| forward UDP (ephemeral) | `src/radio/DataPlane.cpp:56` | Leaked, harmless apart from the fd |
| ioctl helper | `src/radio/NetLink.cpp:16` | Closed before return; fix only for consistency |

The new image can rebind the UDP ports only because every bind sets
`SO_REUSEADDR`. With two sockets on one port, the kernel delivers each unicast
datagram to only one of them. If it picks the leaked socket, m-plane commands
or injected frames vanish after a reset. This was never verified, and with the
fix it no longer matters.

Already close-on-exec, no change: the eventfds (`App.cpp:197`,
`DataPlane.cpp:53`), the retry timerfd (`Injector.cpp:21`), the data-plane
epoll (`DataPlane.cpp:131`). The libnl nl80211 socket (`Nl80211::open`, `src/radio/Nl80211.cpp:197`) is
created by libnl; check it on the bench (below) and fix it with
`fcntl(nl_socket_get_fd(sk), F_SETFD, FD_CLOEXEC)` after `genl_connect` only if
it shows up as leaked.

## Change (step 1)

Add `SOCK_CLOEXEC` to the type argument of each `socket()` call:

```cpp
// src/radio/PacketSocket.cpp:24
fd_ = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ETH_P_ALL));

// src/radio/MplaneServer.cpp:24
fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

// src/radio/DataPlane.cpp:54-56
inject_fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
reg_fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
fwd_fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);

// src/radio/NetLink.cpp:16
int fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
```

Use the flag on `socket()`, not a later `fcntl(F_SETFD)`, so there is no window
in which the fd is inheritable.

Leave `O_NONBLOCK` handling as it is (`fcntl(F_SETFL)` after bind); folding it
into `SOCK_NONBLOCK` is a separate cleanup and not part of this change.

Do not touch `src/vendor/` (no sockets there).

## Docs (step 1)

- `docs/implementation.md:261` (`reset` row): add "All fds are close-on-exec,
  so the new image starts with only stdin/stdout/stderr."
- `docs/implementation.md:353` (packet socket bullet): add `SOCK_CLOEXEC` to the
  list of socket options.

## Tests (step 1)

No unit test: the socket wrappers are only exercised on hardware. Run the
existing suite to confirm nothing else broke:

```bash
cmake --build build && ctest --test-dir build --output-on-failure
```

## Bench check (step 1)

On the bench (`scripts/bench_two_radios.sh` setup, or one radio started by
hand), before and after the change:

```bash
pid=$(pgrep -f 'winject-radio-realtek.*radio-a.cfg')
ls -l /proc/$pid/fd          # note the socket:[…] entries
ss -uapn | grep "pid=$pid"   # UDP sockets and their ports
mp 127.0.0.1:2201 reset
sleep 1
ls -l /proc/$pid/fd
ss -uapn | grep "pid=$pid"
mp 127.0.0.1:2201 tx_info    # ts near 0
```

Done when:

1. After `reset`, the fd count and the set of `socket:` entries match a fresh
   start (same count, all inode numbers new). Before the fix the count grows by
   five sockets per reset.
2. `ss` shows exactly one socket per port (`console_port`, `inject_port`,
   `forward_port`).
3. `ss -0apn | grep "pid=$pid"` shows one packet socket.
4. Traffic still flows after a `reset` (run `bw_test.py` once after resetting
   both radios), and `mp … ping` answers.
5. If the nl80211 netlink socket shows up twice after a reset
   (`ls -l /proc/$pid/fd`, `ss -f netlink -apn`), apply the libnl fix above
   and repeat.

# Step 2: `DeviceWatch`, restart when the device is back

## Today (step 2)

Nothing watches the device after bring-up. On unplug, the packet socket gets
`ENETDOWN` once and its ifindex becomes −1. RX goes silent, every send fails
and is counted as `dropped_wifi`, and nl80211 calls fail, so `radio_tx` returns
`io_error`. m-plane still answers. On replug the kernel creates a new netdev
with a **new ifindex** but the same `wlx…` name (it follows the MAC). The old
socket does not follow it, so the radio stays dead until something restarts
the process.

## Rule

The device is identified by `dev_.ifname`, the interface name that startup
resolved (`DeviceSelector::resolve`, whatever selector the config used), and
`dev_.ifindex`, the index it had at startup. One check decides everything:

```cpp
const unsigned idx = if_nametoindex(dev_.ifname.c_str());  // 0 = not present
```

| `idx` | Meaning |
|---|---|
| `== dev_.ifindex` | Our device, still the same netdev. Nothing to do |
| `0` | Unplugged |
| any other value | Replugged: same name, new netdev. Our socket and ifindex are stale |

`if_nametoindex` returns `unsigned`, `DeviceMatch::ifindex` is `int`; cast
`dev_.ifindex` to `unsigned` for the comparison (it is always > 0 after
`resolve`).

Compare against `dev_.ifindex`, not just `idx != 0`. An unplug and replug that
both happen between two checks would otherwise look like "still plugged in"
while the socket is dead.

A device that is back must also be bound to the configured driver:
`DeviceSelector::read_driver(dev_.ifname, &drv)` and `drv == cfg_.radio.driver`.
`read_driver` is private today (`src/radio/DeviceSelector.h:30`); move it to
the public section. No other change to `DeviceSelector`.
A dongle that came back on the stock `88XXau` is not accepted (log once, keep
waiting).

Recovery requires a name that is stable across replugs, which the `wlx<mac>`
names are. A plain `wlan0` name can change on replug; document that.

## Triggers

The check runs on two triggers. Both call the same function and neither needs
to know what changed.

1. **rtnetlink link events (fast path).** `socket(AF_NETLINK, SOCK_RAW |
   SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE)`, bound with a `sockaddr_nl`
   that has `nl_family = AF_NETLINK`, `nl_pid = 0` (kernel assigns) and
   `nl_groups = RTMGRP_LINK` (headers: `<linux/netlink.h>`,
   `<linux/rtnetlink.h>`). Registered on the main reactor with
   `reactor_.add_read_rdy`. On readable, drain with `recv` into an 8 KiB stack
   buffer until it returns -1 with `EAGAIN`, and run the check once. Do not parse the messages: any link event is a hint.
   `ENOBUFS` (events dropped) is handled the same way: run the check.
   Our own link changes during bring-up (up/down, monitor mode) produce events
   too; they leave `idx == dev_.ifindex`, so the check does nothing.
   `DeviceWatch` starts at the end of `bring_up`, so events between `resolve`
   and that point are not seen. That is fine: the check compares against the
   ifindex from `resolve`, so the first scan catches anything that happened.
2. **Fixed scan (backup).** Every **1000 ms**, using
   `reactor_.get_timer().wait_ms` like `poll_reset` in `App::run`. The callback
   re-arms itself; keep the returned `timer_id_t` and `cancel()` it in
   `DeviceWatch::stop`. A constant
   in `DeviceWatch.cpp` (`kCheckIntervalMs = 1000`), no config key.
   Why 1 s: it only matters when events were missed. An RTL8812AU takes 1–3 s
   to re-enumerate and load firmware after replug, so a faster scan gains
   nothing. The cost is one `if_nametoindex` per second while attached, plus a
   `readlink` while waiting. The scan is also the debounce timer (below).

## State machine

Every check, from either trigger, calls `check(idx, driver_ok, now_ms)` with
`now_ms` from `CLOCK_MONOTONIC`. The driver is only read when `idx != 0` and
`idx != dev_.ifindex`.

| State | Input | Next | Action / log |
|---|---|---|---|
| ATTACHED | `idx == ifindex` | ATTACHED | none |
| ATTACHED | anything else | LOST, then evaluate the LOST rows with the same input | `LOG_WRN("device %s lost", ifname)` |
| LOST | `idx == 0` | LOST | none |
| LOST | `idx != 0`, driver not ok | LOST | `LOG_WRN("device %s back on driver %s, waiting", …)` once per `idx` (remember the last logged `idx`) |
| LOST | `idx != 0`, driver ok | CANDIDATE(`idx`, since=`now_ms`) | `LOG_INF("device %s back ifindex=%u", …)` |
| CANDIDATE(i) | `idx == i`, driver ok, `now_ms - since < 1000` | CANDIDATE(i) | none |
| CANDIDATE(i) | `idx == i`, driver ok, `now_ms - since >= 1000` | (restart) | return `restart`; caller logs `LOG_INF("restarting")` and execs |
| CANDIDATE(i) | anything else | LOST, then evaluate the LOST rows with the same input | none |

`idx == ifindex` while LOST or CANDIDATE (the old index came back) cannot
happen in practice; treat it like any other non-zero `idx`, which restarts.
That is safe.

- The ATTACHED row with a non-zero new `idx` (unplug and replug both missed)
  falls through to LOST and becomes CANDIDATE in the same check.
- The restart needs the same `idx` and driver for at least 1 s. With the 1 s
  scan, it fires on the first or second scan after the device appears. This debounces USB brownouts that
  reconnect several times, and gives udev/NetworkManager time to settle.
  Bring-up already handles NetworkManager's monitor-mode race (retry loop in
  `App::bring_up`). Restart latency after replug: 1–2 s.
- While `LOST`/`CANDIDATE`, nothing else changes: m-plane keeps answering,
  `radio_tx` returns `io_error`, sends are counted as `dropped_wifi`.

## Code

New `src/radio/DeviceWatch.{h,cpp}`:

- A pure state machine, testable without root or hardware:
  ```cpp
  enum class WatchAction { none, restart };
  class DeviceWatchState
  {
  public:
      DeviceWatchState(std::string ifname, unsigned ifindex);
      // idx: if_nametoindex result; driver: bound driver for idx, empty if
      // idx == 0, idx == ifindex or unreadable; expected: cfg_.radio.driver
      WatchAction check(unsigned idx, const std::string& driver,
                        const std::string& expected, int64_t now_ms);
  };
  ```
  It logs the state changes from the table itself (tests link `Log.cpp`
  already), and has no other side effects.
- `DeviceWatch`: owns the rtnetlink fd and the scan timer, holds `ifname`,
  `ifindex`, expected driver and a `DeviceSelector` (for `read_driver`), and
  calls an `on_restart` callback when the state machine returns `restart`.
  `start(IOReactor&)` / `stop(IOReactor&)`, matching `MplaneServer`.

`App`:

- Factor the restart in `App::run` into `void App::restart()` (log
  `"restarting"`, `execv("/proc/self/exe", argv_copy_.data())`, `_exit(1)`).
  `poll_reset` keeps its 200 ms reply flush before calling it; device recovery
  calls it directly.
- Create and start `DeviceWatch` at the end of `bring_up()`, after the m-plane
  starts, with `dev_.ifname`, `dev_.ifindex`, `cfg_.radio.driver` and
  `[this]() { restart(); }`. Stop it in `shutdown()`.
- Add `DeviceWatch.cpp` to `src/radio/CMakeLists.txt`.

## Docs (step 2)

- `docs/implementation.md`: new short section "Device loss and recovery" with
  the rule, the triggers, the 1 s scan and the debounce. Replace the
  "Driver check at startup" plan in §14 (`88XXau` row) with "checked at startup
  and on recovery".
- `docs/implementation.md:497`: note that recovery relies on the `wlx…` name
  being stable across replugs.
- `docs/mplane.md`: under `reset`, note that the radio also restarts itself
  when its dongle is replugged; `ts` restarts near zero as for `reset`.

## Tests (step 2)

`src/test/DeviceWatchTest.cpp` (add it and `../radio/DeviceWatch.cpp` to
`src/test/CMakeLists.txt`), on `DeviceWatchState`:

1. `idx == ifindex` on events and ticks → `none`, forever.
2. `idx == 0` → lost; ticks with `0` → `none`.
3. Lost, then `idx = new`, driver ok at t → `none` at t and t+999,
   `restart` at t+1000.
4. Candidate on idx 7 at t, then idx 8 at t+500 → `none`; `restart` only at
   t+1500 or later.
5. Candidate at t, then `idx == 0` at t+500, idx back at t+1000 → `none` until
   t+2000.
6. `idx = new` with driver not ok → never `restart`; becomes candidate when the
   driver check passes.
7. Attached, then directly `idx = new` (missed unplug and replug) → candidate,
   `restart` 1 s later.

## Bench check (step 2)

Simulate unplug/replug without touching the hardware. Find the USB device of
the interface (`readlink -f /sys/class/net/<ifname>/device/..`), then:

```bash
dev=/sys/bus/usb/devices/<port>          # e.g. 3-1
echo 0 | sudo tee $dev/authorized        # unplug
mp 127.0.0.1:2201 ping                   # still answers
mp 127.0.0.1:2201 tx_info                # ts keeps growing
echo 1 | sudo tee $dev/authorized        # replug
sleep 4
mp 127.0.0.1:2201 tx_info                # ts near 0
iw dev                                   # monitor mode, configured channel
```

Done when:

1. The log shows `device lost`, then `device back ifindex=<new>`, then
   `restarting`, then a normal bring-up.
2. After the restart, `ts` is near zero, the PID is the same, and traffic flows
   again (`bw_test.py` run).
3. Step 1's fd check holds after the recovery restart (no duplicate sockets).
4. Unplug only (`authorized` 0, wait 30 s): no restart, m-plane answers,
   `radio_tx` returns `io_error`.
5. Quick toggle (`echo 0 …; echo 1 …` in one line): the radio still restarts
   once on the new ifindex.
6. A physical unplug and replug, into the same port and into another port,
   behaves the same as above.

# Step 3: wait for the device at startup

## Today (step 3)

`App::bring_up` calls `DeviceSelector::resolve` once (`src/radio/App.cpp:78`).
If the dongle is missing, still enumerating, or on the wrong driver, it logs
the error and the process exits 1. Nothing restarts it (no systemd unit yet).
This hits three cases:

1. Boot, or service start, before the dongle has enumerated.
2. Step 2's recovery restart, if the dongle disappears again between the
   debounce and bring-up.
3. Starting the radio while the dongle is unplugged.

## Rule (step 3)

Startup retries `DeviceSelector::resolve(cfg_.radio, …)` until it succeeds.
Use `resolve`, not `if_nametoindex`: at startup there is no `dev_.ifname` yet,
and the config may select by `usb_port` or `mac`. `resolve` already includes
the driver check, so a dongle on `88XXau` keeps waiting.

- **Triggers:** the same as step 2. An rtnetlink socket (`RTMGRP_LINK`) and the
  same fixed `kCheckIntervalMs = 1000`. The reactor is not running yet during
  bring-up, so this is a blocking loop: `poll()` on the rtnetlink fd with a
  1000 ms timeout; on an event (drain until `EAGAIN`, including `ENOBUFS`) or
  on timeout, run `resolve`.
- **Debounce:** proceed only after two successful `resolve` calls at least 1 s
  apart that return the same `ifname` and `ifindex`. Same reason as step 2: a
  dongle that has just appeared may still be loading firmware or being
  renamed by udev. If the device is already present when the process starts,
  this costs 1 s.
- **No timeout.** Wait forever; no config key.
- **Logging:** one `LOG_WRN("waiting for device: %s", err)` when waiting
  starts, and again only when the `resolve` error text changes (for example
  "no net device" → "saw wlx…=88XXau"). `LOG_INF("device found %s ifindex=%d")`
  when it proceeds. No log per scan.
- **Signals:** unchanged. During bring-up `on_signal` sees
  `g_shutdown_fd < 0` and calls `_exit(0)`, so SIGTERM stops a waiting radio.
- **`reset` takes about 1 s longer** because of the debounce. Clients already
  wait for `tx_info` to answer after `reset` (`docs/mplane.md:103`), so
  nothing else changes; mention it in that paragraph.
- **m-plane is not up while waiting.** The radio does not answer, like an ESP32
  that is not powered. `ts` starts after the wait (`start_us_` is set later in
  `bring_up`).

Order in `bring_up` stays the same apart from the wait: load config and power
CSV (errors still exit 1), wait for device, then `netlink_release_nm` and the
rest.

## Device gone during bring-up

If any step after the wait fails (monitor mode, nl80211, packet socket,
applying settings), check whether the device is still the one we found:
`if_nametoindex(dev_.ifname) == dev_.ifindex`.

- Changed or gone → log `"device lost during bring-up"` and call
  `App::restart()` (step 2). The new image waits again, from a clean state.
- Unchanged → a real failure: exit 1 as today.

This avoids tearing down half-built state (nl80211 socket, packet socket) in
place; `execv` does it, and step 1 makes that clean.

## Code (step 3)

In `src/radio/DeviceWatch.{h,cpp}`:

- Share the rtnetlink open/drain helper and `kCheckIntervalMs` with step 2's
  `DeviceWatch`.
- A pure debounce state, testable without hardware:
  ```cpp
  class StartupWaitState
  {
  public:
      // ok: resolve succeeded; ifname/ifindex: its result (ignored when !ok)
      // returns true when the device is stable and bring-up may proceed
      bool check(bool ok, const std::string& ifname, int ifindex, int64_t now_ms);
  };
  ```
- `void wait_for_device(const RadioConfig& radio, DeviceSelector& sel,
  DeviceMatch* out)`: the blocking loop. It returns only when the device is
  stable (or the process is killed). If the rtnetlink socket cannot be
  opened, log a warning and use the 1 s scan alone (`usleep` instead of
  `poll`). Close the rtnetlink socket before returning.

In `App::bring_up`: replace the single `resolve` call with `wait_for_device`,
and route every `return false` after it through one helper:

```cpp
// Logs what failed. If the device is gone or replugged, restarts (does not
// return); otherwise returns false so bring_up fails as today.
bool App::fail_bring_up(const char* what);

// usage
if (!monitor_ok)
{
    return fail_bring_up("monitor mode failed");
}
```

Apply it to all failure returns after the wait, including the ones that are
not about the device (state dir, UDP binds, m-plane start). For those the
device is unchanged, so they still exit 1.

## Docs (step 3)

- `docs/implementation.md`, in the "Device loss and recovery" section from
  step 2: startup waits for the device, the debounce, no timeout, m-plane is
  down while waiting, and the "device gone during bring-up" restart.
- `README.md` / run instructions: the radio no longer exits when the dongle is
  missing; it logs `waiting for device` and starts when it appears.

## Tests (step 3)

`src/test/DeviceWatchTest.cpp`, on `StartupWaitState`:

1. `ok` at t=0 and t=1000 with the same ifname/ifindex → false, then true.
2. `ok` at t=0 and t=500 → false both times (not yet 1 s).
3. `ok` at t=0, `!ok` at t=500, `ok` at t=1000 → false; the stable period
   restarts at t=1000, true at t≥2000.
4. `ok` with ifindex 5 at t=0, ifindex 7 at t=1000 → false; true at t≥2000
   with ifindex 7.
5. Never `ok` → never true.

## Bench check (step 3)

```bash
echo 0 | sudo tee $dev/authorized          # dongle "unplugged"
sudo build/winject-radio-realtek --config configuration/radio-a.cfg &
# log: waiting for device: …; process stays up; m-plane does not answer
echo 1 | sudo tee $dev/authorized
# log: device found wlx… ifindex=N, then a normal bring-up
mp 127.0.0.1:2201 tx_info                  # answers, ts small
```

Done when:

1. Starting with the dongle unplugged waits instead of exiting, and comes up
   1–2 s after replug with traffic flowing.
2. Starting with the dongle present works as before, with about 1 s extra
   startup time.
3. SIGTERM while waiting exits 0 straight away.
4. Unplugging during bring-up (toggle `authorized` right after start) ends in
   `device lost during bring-up`, a restart, waiting, and a normal bring-up
   once the dongle is back. Not an exit 1.
5. A config error (bad `radio.txpower` path) still exits 1 at once.
6. After a reboot with the radio started early (before USB enumeration), it
   comes up by itself.
