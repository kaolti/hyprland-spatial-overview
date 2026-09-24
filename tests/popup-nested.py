"""Popups on the canvas: a menu must open where the app put it, relative to
its window as drawn, and fade in fully, also when the canvas camera is not at
the window's home.

usage: tests/popup-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
"""
import json, os, subprocess, sys, tempfile, time, importlib.util

spec = importlib.util.spec_from_file_location("nav", os.path.join(os.path.dirname(__file__), "navigator-nested.py"))
nav = importlib.util.module_from_spec(spec); spec.loader.exec_module(nav)

HERE = os.path.dirname(os.path.abspath(__file__))
trigger = tempfile.mkdtemp(prefix="popup-trigger-")
# Omarchy blurs popups and ignores their faint pixels; mirror that.
BLUR = "\nhl.config({decoration={blur={enabled=true, popups=true, popups_ignorealpha=0.2}}})\n"
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else ".build/dev/spatialoverview.so", ".build/shots-popup", extra_lua=BLUR)
failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)


def frame():
    """The nested output as (width, height, rgb bytes)."""
    raw = subprocess.run(["grim", "-t", "ppm", "-"], env=n.env(), capture_output=True, timeout=10).stdout
    parts = raw.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]


def bbox(image, rgb, tol=40):
    w, h, px = image
    xs, ys, count = [], [], 0
    for y in range(0, h, 2):
        row = px[y * w * 3:(y + 1) * w * 3]
        for x in range(0, w, 2):
            r, g, b = row[x * 3], row[x * 3 + 1], row[x * 3 + 2]
            if abs(r - rgb[0]) < tol and abs(g - rgb[1]) < tol and abs(b - rgb[2]) < tol:
                xs.append(x); ys.append(y); count += 1
    return (min(xs), min(ys), max(xs), max(ys), count) if xs else None


def brightest_magenta(image, box):
    """How opaque the popup looks: its peak red+blue over the window blue."""
    w, h, px = image
    best = 0
    for y in range(box[1], box[3] + 1, 2):
        for x in range(box[0], box[2] + 1, 2):
            i = (y * w + x) * 3
            best = max(best, min(px[i], px[i + 2]) - px[i + 1])
    return best


def popup(action):
    open(os.path.join(trigger, action), "w").close()


try:
    n.launch()
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"python3 {HERE}/popup-app.py {trigger}") + ")")
    for _ in range(80):
        app = next((c for c in n.clients() if c["title"] == "popup-test"), None)
        if app:
            break
        time.sleep(0.1)
    check(app is not None, "popup test window mapped")

    # Land on the test window at 100%: the camera now sits wherever the
    # window lives in the world, not at the monitor's home.
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("popup-test"); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.2)
    print("window at", app["at"], "size", app["size"], "monitor", json.loads(n.ctl("-j", "monitors"))[0]["width"])

    # Cases: the camera moved away from the window's home, and the window
    # living (really) far off this monitor or right at its bottom edge, which
    # is normal on a large canvas.
    cases = [("landed", None, None), ("pan down", "down", None), ("pan right", "right", None),
             ("far away", None, (2600, -900)), ("bottom edge", None, (300, 640)), ("far left", None, (-3000, 200))]
    for attempt, (name, pan, move) in enumerate(cases):
        if pan:
            n.dispatch(f'hl.plugin.spatialoverview.canvas("pan {pan}")')
            time.sleep(0.8)
        if move:
            n.dispatch(f'hl.dsp.window.move({{x={move[0]}, y={move[1]}, window="title:popup-test"}})'); time.sleep(0.3)
            n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
            n.keys("popup-test"); time.sleep(0.4)
            n.keys("-k", "Return"); time.sleep(1.2)
        pan = name
        popup("open"); time.sleep(1.0)
        image = frame()
        n.shot(f"popup-{attempt}")
        win = bbox(image, (0x10, 0x30, 0x50), tol=12)
        pop = bbox(image, (0xff, 0x00, 0xff), tol=60)
        real = next((c for c in n.clients() if c["title"] == "popup-test"), {})
        print(f"  [{pan}] drawn window {win[:4] if win else None} real {real.get('at')} {real.get('size')} popup {pop[:4] if pop else None}")
        check(pop is not None, f"[{pan}] popup visible")
        if win and pop:
            # Anchored ~80,60 px into the window and opening downward.
            below = win[1] + 50 <= pop[1] <= win[1] + 150
            beside = win[0] - 10 <= pop[0] <= win[0] + 200
            check(below and beside, f"[{pan}] popup opens under its anchor in the drawn window (offset {pop[0] - win[0]},{pop[1] - win[1]})")
            check(brightest_magenta(image, pop) > 200, f"[{pan}] popup is fully opaque (peak {brightest_magenta(image, pop)})")
        popup("close"); time.sleep(0.8)
    # Drawn near the bottom of the screen, a downward menu must flip or slide
    # to stay fully visible, judged by the screen as drawn.
    for _ in range(12):
        image = frame()
        win = bbox(image, (0x10, 0x30, 0x50), tol=12)
        if win and win[1] >= 560:
            break
        n.dispatch('hl.plugin.spatialoverview.canvas("pan up")'); time.sleep(0.5)
    popup("open"); time.sleep(1.0)
    image = frame()
    n.shot("popup-drawn-bottom")
    win = bbox(image, (0x10, 0x30, 0x50), tol=12)
    pop = bbox(image, (0xff, 0x00, 0xff), tol=60)
    print(f"  [drawn at bottom] drawn window {win[:4] if win else None} popup {pop[:4] if pop else None}")
    check(pop is not None and pop[3] <= image[1] - 1 and pop[3] - pop[1] >= 130, "[drawn at bottom] menu stays fully on screen")
    popup("close"); time.sleep(0.5)
    # A menu hanging below its window: the pointer over that part must reach
    # the menu (its button turns green under the pointer), not whatever lies
    # beneath. Clicks take the same route.
    low = tempfile.mkdtemp(prefix="popup-low-")
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"python3 {HERE}/popup-app.py {low} low") + ")")
    for _ in range(80):
        if any(c["title"] == "popup-low" for c in n.clients()):
            break
        time.sleep(0.1)
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("popup-low"); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.2)
    n.dispatch('hl.plugin.spatialoverview.canvas("pan down")'); time.sleep(0.8)
    open(os.path.join(low, "open"), "w").close(); time.sleep(1.0)
    image = frame()
    win = bbox(image, (0x5a, 0x2a, 0x08), tol=10)
    button = bbox(image, (0x0a, 0x5a, 0x3a), tol=10)
    print(f"  [hanging] window {win[:4] if win else None} button {button[:4] if button else None}")
    check(bool(win and button and button[1] >= win[3]), "[hanging] the menu's button hangs below its window")
    if button:
        cx, cy = (button[0] + button[2]) // 2, (button[1] + button[3]) // 2
        n.dispatch(f"hl.dsp.cursor.move({{x={cx}, y={cy}}})"); time.sleep(0.2)
        n.dispatch(f"hl.dsp.cursor.move({{x={cx + 3}, y={cy + 2}}})"); time.sleep(0.6)
        n.shot("popup-hanging-hover")
        check(bbox(frame(), (0x00, 0xff, 0x00), tol=60) is not None, "[hanging] the pointer below the window reaches the menu")
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
