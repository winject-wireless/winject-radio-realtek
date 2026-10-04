#!/usr/bin/env bash
set -euo pipefail
# Copy shared m-plane sources from winject-radio-esp32 into src/vendor/mplane/.
# Does not check out the ESP32 repo; uses the commit resolved from the argument
# (default HEAD) only to record provenance in vendor/mplane/VERSION.
COMMIT_REF="${1:-HEAD}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ESP="${ESP32_REPO:-$HOME/development/winject-radio-esp32}"
DST="$ROOT/src/vendor/mplane"
FW="$ESP/src/winject-esp32"
COMMIT="$(git -C "$ESP" rev-parse "$COMMIT_REF")"
for f in mplane/mplane_args.{h,cpp} mplane/mplane_commands.{h,cpp} \
  mplane/mplane_reply.{h,cpp} mplane/mplane_req_id.{h,cpp} \
  mplane/mplane_version.{h,cpp} mplane/mplane_backend.h config_types.{h,cpp}; do
  cp "$FW/$f" "$DST/$(basename "$f")"
done
echo "$COMMIT" > "$DST/VERSION"
echo "synced mplane from $ESP @ $COMMIT (config.h / frame.h shims unchanged)"
