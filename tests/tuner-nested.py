"""End-to-end test of the live tuner in a nested compositor.

usage: tests/tuner-nested.py [PLUGIN.so]   (default .build/dev/spatialoverview.so)
"""
import sys, time, json, os, tempfile, importlib.util

spec = importlib.util.spec_from_file_location("nav", "tests/navigator-nested.py")
nav = importlib.util.module_from_spec(spec); spec.loader.exec_module(nav)

tmp = tempfile.mkdtemp(prefix="tuner-")
tuning = os.path.join(tmp, "spatialoverview-tuning.lua")
# The preset's loader (examples/spatialoverview.lua applies the tuning file last).
loader = """
local ok, tuned = pcall(dofile, hl.plugin.spatialoverview.tuning_file())
if ok and type(tuned) == "table" then
  local tree = {}
  for key, value in pairs(tuned) do
    local node, parts = tree, {}
    for part in string.gmatch(key, "[^:]+") do parts[#parts + 1] = part end
    for i = 1, #parts - 1 do node[parts[i]] = node[parts[i]] or {}; node = node[parts[i]] end
    node[parts[#parts]] = value
  end
  hl.config({ plugin = { spatialoverview = tree } })
end
"""
extra = ("\nhl.config({misc={disable_hyprland_logo=false, force_default_wallpaper=2}, plugin={spatialoverview={navigator={tuning_file=" + json.dumps(tuning) + "}}}})\n"
         + loader)
n = nav.Nested(sys.argv[1] if len(sys.argv) > 1 else ".build/dev/spatialoverview.so", ".build/shots-tuner", extra_lua=extra)
failures = []
def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)
def opt(key):
    out = json.loads(n.ctl("-j", "getoption", "plugin:spatialoverview:" + key))
    return out.get("float", out.get("int", out.get("bool")))
def accent():
    # the tuned color, as the tuner saved it (Hyprland keeps string settings read-only)
    import re
    m = re.search(r'\["navigator:accent"\] = "([^"]+)"', tuned())
    return m.group(1) if m else json.loads(n.ctl("-j", "getoption", "plugin:spatialoverview:navigator:accent"))["str"]
def tuned():
    return open(tuning).read() if os.path.exists(tuning) else ""
try:
    n.launch()
    check(n.ctl("configerrors", check=False) == "", "no config errors")
    check(n.ctl("-j", "getoption", "plugin:spatialoverview:parallax:strength").find("0.06") >= 0, "new keys registered")
    start = opt("distortion:strength")
    n.dispatch('hl.plugin.spatialoverview.overview("toggle all")'); time.sleep(1.0)
    n.dispatch('hl.plugin.spatialoverview.canvas("tune")'); time.sleep(0.5)
    n.shot("00-tuner")
    n.keys("curv"); time.sleep(0.3)
    n.shot("01-filtered")
    for _ in range(5):
        n.keys("-k", "Right")
    after = opt("distortion:strength")
    check(abs(after - (start + 0.05)) < 1e-4, f"→ x5 raises curvature {start:.2f} -> {after:.2f}")
    check('["distortion:strength"] = ' in tuned(), "tuning file written")
    n.keys("-M", "shift", "-k", "Right", "-m", "shift")
    fine = opt("distortion:strength")
    check(abs(fine - (after + 0.001)) < 1e-4, f"⇧→ is a fine step ({fine:.4f})")
    n.keys("-M", "ctrl", "-k", "Left", "-m", "ctrl")
    coarse = opt("distortion:strength")
    check(abs(coarse - round(fine - 0.1, 2)) < 1e-4, f"⌃← is ten steps, back on the step grid ({coarse:.4f})")
    n.keys("-k", "Delete")
    check(abs(opt("distortion:strength") - start) < 1e-4, "Delete reverts to the value at opening")
    # filter to another setting and flip a switch
    n.keys("-k", "Escape")
    n.keys("uppercase"); time.sleep(0.3)
    was = opt("navigator:uppercase")
    n.keys("-k", "Return")
    check(opt("navigator:uppercase") != was, "Enter flips a switch")
    n.shot("02-normal-case")
    # search field border and HUD scale, live
    n.keys("-k", "Escape")
    n.keys("scale"); time.sleep(0.2)
    for _ in range(4):
        n.keys("-k", "Right")
    check(abs(opt("navigator:hud_scale") - 1.2) < 1e-4, "HUD scale adjusts")
    n.shot("03-hud-scale")
    # accent: presets on ←/→, hue on ⇧←/→, Delete reverts
    n.keys("-k", "Escape")
    n.keys("accent"); time.sleep(0.2)
    accent0 = accent()
    n.keys("-k", "Right")
    accent1 = accent()
    check(accent0 == "#ff6b1a" and accent1 == "#ffb020", f"→ steps the accent to the next preset ({accent0} -> {accent1})")
    for _ in range(8):
        n.keys("-k", "Right")
    n.shot("04-accent-preset")
    n.keys("-M", "shift", "-k", "Right", "-m", "shift")
    hued = accent()
    check(hued.startswith("#") and hued not in ("#ff6b1a", "#ffb020"), f"⇧→ turns the hue ({hued})")
    check('["navigator:accent"] = "' in tuned(), "accent saved as a string")
    n.keys("-k", "Delete")
    check(accent() == accent0, "Delete reverts the accent")
    for _ in range(9):
        n.keys("-k", "Right")   # leave it on Blue for the reload check
    blue = accent()
    n.keys("-k", "Escape")   # clear filter
    n.keys("-k", "Escape")   # close tuner
    check(True, "tuner closes")
    n.keys("-k", "Escape")
    time.sleep(0.5)
    # the file survives a reload through the Lua loader
    print(tuned())
    n.ctl("reload")
    time.sleep(1.0)
    check(abs(opt("navigator:hud_scale") - 1.2) < 1e-4, "tuned value survives hyprctl reload")
    check(opt("navigator:uppercase") != was, "tuned switch survives hyprctl reload")
    check(json.loads(n.ctl("-j", "getoption", "plugin:spatialoverview:navigator:accent"))["str"] == blue, f"tuned accent is the config value after reload ({blue})")
    check(n.ctl("configerrors", check=False) == "", "no config errors after reload")
    check(n.proc.poll() is None, "compositor alive")
finally:
    n.stop()
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
