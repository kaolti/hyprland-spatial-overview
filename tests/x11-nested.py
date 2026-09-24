"""X11 (XWayland) windows on the canvas: clicks must reach the app, and a menu
the app places itself (override-redirect, in root coordinates) must show up
where the app meant it, relative to its window as drawn, also when the
window's real position is far from where the canvas shows it.

usage: tests/x11-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
Set NESTED_SCALE=1.25 to test a scaled output (with force_zero_scaling, as
Omarchy sets it).
"""
import json, os, re, subprocess, sys, tempfile, time, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("nav", os.path.join(HERE, "navigator-nested.py"))
nav = importlib.util.module_from_spec(spec); spec.loader.exec_module(nav)
ROOT = os.path.dirname(HERE)
subprocess.run(["make", "-s", "-C", ROOT, "test-tools"], check=True)

SCALE = float(os.environ.get("NESTED_SCALE", "1"))
W, H = int(1280 / SCALE), int(720 / SCALE)   # logical size of the nested output
log = tempfile.mktemp(prefix="x11-menu-", suffix=".log")
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-x11"),
               extra_lua="\nhl.config({xwayland={force_zero_scaling=true}})\n")
failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)


def frame():
    raw = subprocess.run(["grim", "-t", "ppm", "-"], env=n.env(), capture_output=True, timeout=10).stdout
    parts = raw.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def bbox(image, rgb, tol=10):
    w, h, px = image
    xs, ys = [], []
    for y in range(0, h, 2):
        row = px[y * w * 3:(y + 1) * w * 3]
        for x in range(0, w, 2):
            if abs(row[x * 3] - rgb[0]) < tol and abs(row[x * 3 + 1] - rgb[1]) < tol and abs(row[x * 3 + 2] - rgb[2]) < tol:
                xs.append(x); ys.append(y)
    return (min(xs), min(ys), max(xs), max(ys)) if xs else None


def mouse(*cmds):
    subprocess.run([os.path.join(ROOT, ".build/vpointer"), str(W), str(H), *map(str, cmds)], env=n.env(), check=True, timeout=20)


def events():
    return open(log).read() if os.path.exists(log) else ""


def land():
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("x11-test"); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.5)


try:
    n.launch()
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c '{ROOT}/.build/x11-menu x11-test > {log}'") + ")")
    for _ in range(80):
        if any(c["title"] == "x11-test" for c in n.clients()):
            break
        time.sleep(0.1)
    time.sleep(0.5)

    for case, move in (("landed", None), ("far away", (2600, -900)), ("bottom edge", (300, 640))):
        if move:
            n.dispatch(f'hl.dsp.window.move({{x={move[0]}, y={move[1]}, window="title:x11-test"}})'); time.sleep(0.3)
        land()
        real = next(c for c in n.clients() if c["title"] == "x11-test")
        image = frame()
        win = bbox(image, (0x5a, 0x2a, 0x08))
        print(f"  [{case}] real {real['at']} {real['size']} drawn(px) {win}")
        if not win:
            check(False, f"[{case}] window drawn")
            continue
        # center of the drawn window, in logical coordinates
        cx, cy = int((win[0] + win[2]) / 2 / SCALE), int((win[1] + win[3]) / 2 / SCALE)
        before = events().count("press 1")
        mouse("abs", cx, cy, "sleep", 150, "down", "sleep", 60, "up", "sleep", 300)
        check(events().count("press 1") > before, f"[{case}] a left click reaches the X11 app")
        mouse("rdown", "sleep", 60, "rup", "sleep", 600)
        image = frame()
        menu = bbox(image, (0xff, 0x00, 0xff), tol=40)
        n.shot(f"x11-{case.replace(' ', '-')}")
        placed = re.findall(r"menu at root (-?\d+),(-?\d+)", events())
        print(f"  [{case}] menu drawn(px) {menu}, app placed it at root {placed[-1] if placed else None}")
        if menu:
            # With force_zero_scaling X11 apps work in screen pixels, so the
            # app's 40,60 is pixels whatever the scale.
            dx, dy = menu[0] - win[0], menu[1] - win[1]
            check(abs(dx - 40) <= 6 and abs(dy - 60) <= 6, f"[{case}] the menu opens 40,60 inside its window as drawn (got {dx:.0f},{dy:.0f})")
        else:
            check(False, f"[{case}] the menu is visible")
        mouse("abs", cx, cy, "sleep", 100, "down", "sleep", 60, "up", "sleep", 300)   # closes the menu
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
