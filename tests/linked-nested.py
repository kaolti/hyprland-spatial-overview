"""Linked screens: two monitors show adjacent parts of the canvas, as they are
arranged, and move together, like one wide desk. A window across the seam
shows half on each screen, at 100% and zoomed out, when either screen's
camera moves, and after it is dragged across the seam.

usage: tests/linked-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
Set NESTED_SCALE=1.25 to test scaled outputs.
"""
import json, os, subprocess, sys, tempfile, time, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("nav", os.path.join(HERE, "navigator-nested.py"))
nav = importlib.util.module_from_spec(spec); spec.loader.exec_module(nav)
ROOT = os.path.dirname(HERE)
subprocess.run(["make", "-s", "-C", ROOT, "test-tools"], check=True)

GAME = (0x2a, 0x5a, 0x08)
SCALE = float(os.environ.get("NESTED_SCALE", "1"))
LW, LH = round(1280 / SCALE), round(720 / SCALE)
tmp = tempfile.mkdtemp(prefix="linked-")
fifo, log = os.path.join(tmp, "game.in"), os.path.join(tmp, "game.log")
os.mkfifo(fifo)
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-linked"),
               extra_lua='\nhl.config({xwayland={force_zero_scaling=true}})\n'
                         f'hl.monitor({{ output = "WAYLAND-1", mode = "1280x720@60", position = "0x0", scale = {SCALE} }})\n'
                         f'hl.monitor({{ output = "SECOND", mode = "1280x720@60", position = "{LW}x0", scale = {SCALE} }})\n')
failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)


def frame(output):
    raw = subprocess.run(["grim", "-o", output, "-t", "ppm", "-"], env=n.env(), capture_output=True, timeout=10).stdout
    parts = raw.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def bbox(output, rgb=GAME, tol=12):
    w, h, px = frame(output)
    xs, ys = [], []
    for y in range(0, h, 2):
        row = px[y * w * 3:(y + 1) * w * 3]
        for x in range(0, w, 2):
            if abs(row[x * 3] - rgb[0]) < tol and abs(row[x * 3 + 1] - rgb[1]) < tol and abs(row[x * 3 + 2] - rgb[2]) < tol:
                xs.append(x); ys.append(y)
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1) if xs else None


def halves(label):
    """The game drawn across the seam: its left part ends at the right edge of
    WAYLAND-1, its right part starts at the left edge of SECOND, level."""
    left, right = bbox("WAYLAND-1"), bbox("SECOND")
    print(f"  [{label}] WAYLAND-1 {left}  SECOND {right}")
    ok = bool(left and right and left[2] >= 1276 and right[0] <= 3 and abs(left[1] - right[1]) <= 4 and abs(left[3] - right[3]) <= 4)
    check(ok, f"{label}: half the window on each screen, level")
    return left, right


def game_rect():
    """Where the game is, as the X server tells it: at 100% on the canvas,
    exactly where it is drawn, in pixels across both screens."""
    os.write(game_in, b"report\n"); time.sleep(0.4)
    line = [l for l in open(log).read().splitlines() if l.startswith("at ")][-1].split()
    return tuple(map(int, line[1:5]))


def parts_as_placed(label):
    """Every part of the window that lies on a screen is drawn there, and
    nothing else: no part lost at the seam, none duplicated."""
    x, y, w, h = game_rect()
    want = {}
    for name, x0 in (("WAYLAND-1", 0), ("SECOND", 1280)):
        l, r = max(x, x0), min(x + w, x0 + 1280)
        want[name] = (l - x0, max(y, 0), r - x0, min(y + h, 720)) if r - l > 4 else None
    got = {name: bbox(name) for name in want}
    print(f"  [{label}] placed at {(x, y, w, h)}; expected {want}; drawn {got}")
    ok = all((want[k] is None) == (got[k] is None) and (want[k] is None or all(abs(a - b) <= 4 for a, b in zip(want[k], got[k]))) for k in want)
    check(ok, f"{label}: each screen shows exactly its part of the window")


def client(title):
    return next((c for c in n.clients() if c["title"] == title), None)


