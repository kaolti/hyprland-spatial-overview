"""Arrow keys pick the window in the direction pressed, from where the windows
are now: Super + arrows at 100% (navigate) and the arrows in the zoomed-out
view, on two linked screens.
- a layout with the awkward cases: a wide window with a neighbour below it
  that shares its left edge (not to its left), a small window beside its top
  (not above it), a row that continues on the other screen;
- after moving windows around, the arrows follow the new places;
- after tidying up (canvas("arrange")), every press goes to a window that is
  further that way with both edges and within 45° of that side, and a window
  in line that way is always reachable.

usage: tests/arrows-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
Set NESTED_SCALE=1.25 to test scaled outputs.
"""
import json, os, subprocess, sys, time, importlib.util

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("nav", os.path.join(HERE, "navigator-nested.py"))
nav = importlib.util.module_from_spec(spec); spec.loader.exec_module(nav)
ROOT = os.path.dirname(HERE)

SCALE = float(os.environ.get("NESTED_SCALE", "1"))
LW, LH = round(1280 / SCALE), round(720 / SCALE)
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, ".build/dev/spatialoverview.so"), os.path.join(ROOT, ".build/shots-arrows"),
               extra_lua='\nhl.config({xwayland={force_zero_scaling=true}})\n'
                         f'hl.monitor({{ output = "WAYLAND-1", mode = "1280x720@60", position = "0x0", scale = {SCALE} }})\n'
                         f'hl.monitor({{ output = "SECOND", mode = "1280x720@60", position = "{LW}x0", scale = {SCALE} }})\n')
failures = []

# short names for the harness's windows (title regexes)
# (Hyprland matches a window's whole title)
W = {"wide": "scrollOverview.*", "below": "user@arch.*", "beside": "btop", "far": "Hacker News.*", "under": "general.*",
     "above": "Napi jegyzet.*", "middle": "Spotify.*"}
DIRS = ("left", "right", "up", "down")


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)


def state():
    return json.loads(n.ctl("spatialoverview"))


def name_of(title):
    import re
    return next((k for k, rx in W.items() if re.fullmatch(rx, title or "")), title)


def boxes():
    return {name_of(c["title"]): (c["at"][0], c["at"][1], c["size"][0], c["size"][1]) for c in n.clients()}


def place(name, x, y, w, h):
    # sizes and places in the world, in screen-width units so the layout is
    # the same at any scale
    n.dispatch(f'hl.dsp.window.resize({{x={round(w * LW)}, y={round(h * LH)}, window="title:{W[name]}"}})')
    n.dispatch(f'hl.dsp.window.move({{x={round(x * LW)}, y={round(y * LH)}, window="title:{W[name]}"}})')


def focus(name):
    n.dispatch(f'hl.dsp.focus({{window="title:{W[name]}"}})'); time.sleep(0.4)


def focused():
    return name_of(n.active().get("title", ""))


def press(direction, zoomed):
    if zoomed:
        n.keys("-k", direction.capitalize())
    else:
        n.dispatch(f'hl.plugin.spatialoverview.navigate("{direction}")')
    time.sleep(0.5)
    return focused()


def in_view(box):
    x, y, w, h = box
    for s in state()["screens"]:
        vx, vy, vw, vh = s["view"]
        if x < vx + vw and x + w > vx and y < vy + vh and y + h > vy:
            return True
    return False


def along(box, d):
    """(near edge, far edge, across min, across max), flipped so d points up."""
    x, y, w, h = box
    lo, hi, a0, a1 = (x, x + w, y, y + h) if d in ("left", "right") else (y, y + h, x, x + w)
    return (lo, hi, a0, a1) if d in ("right", "down") else (-hi, -lo, a0, a1)


def that_way(src, dst, d):
    """dst reaches further in d with both edges, and some of it is within 45°."""
    s, t = along(src, d), along(dst, d)
    if t[0] <= s[0] or t[1] <= s[1]:
        return False
    aside = max(0, t[2] - s[3], s[2] - t[3])
    return aside < t[1] - s[1]


def in_line_beyond(src, dst, d):
    s, t = along(src, d), along(dst, d)
    return t[0] >= s[1] and t[3] > s[2] and t[2] < s[3]


def zoom_out():
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.3)


