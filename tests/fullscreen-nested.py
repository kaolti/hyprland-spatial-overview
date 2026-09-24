"""Fullscreen on the canvas, with two monitors. A window that goes fullscreen
fills the screen the canvas shows it on; only that screen's canvas steps
aside, nothing else moves, the bar (top layer) is hidden under it, keys go
to it, and after fullscreen the window is back where it was. Covers a Wine-like X11 game going "windowed fullscreen" (borderless,
covering its monitor) and back, Super+F on a Wayland window, Super+T (fill
the screen) and X11 positions while the navigator is zoomed out.

usage: tests/fullscreen-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
Set NESTED_SCALE=1.25 to test scaled outputs (with force_zero_scaling, as
Omarchy sets it; X11 apps then work in pixels).
"""
import json, os, re, subprocess, sys, tempfile, time, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("nav", os.path.join(HERE, "navigator-nested.py"))
nav = importlib.util.module_from_spec(spec); spec.loader.exec_module(nav)
ROOT = os.path.dirname(HERE)
subprocess.run(["make", "-s", "-C", ROOT, "test-tools"], check=True)

GAME = (0x2a, 0x5a, 0x08)
SCALE = float(os.environ.get("NESTED_SCALE", "1"))
LW, LH = round(1280 / SCALE), round(720 / SCALE)   # logical size of each output
tmp = tempfile.mkdtemp(prefix="fullscreen-")
fifo, log, wl_log = os.path.join(tmp, "game.in"), os.path.join(tmp, "game.log"), os.path.join(tmp, "wl.log")
BAR = (0xff, 0x00, 0xff)
os.mkfifo(fifo)
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-fullscreen"),
               extra_lua='\nhl.config({xwayland={force_zero_scaling=true}})\n'
                         f'hl.monitor({{ output = "WAYLAND-1", mode = "1280x720@60", position = "0x0", scale = {SCALE} }})\n'
                         f'hl.monitor({{ output = "SECOND", mode = "1280x720@60", position = "{LW}x0", scale = {SCALE} }})\n')
failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)


def frame(output, cursor=False):
    raw = subprocess.run(["grim", *(["-c"] if cursor else []), "-o", output, "-t", "ppm", "-"], env=n.env(), capture_output=True, timeout=10).stdout
    parts = raw.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def bbox(image, rgb=GAME, tol=12):
    w, h, px = image
    xs, ys = [], []
    for y in range(0, h, 2):
        row = px[y * w * 3:(y + 1) * w * 3]
        for x in range(0, w, 2):
            if abs(row[x * 3] - rgb[0]) < tol and abs(row[x * 3 + 1] - rgb[1]) < tol and abs(row[x * 3 + 2] - rgb[2]) < tol:
                xs.append(x); ys.append(y)
    return (min(xs), min(ys), max(xs) + 1, max(ys) + 1) if xs else None


def changed(a, b):
    """Share of pixels that differ noticeably between two frames."""
    (w, h, pa), (_, _, pb) = a, b
    diff = total = 0
    for i in range(0, min(len(pa), len(pb)) - 3, 3 * 7):
        total += 1
        diff += abs(pa[i] - pb[i]) + abs(pa[i + 1] - pb[i + 1]) + abs(pa[i + 2] - pb[i + 2]) > 30
    return diff / max(1, total)


def game(cmd):
    os.write(game_in, (cmd + "\n").encode())
    time.sleep(1.2)


def game_state():
    lines = [l for l in open(log).read().splitlines() if l.startswith("at ")] if os.path.exists(log) else []
    m = re.match(r"at (-?\d+) (-?\d+) (\d+) (\d+) monitor (-?\d+) fullscreen (\d)", lines[-1]) if lines else None
    return tuple(map(int, m.groups())) if m else None


