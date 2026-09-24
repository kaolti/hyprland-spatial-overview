"""scripts/install.sh and scripts/uninstall.sh against a nested Hyprland with
its own config, data and state folders (never the real ones), run from a
copy of this checkout so the build in it is left alone.

usage: tests/install-nested.py
"""
import json, os, shutil, subprocess, sys, tempfile, time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
tmp = Path(tempfile.mkdtemp(prefix="install-test-"))
src = tmp / "src"
config = tmp / "config" / "hypr"
failures = []


def check(cond, msg):
    print(("PASS " if cond else "FAIL ") + msg, flush=True)
    if not cond:
        failures.append(msg)


# A plain Hyprland config: the test base without the plugin.
base = (ROOT / "tests" / "nested.lua").read_text().split("local plugin_path")[0]
config.mkdir(parents=True)
(config / "hyprland.lua").write_text(base)
original = base

shutil.copytree(ROOT, src, ignore=shutil.ignore_patterns(".build", ".git", "spatialoverview.so*", "flight-deck", "preserved-runtime", "__pycache__"))

env = os.environ.copy()
env.update(XDG_CONFIG_HOME=str(tmp / "config"), XDG_DATA_HOME=str(tmp / "data"), XDG_STATE_HOME=str(tmp / "state"), AQ_BACKEND="wayland")
log = open(tmp / "hyprland.log", "w")
proc = subprocess.Popen(["Hyprland", "--config", str(config / "hyprland.lua")], env=env, stdout=log, stderr=subprocess.STDOUT)
sig = None
for _ in range(80):
    try:
        sig = next((i["instance"] for i in json.loads(subprocess.check_output(["hyprctl", "instances", "-j"], text=True)) if i["pid"] == proc.pid), None)
    except (subprocess.SubprocessError, json.JSONDecodeError):
        pass
    if sig:
        break
    time.sleep(0.1)
env["HYPRLAND_INSTANCE_SIGNATURE"] = sig


def ctl(*args):
    return subprocess.run(["hyprctl", "-i", sig, *args], capture_output=True, text=True, timeout=10).stdout.strip()


def run(*script):
    r = subprocess.run([str(src / "scripts" / script[0]), *script[1:]], env=env, capture_output=True, text=True, timeout=600)
    print("  " + (r.stdout + r.stderr).strip().replace("\n", "\n  ")[-1400:])
    return r.returncode


def loaded():
    return "Plugin spatialoverview " in ctl("plugin", "list")


try:
    for title in ("one", "two"):
        ctl("dispatch", "hl.dsp.exec_cmd(" + json.dumps(f"foot --title={title} /usr/bin/cat") + ")")
        time.sleep(0.8)

    print("# install")
    check(run("install.sh") == 0, "install.sh succeeds")
    lua = (config / "hyprland.lua").read_text()
    check("-- >>> spatial-overview >>>" in lua and "hl.plugin.load(" in lua, "hyprland.lua gets the marked block")
    check((config / "spatialoverview.lua").is_file(), "settings are installed")
    check((tmp / "data" / "spatial-overview" / "spatialoverview.so").is_file(), "plugin is installed")
    check(loaded(), "plugin is loaded")
    check(ctl("configerrors") == "", "no config errors")
    binds = json.loads(ctl("-j", "binds"))
    check(any(b.get("key") == "G" and b.get("modmask") == 68 for b in binds), "SUPER + CTRL + G is bound")
    ctl("dispatch", 'hl.plugin.spatialoverview.overview("toggle all")')
    time.sleep(1.0)
    check(proc.poll() is None, "compositor alive with the canvas open")

    print("# install again (an update)")
    check(run("install.sh") == 0, "install.sh succeeds again")
    check(loaded() and proc.poll() is None, "still loaded, compositor alive")
    check((config / "hyprland.lua").read_text().count("-- >>> spatial-overview >>>") == 1, "the block is not added twice")

    print("# uninstall")
    check(run("uninstall.sh") == 0, "uninstall.sh succeeds")
    time.sleep(0.5)
    check(not loaded(), "plugin is unloaded")
    check((config / "hyprland.lua").read_text() == original, "hyprland.lua is back to what it was")
    check(not (tmp / "data" / "spatial-overview").exists(), "installed plugin is deleted")
    check((config / "spatialoverview.lua").is_file(), "settings are kept")
    check(proc.poll() is None, "compositor alive")

    print("# uninstall --purge")
    check(run("uninstall.sh", "--purge") == 0, "uninstall.sh --purge succeeds")
    check(not (config / "spatialoverview.lua").exists(), "settings are deleted")
finally:
    if proc.poll() is None:
        ctl("dispatch", "hl.dsp.exit()")
        try:
            proc.wait(timeout=8)
        except subprocess.TimeoutExpired:
            proc.terminate()
    log.close()
    shutil.rmtree(tmp, ignore_errors=True)
print("ALL PASSED" if not failures else f"{len(failures)} FAILED")
sys.exit(1 if failures else 0)