def back_to_100():
    n.keys("-k", "Escape"); time.sleep(0.3)
    n.keys("-k", "Escape"); time.sleep(1.2)
    if any(s["navigating"] for s in state()["screens"]):
        zoom_out()


try:
    n.launch()
    n.ctl("output", "create", "wayland", "SECOND")
    time.sleep(0.8)
    n.ctl("reload")
    time.sleep(1.5)
    zoom_out()
    back_to_100()

    # -- the layout: two rows across both screens, one window above
    #   above
    #   wide ............ beside ......... | far (second screen)
    #   below ....... middle ............. | under
    place("wide", 0.08, 0.15, 0.55, 0.35)
    place("beside", 0.72, 0.15, 0.12, 0.10)     # beside the wide one's top, not above it
    place("far", 1.20, 0.18, 0.40, 0.35)        # the same row, on the second screen
    place("below", 0.08, 0.60, 0.25, 0.30)      # below the wide one, sharing its left edge
    place("middle", 0.75, 0.60, 0.20, 0.30)
    place("under", 1.20, 0.62, 0.40, 0.30)
    place("above", 0.20, -0.70, 0.30, 0.40)     # off screen above
    time.sleep(1.0)
    B = boxes()
    for k in W:
        print(f"  {k:7} {B.get(k)}")

    EXPECT = [
        # from, direction, to (None: nothing that way, the selection stays)
        ("wide", "left", None),       # 'below' shares its left edge: it is below, not left
        ("wide", "up", "above"),      # not 'beside', which only shares its top edge
        ("wide", "right", "beside"),
        ("wide", "down", "below"),
        ("beside", "right", "far"),
        ("beside", "left", "wide"),
        ("beside", "up", "above"),    # up and a bit left: within 45°
        ("far", "up", None),          # everything up there is far off to the left
        ("far", "down", "under"),
        ("far", "left", "beside"),
        ("under", "left", "middle"),
        ("middle", "left", "below"),
        ("middle", "right", "under"),
        ("below", "up", "wide"),
        ("above", "down", "wide"),
    ]
    for zoomed in (False, True):
        view = "zoomed out" if zoomed else "100%"
        for src, d, dst in EXPECT:
            if zoomed:
                back_to_100()
            focus(src)
            if zoomed:
                zoom_out()
            got = press(d, zoomed)
            check(got == (dst or src), f"[{view}] {d} from {src}: {dst or 'stays'} (got {got})")
            if zoomed and dst:
                check(name_of(state().get("selected")) == dst, f"[{view}] and it is the selected window ({state().get('selected')!r})")
            if not zoomed and dst:
                check(in_view(B[dst]), f"[{view}] and it is brought into view")

    # -- move windows around: the arrows follow where they are now
    back_to_100()
    place("beside", 0.20, -0.70, 0.30, 0.40)    # swap 'beside' and 'above'
    place("above", 0.72, 0.15, 0.12, 0.10)
    time.sleep(1.0)
    for zoomed in (False, True):
        view = "zoomed out" if zoomed else "100%"
        back_to_100()
        focus("wide")
        if zoomed:
            zoom_out()
        got = press("right", zoomed)
        check(got == "above", f"[{view}] after moving windows, right from wide goes to what is there now (got {got})")
        got = press("left", zoomed)
        check(got == "wide", f"[{view}] and back left (got {got})")
        got = press("up", zoomed)
        check(got == "beside", f"[{view}] up goes to the one moved above (got {got})")

    # -- tidy up, then every press goes that way, and nothing in line is unreachable
    back_to_100()
    n.dispatch('hl.plugin.spatialoverview.canvas("arrange")'); time.sleep(1.8)
    B = boxes()
    for k in W:
        print(f"  tidied {k:7} {B.get(k)}")
    for zoomed in (False, True):
        view = "zoomed out" if zoomed else "100%"
        bad = []
        for src in W:
            for d in DIRS:
                if zoomed:
                    back_to_100()
                focus(src)
                if zoomed:
                    zoom_out()
                got = press(d, zoomed)
                if got != src and (got not in B or not that_way(B[src], B[got], d)):
                    bad.append(f"{d} from {src} went to {got}")
                if got == src and any(in_line_beyond(B[src], B[o], d) for o in W if o != src):
                    bad.append(f"{d} from {src} went nowhere, with a window in line that way")
        check(not bad, f"[{view}] after tidying, every arrow goes that way ({'; '.join(bad)})")
    back_to_100()
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
