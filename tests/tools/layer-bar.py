"""A magenta 30 px bar on the top layer of one output, like Waybar: Hyprland
hides it under a fullscreen window.

usage: LD_PRELOAD=/usr/lib/libgtk4-layer-shell.so layer-bar.py OUTPUT
(gtk4-layer-shell must be loaded before libwayland-client)
"""
import sys
import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Gtk4LayerShell", "1.0")
from gi.repository import Gdk, Gio, Gtk, Gtk4LayerShell as LayerShell

CSS = b"window { background: #ff00ff; }"


def activate(app):
    win = Gtk.ApplicationWindow(application=app)
    LayerShell.init_for_window(win)
    LayerShell.set_layer(win, LayerShell.Layer.TOP)
    LayerShell.set_namespace(win, "test-bar")
    for edge in (LayerShell.Edge.TOP, LayerShell.Edge.LEFT, LayerShell.Edge.RIGHT):
        LayerShell.set_anchor(win, edge, True)
    LayerShell.auto_exclusive_zone_enable(win)
    monitors = Gdk.Display.get_default().get_monitors()
    for i in range(monitors.get_n_items()):
        if monitors.get_item(i).get_connector() == sys.argv[1]:
            LayerShell.set_monitor(win, monitors.get_item(i))
    win.set_default_size(100, 30)
    provider = Gtk.CssProvider()
    provider.load_from_data(CSS)
    Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)
    win.present()


app = Gtk.Application(application_id=f"dev.spatialoverview.bar{sys.argv[1].replace('-', '')}", flags=Gio.ApplicationFlags.NON_UNIQUE)
app.connect("activate", activate)
app.run(None)
