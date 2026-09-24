"""A window with a magenta popover (an xdg_popup) that opens and closes on
demand, for testing popups on the canvas. Touch TRIGGER/open or
TRIGGER/close; the files are removed once acted on.

usage: popup-app.py TRIGGER_DIR [low]
  low: anchor the popover near the window's bottom, so it hangs below it,
       with a button that turns green under the pointer
"""
import os, sys
import gi

gi.require_version("Gtk", "4.0")
from gi.repository import GLib, Gtk

TRIGGER = sys.argv[1]
LOW = len(sys.argv) > 2 and sys.argv[2] == "low"
CSS_WINDOWBG = "#5a2a08" if LOW else "#103050"
CSS = """
window, window > * { background: WINDOWBG; }
popover > contents { background: #ff00ff; padding: 0; min-width: 220px; min-height: 140px; }
popover > arrow { background: #ff00ff; }
popover button { background: #0a5a3a; min-height: 70px; border-radius: 0; }
popover button:hover { background: #00ff00; }
"""


def activate(app):
    win = Gtk.ApplicationWindow(application=app, title="popup-low" if LOW else "popup-test")
    win.set_default_size(560, 420)
    provider = Gtk.CssProvider()
    provider.load_from_string(CSS.replace("WINDOWBG", CSS_WINDOWBG))
    Gtk.StyleContext.add_provider_for_display(win.get_display(), provider, Gtk.STYLE_PROVIDER_PRIORITY_USER)

    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    box.set_size_request(560, 420)
    anchor = Gtk.Label(label="anchor")
    anchor.set_halign(Gtk.Align.START)
    anchor.set_margin_start(80)
    anchor.set_margin_top(380 if LOW else 60)
    box.append(anchor)
    win.set_child(box)

    popover = Gtk.Popover()
    popover.set_autohide(False)
    popover.set_has_arrow(False)
    popover.set_position(Gtk.PositionType.BOTTOM)
    content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
    content.append(Gtk.Label(label="POPUP"))
    if LOW:
        content.append(Gtk.Button(label="HOVER"))
    popover.set_child(content)
    popover.set_parent(anchor)

    def poll():
        for name, action in (("open", popover.popup), ("close", popover.popdown)):
            path = os.path.join(TRIGGER, name)
            if os.path.exists(path):
                os.remove(path)
                action()
        return True

    GLib.timeout_add(50, poll)
    win.present()


app = Gtk.Application(application_id="dev.spatialoverview.popuptest" + (".low" if LOW else ""))
app.connect("activate", activate)
app.run(None)
