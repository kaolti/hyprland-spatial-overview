"""Windows on the canvas must get a frame every refresh, like on a plain
desktop: video pacing (and audio/video sync) depends on it. Measured with a
window that animates on every frame it is granted, where it really is and
drawn far from its real position (as the canvas shows windows).

usage: tests/framerate-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
"""
import json, os, re, subprocess, sys, tempfile, time, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("nav", os.path.join(HERE, "navigator-nested.py"))
nav = importlib.util.module_from_spec(spec); spec.loader.exec_module(nav)
ROOT = os.path.dirname(HERE)

log = tempfile.mktemp(prefix="fps-", suffix=".log")
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-fps"))
failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)


def land():
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(0.8)
    n.keys("fps-probe"); time.sleep(0.4)
    n.keys("-k", "Return"); time.sleep(1.0)


def measure(label):
    time.sleep(4.5)  # two full 2 s windows after things settle
    lines = [l for l in open(log).read().splitlines() if l.startswith("fps")] if os.path.exists(log) else []
    last = lines[-1] if lines else ""
    m = re.match(r"fps ([\d.]+) mean ([\d.]+) p95 ([\d.]+) max ([\d.]+)", last)
    fps, p95 = (float(m.group(1)), float(m.group(3))) if m else (0.0, 999.0)
    print(f"  [{label}] {last}")
    return fps, p95


try:
    n.launch()
    n.dispatch("hl.dsp.exec_cmd(" + json.dumps(f"sh -c 'python3 {HERE}/tools/fps-probe.py fps-probe > {log} 2>&1'") + ")")
    for _ in range(80):
        if any(c["title"] == "fps-probe" for c in n.clients()):
            break
        time.sleep(0.1)

    fps, p95 = measure("plain desktop, before the canvas")
    baseline = fps
    # The nested compositor gets its frames from the desktop it runs on; if
    # that desktop throttles it, nothing here can be measured.
    if baseline < 50:
        print(f"SKIP the nested session itself only gets {baseline:.0f} fps from the desktop it runs on; "
              "run this where its window gets every frame (a plain desktop, or a canvas with this fix)")
        sys.exit(0)

    land()
    fps, p95 = measure("canvas, where it really is")
    check(fps >= baseline * 0.9 and p95 < 25, f"on the canvas it gets every frame ({fps:.0f} fps, p95 gap {p95:.0f} ms; plain desktop {baseline:.0f} fps)")

    # The canvas shows windows away from their real position. Put this one's
    # real position off every monitor, then go to it on the canvas: its new
    # frames must still repaint the monitor that shows it, every refresh.
    n.dispatch('hl.dsp.window.move({x=2600, y=-900, window="title:fps-probe"})'); time.sleep(0.3)
    land()
    real = next(c for c in n.clients() if c["title"] == "fps-probe")
    n.shot("fps-far-away")
    fps, p95 = measure(f"canvas, real position {real['at']}")
    check(fps >= baseline * 0.9 and p95 < 25, f"drawn away from its real position it still gets every frame ({fps:.0f} fps, p95 gap {p95:.0f} ms)")
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
