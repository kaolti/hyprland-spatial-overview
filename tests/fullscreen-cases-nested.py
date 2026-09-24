"""Fullscreen on the canvas, the harder cases, on two monitors:
- fullscreen grows out of where the window is drawn;
- SUPER + F from the zoomed-out view leaves no screen zoomed out;
- leaving fullscreen while the other screen is zoomed out: this one joins it;
- a fullscreen game that hides its cursor (Wine's way) keeps the pointer on
  its screen; with the cursor shown, the pointer may leave;
- a fullscreen window on each screen: focus follows the pointer between them
  and SUPER + F only toggles the focused one.

usage: tests/fullscreen-cases-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
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
tmp = tempfile.mkdtemp(prefix="fscases-")
fifo = os.path.join(tmp, "game.in")
os.mkfifo(fifo)
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-fscases"),
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


def bbox(output, rgb=GAME, tol=12, step=2):
    w, h, px = frame(output)
    xs, ys = [], []
    for y in range(0, h, step):
        row = px[y * w * 3:(y + 1) * w * 3]
        for x in range(0, w, step):
            if abs(row[x * 3] - rgb[0]) < tol and abs(row[x * 3 + 1] - rgb[1]) < tol and abs(row[x * 3 + 2] - rgb[2]) < tol:
                xs.append(x); ys.append(y)
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1) if xs else None


def state():
    return json.loads(n.ctl("spatialoverview"))


def navigating():
    return {s["monitor"]: s["navigating"] for s in state()["screens"]}


def fullscreen():
    return {f["monitor"]: f["window"] for f in state()["fullscreen"]}


def client(title):
    return next((c for c in n.clients() if c["title"] == title), None)


def active():
    return json.loads(n.ctl("-j", "activewindow")).get("title")


def cursor():
    return tuple(float(v) for v in n.ctl("cursorpos").split(","))


def game(cmd):
    os.write(game_in, (cmd + "\n").encode())
    time.sleep(0.6)


def land(monitor, title):
    n.dispatch(f'hl.dsp.focus({{monitor="{monitor}"}})'); time.sleep(0.3)
    n.dispatch(f'hl.plugin.spatialoverview.canvas("search {title}")'); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.2)


def super_f():
    n.dispatch('hl.dsp.window.fullscreen({mode="fullscreen"})'); time.sleep(1.3)


def mouse(*cmds):
    subprocess.run([os.path.join(ROOT, ".build/vpointer"), "1", "1", *map(str, cmds)], env=n.env(), check=True, timeout=30)


try:
    n.launch()
    n.ctl("output", "create", "wayland", "SECOND")
    time.sleep(0.8)
    n.ctl("reload")
    time.sleep(1.0)
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c '{ROOT}/.build/x11-game x11-game < {fifo}'") + ")")
    game_in = os.open(fifo, os.O_RDWR)
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c 'python3 {HERE}/tools/wl-click.py wl-test'") + ")")
    for _ in range(100):
        if client("x11-game") and client("wl-test"):
            break
        time.sleep(0.1)
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(0.8)

    # -- fullscreen grows out of where the window is drawn: put it near the
    # top left of SECOND, then watch the first frames
    for _ in range(5):   # a fresh session may need a moment to draw it
        land("SECOND", "x11-game")
        b0 = bbox("SECOND")
        if b0:
            break
    real = client("x11-game")
    n.dispatch(f'hl.dsp.window.move({{x={round(real["at"][0] - (b0[0] - 60) / SCALE)}, y={round(real["at"][1] - (b0[1] - 60) / SCALE)}, window="title:x11-game"}})')
    time.sleep(0.8)
    b0 = bbox("SECOND")
    print("  drawn before fullscreen:", b0)
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + round((b0[0] + b0[2]) / 2 / SCALE)}, y={round((b0[1] + b0[3]) / 2 / SCALE)}}})"); time.sleep(0.3)
    n.dispatch('hl.dsp.window.fullscreen({mode="fullscreen"})')
    growth = []
    t0 = time.time()
    while time.time() - t0 < 0.9:
        growth.append(bbox("SECOND", step=4))
    time.sleep(0.8)
    print("  while growing:", growth[:8])
    between = [b for b in growth if b and b != b0 and not (b[0] <= 4 and b[1] <= 4 and b[2] >= 1276 and b[3] >= 716)]
    if between:
        check(all(b[0] <= b0[0] + 8 and b[1] <= b0[1] + 8 and b[0] >= -2 and b[1] >= -2 for b in between),
              "fullscreen grows out of where the window was drawn (no frame starts elsewhere)")
    else:
        print("  (no in-between frame captured; the growth check could not look)")
    check(fullscreen().get("SECOND") == "x11-game", "the game is fullscreen on SECOND")
    super_f()
    check(not fullscreen(), "SUPER + F again: out of fullscreen")
    back = bbox("SECOND")
    check(back and all(abs(a - b) <= 4 for a, b in zip(back, b0)), f"and back where it was ({back} vs {b0})")

    # -- SUPER + F from the zoomed-out view
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.2)
    check(all(navigating().values()), f"zoomed out on both screens ({navigating()})")
    n.keys("x11-game"); time.sleep(0.6)
    check(active() == "x11-game", "searching selects (and focuses) the game")
    super_f()
    print("  after SUPER + F zoomed out:", state())
    check(fullscreen().get("SECOND") == "x11-game", "SUPER + F from the zoomed-out view: the game goes fullscreen")
    check(not any(navigating().values()), f"and no screen is left zoomed out ({navigating()})")

    # -- leaving fullscreen while the other screen is zoomed out: it joins
    n.dispatch('hl.plugin.spatialoverview.overview("toggle WAYLAND-1")'); time.sleep(1.2)
    check(navigating().get("WAYLAND-1"), "WAYLAND-1 alone zoomed out")
    n.dispatch('hl.dsp.focus({window="title:x11-game"})'); time.sleep(0.3)
    super_f()
    print("  after leaving fullscreen:", navigating())
    check(not fullscreen() and all(navigating().values()), f"leaving fullscreen, SECOND joins the zoomed-out view ({navigating()})")
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(1.2)
    check(not any(navigating().values()), f"and both come back together ({navigating()})")

    # -- a game hiding its cursor keeps the pointer on its screen
    land("SECOND", "x11-game")
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + LW // 2}, y={LH // 2}}})"); time.sleep(0.3)
    super_f()
    game("hidecursor")
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + 40}, y={LH // 2}}})"); time.sleep(0.2)
    mouse(*sum([["rel", -20, 0, "sleep", 10] for _ in range(20)], []), "sleep", 200)
    pos = cursor()
    print("  cursor hidden, pointer pushed left:", pos, "active", active())
    check(pos[0] >= LW - 1, "with its cursor hidden, the pointer stays on the game's screen")
    check(active() == "x11-game", "and the game keeps focus")
    game("showcursor")
    mouse(*sum([["rel", -20, 0, "sleep", 10] for _ in range(20)], []), "sleep", 300)
    pos = cursor()
    print("  cursor shown, pointer pushed left:", pos)
    check(pos[0] < LW, "with its cursor shown, the pointer may leave")

    # -- a fullscreen window on each screen (focusing wl-test directly: the
    # search would zoom out, which takes the game's screen back)
    n.dispatch(f"hl.dsp.cursor.move({{x={LW // 2}, y={LH // 2}}})"); time.sleep(0.3)
    n.dispatch('hl.dsp.focus({window="title:wl-test"})'); time.sleep(0.6)
    print("  focused wl-test?", active(), [(c["title"], c["at"], c["workspace"]["name"], c["monitor"], c["fullscreen"]) for c in n.clients() if c["title"] in ("wl-test", "x11-game")],
          [(m["name"], m["activeWorkspace"]["name"], m["focused"]) for m in json.loads(n.ctl("-j", "monitors"))])
    check(fullscreen().get("SECOND") == "x11-game", "the game is still fullscreen on SECOND")
    check(active() == "wl-test", "wl-test has focus")
    super_f()
    print("  both fullscreen:", fullscreen())
    check(fullscreen() == {"SECOND": "x11-game", "WAYLAND-1": "wl-test"}, "a fullscreen window on each screen")
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + LW // 2}, y={LH // 2}}})"); time.sleep(0.2)
    mouse("rel", 3, 0, "sleep", 300)
    check(active() == "x11-game", f"the pointer on SECOND: the game has focus ({active()})")
    super_f()
    check(fullscreen() == {"WAYLAND-1": "wl-test"}, f"SUPER + F there: only the game leaves fullscreen ({fullscreen()})")
    n.dispatch(f"hl.dsp.cursor.move({{x={LW // 2}, y={LH // 2}}})"); time.sleep(0.2)
    mouse("rel", 3, 0, "sleep", 300)
    check(active() == "wl-test", f"the pointer on WAYLAND-1: wl-test has focus ({active()})")
    super_f()
    check(not fullscreen(), f"SUPER + F there: it leaves too ({fullscreen()})")
    check(len(state()["screens"]) == 2, "both screens have their canvas back")
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
