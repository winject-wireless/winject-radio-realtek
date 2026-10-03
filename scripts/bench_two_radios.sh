#!/usr/bin/env bash
# Bandwidth test through winject-l3 with both local RTL8812AU dongles.
#
# Starts radio-a and radio-b (configuration/radio-{a,b}.cfg), one
# winject-manager per radio (configuration/manager-{a,b}.cfg), then runs
# winject-l3 tools/bw_test.py. On exit it stops everything and hands the
# dongles back to NetworkManager in managed mode.
#
# Usage (extra arguments go to bw_test.py):
#   ./scripts/bench_two_radios.sh
#   ./scripts/bench_two_radios.sh --channel 13
#   ./scripts/bench_two_radios.sh --modulation OFDM_24M,OFDM_54M,OFDM_MCS7_SGI
#   ./scripts/bench_two_radios.sh --modulation OFDM_MCS7_SGI --kbps 40000 --test-ab
#
# Environment:
#   WINJECT_L3=<dir>       winject-l3 checkout (default ../winject-l3)
#   WINJECT_MANAGER=<bin>  manager binary, skips the build (see ensure_manager.sh)
#   BIN=<bin>              radio binary (default build/src/radio/winject-radio-realtek)
#   LOG_DIR=<dir>          logs and radio snapshots (default $TMPDIR/winject-bench-<pid>)
#
# Needs sudo for the radios. Do not use --no-cca: the Realtek radio rejects
# cca=false (implementation.md §6.4).

set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
L3="${WINJECT_L3:-$ROOT/../winject-l3}"
BIN="${BIN:-$ROOT/build/src/radio/winject-radio-realtek}"
LOG_DIR="${LOG_DIR:-${TMPDIR:-/tmp}/winject-bench-$$}"
mkdir -p "$LOG_DIR"

if [[ ! -x "$BIN" ]]; then
  echo "error: $BIN not built (cmake --build build)" >&2
  exit 1
fi
if [[ ! -f "$L3/tools/bw_test.py" ]]; then
  echo "error: winject-l3 not found at $L3 (set WINJECT_L3)" >&2
  exit 1
fi
# shellcheck source=/dev/null
source "$L3/scripts/ensure_manager.sh"
ensure_winject_manager "$L3"

if pgrep -x winject-manager >/dev/null || pgrep -x winject-radio-r >/dev/null; then
  echo "error: winject-manager or winject-radio-realtek already running" >&2
  exit 1
fi

IFACES=()
MGR_PIDS=()

cleanup() {
  set +e
  if [[ ${#MGR_PIDS[@]} -gt 0 ]]; then
    kill "${MGR_PIDS[@]}" 2>/dev/null
  fi
  sudo pkill -TERM -x winject-radio-r 2>/dev/null
  sleep 1
  for i in "${IFACES[@]}"; do
    sudo ip link set "$i" down
    for _ in 1 2 3; do
      sudo iw dev "$i" set type managed 2>/dev/null && break
      sleep 0.5
    done
    sudo ip link set "$i" up
    sudo nmcli device set "$i" managed yes
  done
  echo "logs: $LOG_DIR"
}
trap cleanup EXIT INT TERM

for r in a b; do
  sudo rm -rf "/tmp/winject-radio-$r"
  sudo bash -c "'$BIN' --config '$ROOT/configuration/radio-$r.cfg' >'$LOG_DIR/radio-$r.log' 2>&1 &"
done

# Bring-up logs "<ifname>: channel=... tx_power=... modulation=..." when ready.
for r in a b; do
  ifname=""
  for _ in $(seq 1 60); do
    ifname="$(sed -nE 's/.*\| INF \| ([^ :]+): channel=.*/\1/p' "$LOG_DIR/radio-$r.log" | head -1)"
    [[ -n "$ifname" ]] && break
    if grep -q "| ERR |" "$LOG_DIR/radio-$r.log"; then
      break
    fi
    sleep 0.25
  done
  if [[ -z "$ifname" ]]; then
    echo "radio-$r failed to start:" >&2
    tail -20 "$LOG_DIR/radio-$r.log" >&2
    exit 1
  fi
  IFACES+=("$ifname")
  echo "radio-$r up on $ifname"
done

for r in a b; do
  "$MANAGER" "$ROOT/configuration/manager-$r.cfg" >"$LOG_DIR/manager-$r.log" 2>&1 &
  MGR_PIDS+=($!)
done
for r in a b; do
  for _ in $(seq 1 40); do
    grep -q "manager running" "$LOG_DIR/manager-$r.log" && break
    sleep 0.25
  done
  if ! grep -q "manager running" "$LOG_DIR/manager-$r.log"; then
    echo "manager-$r failed to start:" >&2
    tail -20 "$LOG_DIR/manager-$r.log" >&2
    exit 1
  fi
done

export WINJECT_RADIO_SNAP_DIR="$LOG_DIR"
set +e
python3 "$L3/tools/bw_test.py" --udp --a 127.0.0.1 --b 127.0.0.1 --drop-stages "$@"
status=$?
set -e
exit "$status"
