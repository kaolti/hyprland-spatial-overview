"""A native Wayland (GTK4) window that animates on every frame the compositor
grants it, like a video player, and prints every 2 seconds how many frames
it got and how evenly they came.

usage: fps-probe.py TITLE
"""
import statistics, sys
import gi

gi.require_version("Gtk", "4.0")
from gi.repository import Gio, Gtk

stamps = []


def tick(widget, clock):
    stamps.append(clock.get_frame_time() / 1000.0)  # ms
    widget.queue_draw()
    if stamps[-1] - stamps[0] >= 2000.0 and len(stamps) > 2:
        gaps = [b - a for a, b in zip(stamps, stamps[1:])]
        gaps.sort()
        print(f"fps {len(gaps) / ((stamps[-1] - stamps[0]) / 1000.0):.1f} mean {statistics.mean(gaps):.1f} "
              f"p95 {gaps[int(len(gaps) * 0.95)]:.1f} max {gaps[-1]:.1f}", flush=True)
        stamps[:] = [stamps[-1]]
    return True


def activate(app):
    win = Gtk.ApplicationWindow(application=app, title=sys.argv[1])
    win.set_default_size(400, 300)
    area = Gtk.DrawingArea()
    area.set_draw_func(lambda _a, cr, w, h: (cr.set_source_rgb((len(stamps) % 60) / 60.0, 0.3, 0.5), cr.paint()))
    area.add_tick_callback(tick)
    win.set_child(area)
    win.present()


# Not unique: another probe running elsewhere (say, in the live session)
# must not swallow this one.
app = Gtk.Application(application_id="dev.spatialoverview.fpsprobe", flags=Gio.ApplicationFlags.NON_UNIQUE)
app.connect("activate", activate)
app.run(None)
