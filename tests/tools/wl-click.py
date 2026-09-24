"""A native Wayland (GTK4) test window that prints every click with its
window coordinates, and every key it gets.

usage: wl-click.py TITLE
"""
import sys
import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gdk, Gtk


def activate(app):
    win = Gtk.ApplicationWindow(application=app, title=sys.argv[1])
    win.set_default_size(500, 300)
    area = Gtk.DrawingArea()
    area.set_size_request(500, 300)
    click = Gtk.GestureClick()
    click.connect("pressed", lambda _g, _n, x, y: print(f"click {x:.0f},{y:.0f}", flush=True))
    area.add_controller(click)
    keys = Gtk.EventControllerKey()
    keys.connect("key-pressed", lambda _c, keyval, _code, _state: print(f"key {Gdk.keyval_name(keyval)}", flush=True) or False)
    win.add_controller(keys)
    win.set_child(area)
    win.present()


app = Gtk.Application(application_id="dev.spatialoverview.wlclick")
app.connect("activate", activate)
app.run(None)