def land(monitor, title):
    n.dispatch(f'hl.dsp.focus({{monitor="{monitor}"}})'); time.sleep(0.3)
    n.dispatch(f'hl.plugin.spatialoverview.canvas("search {title}")'); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.2)


def mouse(*cmds):
    subprocess.run([os.path.join(ROOT, ".build/vpointer"), "1", "1", *map(str, cmds)], env=n.env(), check=True, timeout=30)


try:
    n.launch()
    n.ctl("output", "create", "wayland", "SECOND")
    time.sleep(0.8)
    n.ctl("reload")
    time.sleep(1.0)
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c '{ROOT}/.build/x11-game x11-game < {fifo} > {log}'") + ")")
    game_in = os.open(fifo, os.O_RDWR)
    for _ in range(100):
        if client("x11-game"):
            break
        time.sleep(0.1)
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(0.8)

    # Put the game half over the seam: land on it on WAYLAND-1 (centered
    # there), then move it right so its middle is on the seam.
    land("WAYLAND-1", "x11-game")
    centered = bbox("WAYLAND-1")
    real = client("x11-game")
    shift = (1280 - (centered[0] + centered[2]) / 2) / SCALE  # logical
    n.dispatch(f'hl.dsp.window.move({{x={round(real["at"][0] + shift)}, y={real["at"][1]}, window="title:x11-game"}})'); time.sleep(0.8)
    halves("100%")
    parts_as_placed("100%")

    # Focusing the other screen gives the (visible) window focus: nothing moves.
    n.dispatch('hl.dsp.focus({monitor="SECOND"})'); time.sleep(0.8)
    halves("after focusing SECOND")

    # The other screen moves the camera: both follow.
    before_l, before_r = bbox("WAYLAND-1"), bbox("SECOND")
    n.dispatch('hl.plugin.spatialoverview.canvas("pan left")'); time.sleep(1.2)
    after_l, after_r = bbox("WAYLAND-1"), bbox("SECOND")
    print(f"  [pan from SECOND] WAYLAND-1 {before_l} -> {after_l}  SECOND {before_r} -> {after_r}")
    moved_r = after_r[2] - before_r[2] if after_r and before_r else 0
    moved_l = after_l[0] - before_l[0] if after_l and before_l else None
    check(moved_r > 20 and (moved_l is None or abs(moved_l - moved_r) <= 4), f"panning from SECOND moves both screens' view the same way ({moved_l}, {moved_r})")
    parts_as_placed("panned")
    n.dispatch('hl.plugin.spatialoverview.canvas("pan right")'); time.sleep(1.2)
    halves("panned back")

    def whole(label):
        """The window is drawn whole across the screens: nothing cut."""
        left, right = bbox("WAYLAND-1"), bbox("SECOND")
        width = (left[2] - left[0] if left else 0) + (right[2] - right[0] if right else 0)
        print(f"  [{label}] WAYLAND-1 {left}  SECOND {right}  drawn width {width}")
        return left, right, width

    # Zoomed out, the two screens are one zoomed-out view.
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.5)
    n.shot("zoomed-out")
    left, right, zoomed_width = whole("zoomed out")
    if left and right:
        check(left[2] >= 1276 and right[0] <= 3 and abs(left[1] - right[1]) <= 4, "zoomed out: continuous across the seam")

    # Drag it (zoomed out) across the seam, drop it, back to 100%: the window
    # is drawn whole, on both screens if it lies across the seam.
    grab = (left[0] + 20, left[1] + 20) if left else (1280 + right[0] + 20, right[1] + 20)
    step = 12 if left else -12   # toward the other screen
    n.dispatch(f"hl.dsp.cursor.move({{x={round(grab[0] / SCALE)}, y={round(grab[1] / SCALE)}}})"); time.sleep(0.3)
    mouse("down", "sleep", 120, *sum([["rel", step, 0, "sleep", 16] for _ in range(25)], []), "sleep", 200, "up", "sleep", 400)
    whole("after drag, zoomed out")
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(1.2)
    n.shot("after-drag")
    whole("after drag, 100%")
    parts_as_placed("after dragging across the seam")
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
