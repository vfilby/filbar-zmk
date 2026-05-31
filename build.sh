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

# Pull west-level flags (e.g. --pristine / -p) out of "$@" so they go to
# `west build` rather than being forwarded past `--` to cmake.
west_flags=()
extra_args=()
for arg in "$@"; do
  case "$arg" in
    --pristine|-p) west_flags+=("-p" "always") ;;
    *) extra_args+=("$arg") ;;
  esac
done
set -- "${extra_args[@]+"${extra_args[@]}"}"

case "$target" in
  left)         shield="splitkb_aurora_corne_left nice_view_adapter nice_view" ; out="left" ;;
  right)        shield="splitkb_aurora_corne_right nice_view_adapter nice_view" ; out="right" ;;
  left-oled)    shield="splitkb_aurora_corne_left"  ; out="left-oled" ;;
  right-oled)   shield="splitkb_aurora_corne_right" ; out="right-oled" ;;
  settings)     shield="settings_reset" ; out="settings_reset" ;;
  display-test) shield="nice_view_adapter nice_view" ; out="display-test" ; is_zephyr_sample=1 ;;
  *) echo "unknown target: $target (use left | right | left-oled | right-oled | settings | display-test)"; exit 1 ;;
esac

repo="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$repo/build" "$repo/firmware"

if [ "${is_zephyr_sample:-0}" = "1" ]; then
  # Bare Zephyr display sample (no ZMK). Tests the display in isolation.
  docker run --rm \
    -v "$repo:/workspaces/zmk-config" \
    -w /workspaces/zmk-config \
    -e ZEPHYR_BASE=/workspaces/zmk-config/zephyr \
    -e Zephyr_DIR=/workspaces/zmk-config/zephyr/share/zephyr-package/cmake \
    zmkfirmware/zmk-build-arm:stable \
    west build "${west_flags[@]+"${west_flags[@]}"}" -s zephyr/samples/drivers/display -d "build/$out" \
      -b "nice_nano//zmk" \
      -- "-DBOARD_ROOT=/workspaces/zmk-config/zmk/app/module;/workspaces/zmk-config/zmk/app" \
         -DSHIELD="$shield" \
         -DEXTRA_DTC_OVERLAY_FILE=/workspaces/zmk-config/config/cs-bodge-test.overlay "$@"
  uf2_src="$repo/build/$out/zephyr/zephyr.uf2"
else
  docker run --rm \
    -v "$repo:/workspaces/zmk-config" \
    -w /workspaces/zmk-config \
    -e ZEPHYR_BASE=/workspaces/zmk-config/zephyr \
    -e Zephyr_DIR=/workspaces/zmk-config/zephyr/share/zephyr-package/cmake \
    zmkfirmware/zmk-build-arm:stable \
    west build "${west_flags[@]+"${west_flags[@]}"}" -s zmk/app -d "build/$out" \
      -b "nice_nano//zmk" \
      -- -DZMK_CONFIG=/workspaces/zmk-config/config -DSHIELD="$shield" "$@"
  uf2_src="$repo/build/$out/zephyr/zmk.uf2"
fi

cp "$uf2_src" "$repo/firmware/$out.uf2"
echo
echo "==> firmware/$out.uf2 ready to flash"
