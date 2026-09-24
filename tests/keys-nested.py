"""Every Hyprland/Omarchy key that touches windows, run on the canvas in each
situation, checked against the same rules:
- no other window moves;
- the window stays on the canvas: floating, and drawn where it should be;
- both screens are in the same view (100% or zoomed out) afterwards;
- zooming out and back afterwards changes nothing;
- the compositor survives.
Keys are run as the dispatchers their bindings run (a nested session has no
key bindings). Situations: the window on one screen, across the seam, off
screen (the camera panned away), filled (SUPER + T), and zoomed out.

usage: tests/keys-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
Set NESTED_SCALE=1.25 to test scaled outputs; KEYS=name,name to run some.
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
tmp = tempfile.mkdtemp(prefix="keys-")
fifo = os.path.join(tmp, "game.in")
os.mkfifo(fifo)
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-keys"),
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


def drawn():
    return bbox("WAYLAND-1"), bbox("SECOND")


def state():
    return json.loads(n.ctl("spatialoverview"))


def views():
    return {s["monitor"]: s["navigating"] for s in state()["screens"]}


def client(title):
    return next((c for c in n.clients() if c["title"] == title), None)


def boxes():
    return {c["title"]: (tuple(c["at"]), tuple(c["size"])) for c in n.clients()}


def others(b):
    return {k: v for k, v in b.items() if k != "x11-game"}


def land(monitor, title):
    n.dispatch(f'hl.dsp.focus({{monitor="{monitor}"}})'); time.sleep(0.3)
    n.dispatch(f'hl.plugin.spatialoverview.canvas("search {title}")'); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.2)


def at_100():
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(1.0)
    if any(views().values()):
        n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.0)


def roundtrip():
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.2)
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(1.2)


# -- situations: each leaves the game focused
def on_screen():
    at_100()
    for _ in range(4):   # the first search of a session can land before its results
        land("SECOND", "x11-game")
        if bbox("SECOND"):
            break
        at_100()
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + LW // 2}, y={LH // 2}}})"); time.sleep(0.3)


def across_seam():
    on_screen()
    b = bbox("SECOND")
    real = client("x11-game")
    n.dispatch(f'hl.dsp.window.move({{x={round(real["at"][0] - (b[0] + b[2]) / 2 / SCALE)}, y={real["at"][1]}, window="title:x11-game"}})'); time.sleep(0.8)
    # in front, with the pointer on it (hover focus gives focus to what is under it)
    n.dispatch('hl.dsp.focus({window="title:x11-game"})'); time.sleep(0.3)
    n.dispatch('hl.dsp.window.bring_to_top()'); time.sleep(0.3)
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + 20}, y={LH // 2}}})"); time.sleep(0.5)
    n.dispatch('hl.dsp.focus({window="title:x11-game"})'); time.sleep(0.3)


def off_screen():
    on_screen()
    for _ in range(3):
        n.dispatch('hl.plugin.spatialoverview.canvas("pan down")'); time.sleep(0.5)
    time.sleep(0.6)


def filled():
    on_screen()
    n.dispatch('hl.plugin.spatialoverview.canvas("fill")'); time.sleep(1.3)


def zoomed_out():
    on_screen()
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.2)
    n.keys("x11-game"); time.sleep(0.6)


SITUATIONS = {"on one screen": on_screen, "across the seam": across_seam, "off screen": off_screen, "filled": filled, "zoomed out": zoomed_out}

# -- keys: (name, dispatcher lua, what it should do to the game)
#    expect: "same"  nothing changes; "resized" its size changes;
#            "moved" its position changes; "any" only the general rules
KEYS = [
    # kept as Hyprland does them on the canvas
    ("SUPER + minus (resize)", 'hl.dsp.window.resize({ x = -100, y = 0, relative = true })', "resized"),
    ("SUPER + SHIFT + equal (resize)", 'hl.dsp.window.resize({ x = 0, y = 100, relative = true })', "resized"),
    ("CTRL + SUPER + F (tiled fullscreen)", 'hl.dsp.window.fullscreen_state({ internal = 0, client = 2 })', "same"),
    ("ALT + TAB reveal (bring to top)", 'hl.dsp.window.bring_to_top()', "same"),
    ("CTRL + ALT + TAB (focus monitor)", 'hl.dsp.focus({ monitor = "+1" })', "same"),
    ("SUPER + ALT + G (out of group)", 'hl.dsp.window.move({ out_of_group = true })', "any"),
    # bound to their canvas meaning (spatialoverview.lua)
    ("SUPER + J / P / L / Home, SUPER + G (tiling and grouping keys)", 'hl.plugin.spatialoverview.canvas("noop")', "same"),
    ("SUPER + O (pin)", 'hl.plugin.spatialoverview.canvas("pin")', "any"),
    ("SUPER + ALT + F (fill)", 'hl.plugin.spatialoverview.canvas("fill")', "any"),
]
ONLY = [k for k in os.environ.get("KEYS", "").split(",") if k]


def run(key, lua, expect, situation):
    label = f"[{situation}] {key}"
    b0, d0, v0 = boxes(), drawn(), views()
    focused = json.loads(n.ctl("-j", "activewindow")).get("title")
    try:
        n.dispatch(lua)
    except RuntimeError as err:
        print(f"  {label}: dispatcher said {err}")
    time.sleep(1.0)
    b1, d1, v1 = boxes(), drawn(), views()
    g0, g1 = b0.get("x11-game"), b1.get("x11-game")
    c = client("x11-game")
    print(f"  {label}: focused {focused!r}; game {g0} -> {g1}; drawn {d0} -> {d1}; views {v1}")
    check(n.proc.poll() is None, f"{label}: compositor alive")
    check(others(b0) == others(b1), f"{label}: no other window moved")
    check(c and c["floating"], f"{label}: the window stays floating on the canvas")
    check(len(set(v1.values())) <= 1, f"{label}: both screens in the same view")
    # (focusing the other monitor focuses a window there, and the camera goes
    # to it if it is not on screen: the game may leave the view)
    if situation != "off screen" and "focus({ monitor" not in lua:
        check(any(d1), f"{label}: the window is still drawn")
    check(focused == "x11-game", f"{label}: the game had focus")
    if expect == "same":
        check(g0 == g1, f"{label}: nothing changes")
    elif expect == "resized":
        check(g0 and g1 and g0[1] != g1[1], f"{label}: it resizes")
    # zooming out and back leaves it all as it was
    if situation != "zoomed out":
        before = boxes()
        roundtrip()
        check(boxes() == before, f"{label}: zooming out and back changes nothing")
    # undo what the key did, for the next one
    if "fullscreen_state" in lua:
        n.dispatch('hl.dsp.window.fullscreen_state({ internal = 0, client = 0 })'); time.sleep(0.5)
    if 'canvas("pin")' in lua or 'canvas("fill")' in lua:
        n.dispatch(lua); time.sleep(1.0)
    if "focus({ monitor" in lua:
        n.dispatch('hl.dsp.focus({window="title:x11-game"})'); time.sleep(0.3)


try:
    n.launch()
    n.ctl("output", "create", "wayland", "SECOND")
    time.sleep(0.8)
    n.ctl("reload")
    time.sleep(1.0)
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c '{ROOT}/.build/x11-game x11-game < {fifo}'") + ")")
    game_in = os.open(fifo, os.O_RDWR)
    for _ in range(100):
        if client("x11-game"):
            break
        time.sleep(0.1)
    time.sleep(1.0)
    # let the session's other windows finish opening (new ones are placed
    # where you look, over the game)
    last = None
    for _ in range(30):
        count = len(n.clients())
        if count == last:
            break
        last = count
        time.sleep(0.7)
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    at_100()
    for situation, setup in SITUATIONS.items():
        for key, lua, expect in KEYS:
            if ONLY and not any(o.lower() in key.lower() for o in ONLY):
                continue
            setup()
            run(key, lua, expect, situation)
            if n.proc.poll() is not None:
                break
    check(n.proc.poll() is None, "compositor alive at the end")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
