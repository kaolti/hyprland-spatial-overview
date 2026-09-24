# Shared by install.sh, install-live.sh and uninstall.sh. Source it after
# setting project_dir.

hyprland_session() {
  [[ -n ${HYPRLAND_INSTANCE_SIGNATURE:-} ]] && hyprctl instances >/dev/null 2>&1
}

spatialoverview_loaded() {
  hyprctl plugin list 2>/dev/null | grep -q '^Plugin spatialoverview '
}

# Unloading the plugin returns every window to its normal layout. Writing the
# canvas positions to the layout memory first lets the next load put each
# window back where it was (canvas.remember_layout).
remember_canvas_layout() {
  local state_dir="${XDG_STATE_HOME:-$HOME/.local/state}/spatial-overview"
  mkdir -p "$state_dir"
  python3 - "$state_dir/canvas-memory.tsv" <<'EOF'
import json, subprocess, sys, time

clients = json.loads(subprocess.check_output(["hyprctl", "clients", "-j"]))
monitors = json.loads(subprocess.check_output(["hyprctl", "monitors", "-j"]))
now = int(time.time())
clean = lambda s: s.replace("\t", " ").replace("\n", " ").replace("\r", " ")

lines = ["# spatial-overview canvas memory v1"]
for monitor in monitors:
    # Center each camera on the most recently used window it owns.
    owned = [c for c in clients if c.get("monitor") == monitor["id"] and c.get("mapped") and c["workspace"]["id"] > 0]
    if owned:
        c = min(owned, key=lambda c: c.get("focusHistoryID", 1 << 30))
        w, h = monitor["width"] / monitor["scale"], monitor["height"] / monitor["scale"]
        cx, cy = c["at"][0] + c["size"][0] / 2, c["at"][1] + c["size"][1] / 2
        lines.append(f"camera\t{monitor['name']}\t{cx - monitor['x'] - w / 2:.1f}\t{cy - monitor['y'] - h / 2:.1f}")
for c in clients:
    if not c.get("mapped") or not c.get("floating") or c["workspace"]["id"] <= 0 or c.get("pinned"):
        continue
    klass = clean(c.get("class") or c.get("initialClass") or "")
    title = clean(c.get("title") or c.get("initialTitle") or klass)
    if klass:
        x, y = c["at"]
        w, h = c["size"]
        lines.append(f"window\t{klass}\t{title}\t{x:.1f}\t{y:.1f}\t{w:.1f}\t{h:.1f}\t{now}")
open(sys.argv[1], "w").write("\n".join(lines) + "\n")
print(f"Remembered where {len(lines) - 1} windows and cameras are.")
EOF
}

# Unloads the running build through a small helper plugin
# (scripts/safe-unload.cpp) that first repairs window layout state older
# builds could leave inconsistent; re-tiling such a window crashed Hyprland.
# Returns non-zero, having unloaded nothing, if that is not safe.
safe_unload() {
  make -C "$project_dir" --no-print-directory -s safe-unload
  local helper="$project_dir/.build/safe-unload.so"
  local report="${XDG_RUNTIME_DIR:-/tmp}/spatialoverview-safe-unload.${HYPRLAND_INSTANCE_SIGNATURE:?not inside Hyprland}"
  rm -f "$report"
  hyprctl plugin load "$helper" >/dev/null
  for _ in $(seq 50); do
    grep -q '^unloaded ' "$report" 2>/dev/null && break
    sleep 0.1
  done
  hyprctl plugin unload "$helper" >/dev/null || true

  if ! grep -q '^unloaded 1' "$report" 2>/dev/null; then
    echo "Could not unload the running Phantomat safely; nothing was changed." >&2
    [[ -f "$report" ]] && sed 's/^/  /' "$report" >&2
    rm -f "$report"
    return 1
  fi
  local checked repaired repaired_after unresolved
  read -r _ checked repaired _ < <(grep '^before ' "$report")
  read -r _ _ repaired_after unresolved < <(grep '^after ' "$report")
  echo "Unloaded the running Phantomat ($checked windows checked, $((repaired + repaired_after)) layouts repaired)."
  if (( unresolved > 0 )); then
    echo "Warning: $unresolved window layouts could not be repaired; avoid toggling floating on them until you log out and back in." >&2
  fi
  rm -f "$report"
}
