#!/usr/bin/env bash
set -euo pipefail
COMMIT="${1:-3dbab50c2598102d4cd322f653bb5ec58b491dce}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESP="${ESP32_REPO:-$HOME/development/winject-radio-esp32}"
DST="$ROOT/src/vendor/mplane"
FW="$ESP/src/winject-esp32"
git -C "$ESP" fetch --depth 1 origin "$COMMIT" 2>/dev/null || true
git -C "$ESP" checkout "$COMMIT"
for f in mplane/mplane_args.{h,cpp} mplane/mplane_commands.{h,cpp} \
  mplane/mplane_reply.{h,cpp} mplane/mplane_req_id.{h,cpp} \
  mplane/mplane_backend.h config_types.{h,cpp}; do
  cp "$FW/$f" "$DST/$(basename "$f")"
done
echo "$COMMIT" > "$DST/VERSION"
echo "synced mplane to $COMMIT (restore frame.h and config.h shims if needed)"
