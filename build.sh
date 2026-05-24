#!/usr/bin/env bash
# Local ZMK build via Docker.
# Usage:
#   ./build.sh left         # build left half
#   ./build.sh right        # build right half
#   ./build.sh settings     # build settings_reset firmware
#   ./build.sh left --pristine   # force clean rebuild
set -euo pipefail

target="${1:-left}"
shift || true

case "$target" in
  left)     shield="splitkb_aurora_corne_left nice_view_adapter nice_view" ; out="left" ;;
  right)    shield="splitkb_aurora_corne_right nice_view_adapter nice_view" ; out="right" ;;
  settings) shield="settings_reset" ; out="settings_reset" ;;
  *) echo "unknown target: $target (use left | right | settings)"; exit 1 ;;
esac

repo="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$repo/build" "$repo/firmware"

docker run --rm \
  -v "$repo:/workspaces/zmk-config" \
  -v "$repo/.west-workspace:/workspaces/zmk" \
  -w /workspaces/zmk/zmk/app \
  zmkfirmware/zmk-build-arm:stable \
  west build -d "/workspaces/zmk-config/build/$out" \
    -b "nice_nano//zmk" \
    -- -DZMK_CONFIG="/workspaces/zmk-config/config" -DSHIELD="$shield" "$@"

cp "$repo/build/$out/zephyr/zmk.uf2" "$repo/firmware/$out.uf2"
echo
echo "==> firmware/$out.uf2 ready to flash"
