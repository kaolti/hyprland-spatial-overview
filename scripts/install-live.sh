#!/usr/bin/env bash
# Development: build and hot-swap the plugin in the running Hyprland session,
# keeping every window where it is on the canvas. (To install, use install.sh.)
#
#   PLUGIN_PATH=...  plugin file to load (default: the build in this folder)
#   SKIP_BUILD=1     load what is already built
#   PLAIN_UNLOAD=1   unload without the repair helper
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
plugin_path="${PLUGIN_PATH:-$project_dir/spatialoverview.so}"
source "$project_dir/scripts/common.sh"

[[ -n "${SKIP_BUILD:-}" ]] || make -C "$project_dir" -j"$(nproc)" all

if ! hyprland_session; then
  echo "Build complete. It loads with the next Hyprland start."
  exit 0
fi

remember_canvas_layout
if spatialoverview_loaded; then
  if [[ -n "${PLAIN_UNLOAD:-}" ]]; then
    hyprctl plugin unload "$plugin_path"
  elif ! safe_unload; then
    echo "The new build is in place and loads when you log out and back in." >&2
    exit 1
  fi
fi
hyprctl plugin load "$plugin_path"
hyprctl reload

config_errors="$(hyprctl configerrors)"
if [[ -n "$config_errors" ]]; then
  printf 'Hyprland config errors after reload:\n%s\n' "$config_errors" >&2
  exit 1
fi

hyprctl dispatch 'hl.plugin.spatialoverview.overview("on all")'
echo "Phantomat is live. SUPER + CTRL + G, then type."
