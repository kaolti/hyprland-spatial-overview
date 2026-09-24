#!/usr/bin/env bash
# Remove Spatial Overview: unload it (windows go back to your normal layout),
# take it out of hyprland.lua, and delete the installed plugin.
#
#   scripts/uninstall.sh          keep your settings, in case you come back
#   scripts/uninstall.sh --purge  also delete settings, tuning and window memory
set -euo pipefail

project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source "$project_dir/scripts/common.sh"

config_dir="${XDG_CONFIG_HOME:-$HOME/.config}/hypr"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/spatial-overview"
state_dir="${XDG_STATE_HOME:-$HOME/.local/state}/spatial-overview"
hyprland_lua="$config_dir/hyprland.lua"
marker_begin="-- >>> spatial-overview >>>"
marker_end="-- <<< spatial-overview <<<"

say() { printf '\033[1m%s\033[0m\n' "$*"; }
fail() {
  printf 'uninstall: %s\n' "$*" >&2
  exit 1
}

purge=0
for arg in "$@"; do
  case $arg in
  --purge) purge=1 ;;
  -h | --help)
    sed -n '2,7p' "${BASH_SOURCE[0]}" | sed 's/^# \{0,1\}//'
    exit 0
    ;;
  *) fail "unknown option: $arg (see --help)" ;;
  esac
done

# Unload first: that is what puts windows back into your normal layout.
if hyprland_session && spatialoverview_loaded; then
  safe_unload || fail "nothing was removed; log out and back in, then run this again."
fi

if [[ -f $hyprland_lua ]] && grep -qF -e "$marker_begin" "$hyprland_lua"; then
  cp -p "$hyprland_lua" "$hyprland_lua.bak.$(date +%s)-spatial-overview"
  python3 - "$hyprland_lua" "$marker_begin" "$marker_end" <<'PY'
import sys
path, begin, end = sys.argv[1:]
lines = open(path).read().split("\n")
out, skipping = [], False
for line in lines:
    if line.strip() == begin:
        skipping = True
        # drop the blank line the installer put before the block
        if out and out[-1].strip() == "":
            out.pop()
        continue
    if skipping:
        skipping = line.strip() != end
        continue
    out.append(line)
open(path, "w").write("\n".join(out))
PY
  say "Removed Spatial Overview from $hyprland_lua (backup next to it)"
elif [[ -f $hyprland_lua ]] && grep -q 'spatialoverview' "$hyprland_lua"; then
  say "Your hyprland.lua loads Spatial Overview in a way the installer did not write; remove those lines yourself:"
  grep -n 'spatialoverview' "$hyprland_lua" | sed 's/^/  /'
fi

rm -rf "$data_dir"
say "Deleted $data_dir"

if ((purge)); then
  rm -f "$config_dir/spatialoverview.lua" "$config_dir/spatialoverview-tuning.lua"
  rm -rf "$state_dir"
  say "Deleted your Spatial Overview settings, tuning and window memory"
else
  say "Kept your settings: $config_dir/spatialoverview.lua and spatialoverview-tuning.lua (--purge deletes them)"
fi

if hyprland_session; then
  hyprctl reload >/dev/null
fi
say "Spatial Overview is uninstalled."
