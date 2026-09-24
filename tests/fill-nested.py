"""SUPER + T on the canvas (canvas("fill")): the window fills the screen it is
on, below the bar, with the gap tiled windows keep on every side, on top of
other windows; again puts it back. Moved meanwhile, it keeps its new place
and gets its old size back. From the zoomed-out view it lands at 100% first.

usage: tests/fill-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
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
tmp = tempfile.mkdtemp(prefix="fill-")
fifo = os.path.join(tmp, "game.in")
os.mkfifo(fifo)
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-fill"),
               extra_lua='\nhl.config({xwayland={force_zero_scaling=true}, general={gaps_out=10, border_size=2}})\n'
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


def near(a, b, tol=4):
    return bool(a and b) and all(abs(x - y) <= tol for x, y in zip(a, b))


def client(title):
    return next((c for c in n.clients() if c["title"] == title), None)


def others():
    return {c["title"]: (tuple(c["at"]), tuple(c["size"])) for c in n.clients() if c["title"] != "x11-game"}


def land(monitor, title):
    n.dispatch(f'hl.dsp.focus({{monitor="{monitor}"}})'); time.sleep(0.3)
    n.dispatch(f'hl.plugin.spatialoverview.canvas("search {title}")'); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.2)


def fill():
    n.dispatch('hl.plugin.spatialoverview.canvas("fill")'); time.sleep(1.3)


try:
    n.launch()
    n.ctl("output", "create", "wayland", "SECOND")
    time.sleep(0.8)
    n.ctl("reload")
    time.sleep(1.0)
    for output in ("WAYLAND-1", "SECOND"):
        n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c 'LD_PRELOAD=/usr/lib/libgtk4-layer-shell.so python3 {HERE}/tools/layer-bar.py {output}'") + ")")
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c '{ROOT}/.build/x11-game x11-game < {fifo}'") + ")")
    game_in = os.open(fifo, os.O_RDWR)
    for _ in range(100):
        if client("x11-game"):
            break
        time.sleep(0.1)
    time.sleep(1.0)
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(0.8)

    mon = next(m for m in json.loads(n.ctl("-j", "monitors")) if m["name"] == "SECOND")
    top = mon["reserved"][1]                               # the bar, logical
    gap, border = 10, 2
    inset = gap + border
    AREA = (round(inset * SCALE), round((top + inset) * SCALE), 1280 - round(inset * SCALE), 720 - round(inset * SCALE))
    print("  bar", top, "px; expected filled content", AREA)

    land("SECOND", "x11-game")
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + LW // 2}, y={LH // 2}}})"); time.sleep(0.3)
    was, before_others = bbox("SECOND"), others()
    print("  landed on SECOND:", was, " WAYLAND-1:", bbox("WAYLAND-1"), " real:", client("x11-game")["at"], client("x11-game")["size"])

    # -- at 100%
    fill()
    got = bbox("SECOND")
    print("  filled:", got)
    check(near(got, AREA), f"fills its screen below the bar, {gap} px gap and border inside it on every side ({got})")
    check(others() == before_others, "no other window moved")
    fill()
    back = bbox("SECOND")
    check(near(back, was), f"again: exactly back ({back} vs {was})")

    # -- moved while filled: keeps its new place, gets its old size back
    fill()
    real = client("x11-game")
    n.dispatch(f'hl.dsp.window.move({{x={real["at"][0] - 100}, y={real["at"][1] + 40}, window="title:x11-game"}})'); time.sleep(0.8)
    moved = bbox("SECOND")
    fill()
    after = bbox("SECOND")
    print("  moved filled to", moved, "-> after SUPER + T", after)
    if moved and after:
        cx = ((moved[0] + moved[2]) / 2, (moved[1] + moved[3]) / 2)
        ca = ((after[0] + after[2]) / 2, (after[1] + after[3]) / 2)
        # the moved window may reach past the screen edge; compare what is on screen
        check(abs((after[2] - after[0]) - (was[2] - was[0])) <= 4 and abs((after[3] - after[1]) - (was[3] - was[1])) <= 4,
              "moved meanwhile: its old size back")
        check(abs(ca[0] - (cx[0] - 0)) <= 60 and abs(ca[1] - cx[1]) <= 60, "around where it was moved to, not back at the old place")
    else:
        check(False, "moved meanwhile: still on screen")

    # -- from the zoomed-out view: lands at 100% and fills
    land("SECOND", "x11-game")
    n.dispatch(f"hl.dsp.cursor.move({{x={LW + LW // 2}, y={LH // 2}}})"); time.sleep(0.3)
    was = bbox("SECOND")
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.2)
    fill()
    time.sleep(0.6)
    got = bbox("SECOND")
    n.shot("fill-from-zoomed-out")
    print("  zoomed out, SUPER + T:", got)
    check(near(got, AREA), f"zoomed out: SUPER + T lands and fills at 100% ({got})")
    screens = json.loads(n.ctl("spatialoverview"))["screens"]
    check(all(not x["navigating"] for x in screens), f"and every screen left the zoomed-out view ({screens})")
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.2)
    fill()
    time.sleep(0.6)
    got = bbox("SECOND")
    print("  zoomed out again, SUPER + T:", got)
    check(got and abs((got[2] - got[0]) - (was[2] - was[0])) <= 4, f"zoomed out again: back to its size, at 100% ({got})")
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
