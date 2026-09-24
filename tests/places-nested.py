"""Workspaces as places on the canvas: canvas("go N"), canvas("send N [stay]"),
canvas("go next|prev|back"), on two linked screens.
- going to another place moves both screens' view one place over: what was
  there is out of view; going back returns exactly;
- sending the focused window to a place and following it keeps it at the same
  spot on the screen; "stay" sends it away and the view does not move;
- next and previous go to the place next door, empty or not.

usage: tests/places-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
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
tmp = tempfile.mkdtemp(prefix="places-")
fifo = os.path.join(tmp, "game.in")
os.mkfifo(fifo)
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-places"), extra=",places=true",
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


def state():
    return json.loads(n.ctl("spatialoverview"))


def view(monitor="WAYLAND-1"):
    return next(tuple(s["view"]) for s in state()["screens"] if s["monitor"] == monitor)


def client(title):
    return next((c for c in n.clients() if c["title"] == title), None)


def boxes():
    return {c["title"]: (tuple(c["at"]), tuple(c["size"])) for c in n.clients()}


def canvas(action, wait=1.3):
    n.dispatch(f'hl.plugin.spatialoverview.canvas("{action}")'); time.sleep(wait)


def minimap_shown(output="SECOND"):
    """Whether the minimap panel (darker than the empty canvas) is in the
    bottom right corner of a screen."""
    w, h, px = frame(output)
    dark = total = 0
    for y in range(h - 160, h - 40, 4):
        for x in range(w - 250, w - 40, 4):
            i = (y * w + x) * 3
            total += 1
            dark += max(px[i], px[i + 1], px[i + 2]) <= 13
    return dark / max(1, total) > 0.5


def land(monitor, title):
    n.dispatch(f'hl.dsp.focus({{monitor="{monitor}"}})'); time.sleep(0.3)
    n.dispatch(f'hl.plugin.spatialoverview.canvas("search {title}")'); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.2)


try:
    n.launch()
    n.ctl("output", "create", "wayland", "SECOND")
    time.sleep(0.8)
    n.ctl("reload")
    time.sleep(1.0)
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c '{ROOT}/.build/x11-game x11-game < {fifo}'") + ")")
    game_in = os.open(fifo, os.O_RDWR)
    last = None
    for _ in range(30):
        count = len(n.clients())
        if client("x11-game") and count == last:
            break
        last = count
        time.sleep(0.7)
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(0.8)
    for _ in range(4):
        land("SECOND", "x11-game")
        if bbox("SECOND"):
            break
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + LW // 2}, y={LH // 2}}})"); time.sleep(0.3)
    ws = next(m for m in json.loads(n.ctl("-j", "monitors")) if m["focused"])["activeWorkspace"]["id"]
    here = ws if 1 <= ws <= 10 else 1
    other = 7 if here != 7 else 8
    v0, b0, spot = view(), boxes(), bbox("SECOND")
    print(f"  at place {here}; view {v0}; game drawn {spot}")

    # -- go to another place and back; at 100% the minimap shows for a moment
    check(not minimap_shown(), "at 100%, no minimap")
    canvas(f"go {other}", wait=0.5)
    check(minimap_shown(), "going to a place at 100% shows the minimap while travelling")
    time.sleep(3.0)
    check(not minimap_shown(), "and it is gone again after a moment")
    v1 = view()
    print(f"  go {other}: view {v1}; game drawn {bbox('SECOND')}")
    check(v1[0] != v0[0] and v1[1] == v0[1], "going to another place moves the view along the row")
    check(bbox("SECOND") is None and bbox("WAYLAND-1") is None, "what was here is out of view there")
    check(boxes() == b0, "no window moved")
    canvas("go back")
    check(view() == v0, f"go back: exactly where it was ({view()} vs {v0})")
    check(bbox("SECOND") == spot, "with the game where it was")

    # -- send the game along, following it
    n.dispatch('hl.dsp.focus({window="title:x11-game"})'); time.sleep(0.3)
    canvas(f"send {other}")
    print(f"  send {other}: view {view()}; game drawn {bbox('SECOND')}")
    check(view() == v1, "sending and following goes to that place")
    got = bbox("SECOND")
    check(got and all(abs(a - b) <= 4 for a, b in zip(got, spot)), f"the game is at the same spot on the screen ({got} vs {spot})")
    check(json.loads(n.ctl("-j", "activewindow")).get("title") == "x11-game", "and keeps focus")
    others_before = {k: v for k, v in b0.items() if k != "x11-game"}
    check({k: v for k, v in boxes().items() if k != "x11-game"} == others_before, "no other window moved")

    # -- next / prev: the place next door, empty or not
    stride = (v1[0] - v0[0]) / (other - here)
    canvas("go prev")
    check(abs(view()[0] - (v1[0] - stride)) <= 2 and view()[1] == v1[1], f"go prev from {other}: place {other - 1}, next door ({view()})")
    canvas("go next")
    check(view() == v1, f"go next: back to place {other} ({view()})")

    # -- send it away without following
    canvas(f"send 3 stay" if other != 3 and here != 3 else "send 4 stay")
    check(view() == v1, "send ... stay: the view does not move")
    check(bbox("SECOND") is None and bbox("WAYLAND-1") is None, "and the game is gone from it")

    # -- in the zoomed-out view: the view glides there and stays zoomed out
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.2)
    zoomed_before = view()
    canvas(f"go {here}")
    s = state()
    print("  zoomed out, go:", [(x["monitor"], x["zoom"], x["navigating"], x["view"]) for x in s["screens"]])
    check(all(x["navigating"] for x in s["screens"]), f"zoomed out, going to a place stays zoomed out ({s['screens']})")
    check(view()[0] < zoomed_before[0], "and the view moves over to it")
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(1.2)
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