def bar_shown(output):
    """Whether the magenta test bar is drawn along the top of an output."""
    w, h, px = frame(output)
    hits = sum(1 for x in range(0, w, 8) if abs(px[(10 * w + x) * 3] - 0xff) < 30 and px[(10 * w + x) * 3 + 1] < 40 and abs(px[(10 * w + x) * 3 + 2] - 0xff) < 30)
    return hits > (w // 8) * 0.5


def game_cursor_at(output, x, y):
    """Whether the game's own cursor (a solid cyan square) is drawn around
    pixel x, y of an output, rather than the canvas's arrow."""
    w, h, px = frame(output, cursor=True)
    hits = sum(1 for dy in range(-6, 7, 3) for dx in range(-6, 7, 3)
               if px[((y + dy) * w + x + dx) * 3] < 60 and px[((y + dy) * w + x + dx) * 3 + 1] > 190 and px[((y + dy) * w + x + dx) * 3 + 2] > 190)
    return hits >= 12


def typed(path, text, x11=False):
    """Whether every key of `text` reached the window logging to `path`. (An
    X11 app sees wtype's keys under its own keymap, so there only count.)"""
    mark = len(open(path).read()) if os.path.exists(path) else 0
    n.keys(text); time.sleep(0.5)
    got = open(path).read()[mark:] if os.path.exists(path) else ""
    ok = got.count("key ") >= len(text) if x11 else all(f"key {ch}" in got for ch in text)
    return ok, [l for l in got.splitlines() if l.startswith("key ")]


def client(title):
    return next((c for c in n.clients() if c["title"] == title), None)


def others():
    return {c["title"]: (tuple(c["at"]), tuple(c["size"]), c["floating"]) for c in n.clients() if c["title"] not in ("x11-game", "wl-test")}


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
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c '{ROOT}/.build/x11-game x11-game < {fifo} > {log}'") + ")")
    game_in = os.open(fifo, os.O_RDWR)
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c 'python3 {HERE}/tools/wl-click.py wl-test > {wl_log}'") + ")")
    for output in ("WAYLAND-1", "SECOND"):
        n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c 'LD_PRELOAD=/usr/lib/libgtk4-layer-shell.so python3 {HERE}/tools/layer-bar.py {output}'") + ")")
    for _ in range(100):
        if {"x11-game", "wl-test"} <= {c["title"] for c in n.clients()}:
            break
        time.sleep(0.1)
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(0.8)
    land("WAYLAND-1", "wl-test")
    land("SECOND", "x11-game")
    game("report")
    start = game_state()
    print("  game (X11 view):", start, " real:", client("x11-game")["at"], "monitor", client("x11-game")["monitor"])
    check(start and start[4] == 1, "the game thinks it is on the monitor that shows it (SECOND)")
    check(bar_shown("WAYLAND-1") and bar_shown("SECOND"), "the bar shows on both screens")

    # -- windowed fullscreen, the Wine way
    before_left, before_others = frame("WAYLAND-1"), others()
    game("borderless")
    c, state = client("x11-game"), game_state()
    print("  borderless: X11 view", state, " hyprland", c["at"], c["size"], "fullscreen", c["fullscreen"], "monitor", c["monitor"])
    n.shot("borderless")
    check(c["fullscreen"] != 0, "borderless covering its monitor becomes fullscreen")
    check(state and state[:4] == (1280, 0, 1280, 720) and state[5] == 1, "the game covers SECOND exactly, as it asked, and stays fullscreen")
    right = bbox(frame("SECOND"))
    check(right and all(abs(a - b) <= 2 for a, b in zip(right, (0, 0, 1280, 720))), f"it fills the SECOND screen (drawn {right})")
    check(changed(before_left, frame("WAYLAND-1")) < 0.02, "the other screen keeps its canvas as it was")
    check(others() == before_others, "no other window moved")
    check(not bar_shown("SECOND"), "the bar is hidden under the fullscreen game")
    check(bar_shown("WAYLAND-1"), "the other screen keeps its bar")
    ok, got = typed(log, "qwe", x11=True)
    check(ok, f"keys go to the fullscreen game (it got {got})")

    game("windowed 800 450")
    c, state = client("x11-game"), game_state()
    right = bbox(frame("SECOND"))
    print("  windowed: X11 view", state, " hyprland", c["at"], c["size"], "fullscreen", c["fullscreen"], " drawn", right)
    n.shot("windowed")
    check(c["fullscreen"] == 0 and state and state[5] == 0, "back to windowed")
    check(state and state[4] == 1 and state[2:4] == (800, 450), "still 800x450 on SECOND in its own view")
    check(right and abs((right[2] - right[0]) - 800) <= 4 and abs((right[3] - right[1]) - 450) <= 4 and abs(right[0] - 240) <= 6 and abs(right[1] - 135) <= 6,
          f"drawn centered on SECOND, where it asked to be (drawn {right})")
    check(changed(before_left, frame("WAYLAND-1")) < 0.02, "the other screen still unchanged")
    check(others() == before_others, "still no other window moved")
    check(bar_shown("SECOND"), "the bar is back")
    check(json.loads(n.ctl("-j", "activewindow")).get("title") == "x11-game", "the game keeps the keyboard")

    # -- the game's own cursor; SUPER + F on it, then it flashes for attention
    # (a Wine game does when combat starts)
    land("SECOND", "x11-game")
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + LW // 2}, y={LH // 2}}})"); time.sleep(0.6)
    check(game_cursor_at("SECOND", 640, 360), "the game's own cursor shows on the canvas")
    was_game = bbox(frame("SECOND"))
    n.dispatch('hl.dsp.window.fullscreen({mode="fullscreen"})'); time.sleep(1.2)
    check(client("x11-game")["fullscreen"] != 0, "SUPER + F: the game goes fullscreen")
    game("attention")
    c = client("x11-game")
    print("  after attention:", c["at"], c["size"], "fullscreen", c["fullscreen"], c["fullscreenClient"])
    check(c["fullscreen"] != 0, "flashing for attention keeps it fullscreen")
    check(json.loads(n.ctl("-j", "activewindow")).get("title") == "x11-game", "and the keyboard")
    check(game_cursor_at("SECOND", 640, 360), "fullscreen, the game's own cursor shows")
    n.dispatch('hl.dsp.window.fullscreen({mode="fullscreen"})'); time.sleep(1.2)
    back = bbox(frame("SECOND"))
    check(client("x11-game")["fullscreen"] == 0 and back and was_game and all(abs(a - b) <= 4 for a, b in zip(back, was_game)),
          f"SUPER + F again: back where it was (drawn {back}, was {was_game})")

    # -- Super+F on a Wayland window, on the other screen
    wl_before = client("wl-test")
    # (go to it on its screen; focusing it from outside the canvas would move
    # the camera of the screen its real position belongs to)
    land("WAYLAND-1", "wl-test")
    n.dispatch(f"hl.dsp.cursor.move({{x={LW // 2}, y={LH // 2}}})"); time.sleep(0.3)   # the pointer where you work
    left_before = frame("WAYLAND-1")
    second_before = next(x for x in json.loads(n.ctl("spatialoverview"))["screens"] if x["monitor"] == "SECOND")
    n.dispatch('hl.dsp.window.fullscreen({mode="fullscreen"})'); time.sleep(1.2)
    c = client("wl-test")
    print("  wl-test fullscreen:", c["at"], c["size"], c["fullscreen"], "monitor", c["monitor"])
    check(c["fullscreen"] != 0 and tuple(c["size"]) == (LW, LH) and tuple(c["at"]) == (0, 0), "Super+F fills the screen it is shown on (WAYLAND-1)")
    # (the game there loses focus, so compare the canvas state, not pixels)
    second = next((x for x in json.loads(n.ctl("spatialoverview"))["screens"] if x["monitor"] == "SECOND"), None)
    check(second and second["view"] == second_before["view"], f"SECOND keeps its canvas, as it was ({second} vs {second_before})")
    check(not bar_shown("WAYLAND-1"), "Super+F covers the bar")
    ok, got = typed(wl_log, "asd")
    check(ok, f"keys go to the fullscreen window (it got {got})")

    # -- Super+Ctrl+G while fullscreen: the canvas takes the screen back, the
    # window is on it, and going back to it makes it fullscreen again
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.5)
    n.shot("superctrlg-over-fullscreen")
    c = client("wl-test")
    check(c["fullscreen"] == 0, "Super+Ctrl+G: the window leaves fullscreen and is on the canvas")
    w, h, px = frame("WAYLAND-1")   # (zoomed out, the lens bends the bar)
    magenta = sum(1 for y in range(0, h // 8, 2) for x in range(0, w, 8)
                  if px[(y * w + x) * 3] > 0xd0 and px[(y * w + x) * 3 + 1] < 0x40 and px[(y * w + x) * 3 + 2] > 0xd0)
    check(magenta > 40, "with the bar back")
    n.keys("wl-test"); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.5)
    c = client("wl-test")
    check(c["fullscreen"] != 0 and tuple(c["at"]) == (0, 0), "going back to it makes it fullscreen again")
    check(not bar_shown("WAYLAND-1"), "over the bar")
    ok, got = typed(wl_log, "fgh")
    check(ok, f"with the keyboard (it got {got})")
    n.dispatch('hl.dsp.window.fullscreen({mode="fullscreen"})'); time.sleep(1.2)
    c = client("wl-test")
    print("  wl-test after:", c["at"], c["size"], c["fullscreen"], "monitor", c["monitor"], " before:", wl_before["at"], wl_before["size"])
    n.shot("after-super-f")
    check(c["fullscreen"] == 0 and tuple(c["at"]) == tuple(wl_before["at"]) and tuple(c["size"]) == tuple(wl_before["size"]), "after fullscreen it is back where it was")
    check(json.loads(n.ctl("-j", "activewindow")).get("title") == "wl-test", "and it keeps the keyboard")
    left_after = frame("WAYLAND-1")
    if changed(left_before, left_after) >= 0.02:
        for name, image in (("left-before", left_before), ("left-after", left_after)):
            w, h, px = image
            open(os.path.join(ROOT, ".build/shots-fullscreen", name + ".ppm"), "wb").write(b"P6\n%d %d\n255\n" % (w, h) + px)
    check(changed(left_before, left_after) < 0.02, f"and WAYLAND-1 shows the canvas as before ({changed(left_before, left_after):.3f} changed)")
    check(bar_shown("WAYLAND-1"), "with its bar")

    # -- Super+T: fill the screen, and back
    land("SECOND", "x11-game")
    was = bbox(frame("SECOND"))
    n.dispatch('hl.plugin.spatialoverview.canvas("fill")'); time.sleep(1.0)
    filled, state = bbox(frame("SECOND")), game_state()
    bar = next(m for m in json.loads(n.ctl("-j", "monitors")) if m["name"] == "SECOND")["reserved"][1]      # logical
    gap = int(n.ctl("getoption", "general:gaps_out").split(":")[1].split()[0])
    border = int(n.ctl("getoption", "general:border_size").split()[1])
    inset = gap + border
    area = (round(inset * SCALE), round((bar + inset) * SCALE), 1280 - round(inset * SCALE), 720 - round(inset * SCALE))
    print("  fill: drawn", filled, " X11 view", state, " expected", area)
    check(filled and all(abs(a - b) <= 3 for a, b in zip(filled, area)), f"fill: the window fills its screen below the bar, with the gaps (drawn {filled})")
    check(state and all(abs(a - b) <= 3 for a, b in zip(state[:4], (1280 + area[0], area[1], area[2] - area[0], area[3] - area[1]))), "fill: the app sees exactly that")
    n.dispatch('hl.plugin.spatialoverview.canvas("fill")'); time.sleep(1.0)
    back = bbox(frame("SECOND"))
    check(back and was and all(abs(a - b) <= 4 for a, b in zip(back, was)), f"fill again: back as it was ({back} vs {was})")
    check(others() == before_others, "fill moved no other window")

    # -- zoomed out, an X11 app keeps its position
    game("report"); before = game_state()
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.2)
    game("report"); zoomed = game_state()
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(1.0)
    game("report"); after = game_state()
    print("  X11 view: at 100%", before, " zoomed out", zoomed, " back", after)
    check(zoomed and before and zoomed[:5] == before[:5], "zoomed out, the X11 app keeps its position and monitor")
    check(after and before and after[:5] == before[:5], "back at 100%, the same again")
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
