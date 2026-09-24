"""Dragging a window by a title bar the app draws itself (Chromium's tab
strip) at 100% on the canvas: the window must follow the pointer, stop when
the button comes up, and leave no grab behind. Needs chromium; builds the
virtual mouse with `make test-tools`.

usage: tests/drag-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
"""
import json, os, shutil, subprocess, sys, tempfile, time, importlib.util

spec = importlib.util.spec_from_file_location("nav", "tests/navigator-nested.py")
nav = importlib.util.module_from_spec(spec); spec.loader.exec_module(nav)

if not shutil.which("chromium"):
    print("SKIP chromium not installed")
    sys.exit(0)
subprocess.run(["make", "-s", "test-tools"], check=True)
profile = tempfile.mkdtemp(prefix="chromium-drag-")
failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)

n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else ".build/dev/spatialoverview.so", ".build/shots-drag")


def mouse(*cmds):
    subprocess.run([".build/vpointer", "1280", "720", *map(str, cmds)], env=n.env(), check=True, timeout=20)


def chrome():
    return next((c for c in n.clients() if c["class"].lower().startswith("chromium")), None)


try:
    n.launch()
    page = os.path.join(profile, "page.html")
    open(page, "w").write("<title>drag test</title><body style='background:#103050'>")
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"chromium --ozone-platform=wayland --user-data-dir={profile} --no-first-run --no-default-browser-check "
                                               f"--password-store=basic --window-size=900,560 file://{page}") + ")")
    for _ in range(150):
        c = chrome()
        if c and c["title"].startswith("drag test"):
            break
        time.sleep(0.1)
    time.sleep(1.5)
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("drag test"); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.5)
    c = chrome()
    print("landed: real", c["at"], c["size"])
    n.shot("0-landed")

    # The window is drawn centered after landing; grab the tab strip's empty
    # stretch, right of the tabs.
    x0 = 640 - c["size"][0] // 2 + c["size"][0] - 120
    y0 = 360 - c["size"][1] // 2 + 20
    moves = sum([["rel", 12, 6, "sleep", 25] for _ in range(15)], [])
    after_moves = sum([["rel", 20, 0, "sleep", 25] for _ in range(8)], [])
    gesture = subprocess.Popen([".build/vpointer", "1280", "720", "abs", str(x0), str(y0), "sleep", "300", "down", "sleep", "200", *map(str, moves),
                                "sleep", "1500", "up", "sleep", "800", *map(str, after_moves), "sleep", "1500"], env=n.env())
    time.sleep(0.3 + 0.2 + 15 * 0.035 + 0.8)
    mid = chrome()
    print("cursor while dragging:", n.ctl("cursorpos"))
    n.shot("1-dragging")
    time.sleep(0.7 + 0.4)
    after = chrome()
    n.shot("2-released")
    time.sleep(0.4 + 8 * 0.035 + 1.0)
    later = chrome()
    n.shot("3-moved-after-release")
    gesture.wait(timeout=10)
    print("  before", c["at"], "after release", after["at"], "after moving on", later["at"])
    dx, dy = after["at"][0] - c["at"][0], after["at"][1] - c["at"][1]
    check(abs(dx - 180) <= 24 and abs(dy - 90) <= 24, f"the window followed the pointer ({dx},{dy} for a 180,90 drag)")
    check(later["at"] == after["at"], "the drag ended with the button (no grab left behind)")
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
