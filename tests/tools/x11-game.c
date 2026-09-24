// An X11 test window that handles its display mode the way a Wine game does.
// It works out its monitor from where the X server says it is (the monitor
// its window overlaps most, as Windows' MonitorFromWindow), and reads
// commands on stdin:
//
//   borderless    cover that monitor, then ask for fullscreen, as Wine does
//                 once a window covers a monitor ("windowed fullscreen")
//   fullscreen    ask for _NET_WM_STATE_FULLSCREEN
//   windowed W H  drop fullscreen, then be W x H, centered on its monitor
//   attention     flash for attention (_NET_WM_STATE_DEMANDS_ATTENTION), as
//                 Wine does for a game that wants to be noticed (combat)
//   hidecursor    hide its cursor the way Wine does (mouse-look): an empty
//                 1x1 cursor; showcursor brings the cyan one back
//   report        print where it is
//
// It prints "at X Y W H monitor N fullscreen F" whenever that changes, and
// "key NAME" for every key it gets. Its cursor is a solid cyan square, so a
// screenshot shows whether its own cursor is on screen.
//
//   x11-game [TITLE] < commands
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

static Display* d;
static Window   root, w;
static Cursor   shown, hidden;
static Atom     NET_WM_STATE, NET_WM_STATE_FULLSCREEN, NET_WM_STATE_DEMANDS_ATTENTION;

struct rect {
    int x, y, w, h;
};

static struct rect geometry(void) {
    Window       child, r;
    int          x, y;
    unsigned int width, height, border, depth;
    XGetGeometry(d, w, &r, &x, &y, &width, &height, &border, &depth);
    XTranslateCoordinates(d, w, root, 0, 0, &x, &y, &child);
    return (struct rect){x, y, (int)width, (int)height};
}

static int overlap(struct rect a, struct rect b) {
    const int x0 = a.x > b.x ? a.x : b.x, x1 = a.x + a.w < b.x + b.w ? a.x + a.w : b.x + b.w;
    const int y0 = a.y > b.y ? a.y : b.y, y1 = a.y + a.h < b.y + b.h ? a.y + a.h : b.y + b.h;
    return x1 > x0 && y1 > y0 ? (x1 - x0) * (y1 - y0) : 0;
}

// The monitor the window overlaps most, else the nearest one.
static int monitorOf(struct rect r, struct rect* out) {
    int               n    = 0;
    XRRMonitorInfo*   mons = XRRGetMonitors(d, root, True, &n);
    int               best = -1, bestArea = 0;
    long long         bestDist = -1;
    for (int i = 0; i < n; ++i) {
        const struct rect m    = {mons[i].x, mons[i].y, mons[i].width, mons[i].height};
        const int         area = overlap(r, m);
        if (area > bestArea) {
            bestArea = area;
            best     = i;
        }
    }
    if (best < 0) {
        for (int i = 0; i < n; ++i) {
            const long long dx = (mons[i].x + mons[i].width / 2) - (r.x + r.w / 2), dy = (mons[i].y + mons[i].height / 2) - (r.y + r.h / 2);
            if (bestDist < 0 || dx * dx + dy * dy < bestDist) {
                bestDist = dx * dx + dy * dy;
                best     = i;
            }
        }
    }
    if (best >= 0 && out)
        *out = (struct rect){mons[best].x, mons[best].y, mons[best].width, mons[best].height};
    XRRFreeMonitors(mons);
    return best;
}

static int fullscreenState(void) {
    Atom           type;
    int            format, fs = 0;
    unsigned long  count, after;
    unsigned char* data = NULL;
    if (XGetWindowProperty(d, w, NET_WM_STATE, 0, 32, False, XA_ATOM, &type, &format, &count, &after, &data) == Success && data) {
        for (unsigned long i = 0; i < count; ++i)
            fs |= ((Atom*)data)[i] == NET_WM_STATE_FULLSCREEN;
        XFree(data);
    }
    return fs;
}

static void askState(int on, Atom state) {
    XEvent e                = {0};
    e.xclient.type          = ClientMessage;
    e.xclient.window        = w;
    e.xclient.message_type  = NET_WM_STATE;
    e.xclient.format        = 32;
    e.xclient.data.l[0]     = on ? 1 : 0;
    e.xclient.data.l[1]     = state;
    e.xclient.data.l[3]     = 1;
    XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
}

static void askFullscreen(int on) {
    askState(on, NET_WM_STATE_FULLSCREEN);
}

static char last[256];
static void report(int force) {
    const struct rect r = geometry();
    char              line[256];
    snprintf(line, sizeof line, "at %d %d %d %d monitor %d fullscreen %d", r.x, r.y, r.w, r.h, monitorOf(r, NULL), fullscreenState());
    if (force || strcmp(line, last)) {
        printf("%s\n", line);
        strcpy(last, line);
    }
}

static void command(char* line) {
    int               width, height;
    struct rect       mon;
    const struct rect r = geometry();
    monitorOf(r, &mon);
    if (!strncmp(line, "borderless", 10)) {
        XMoveResizeWindow(d, w, mon.x, mon.y, mon.w, mon.h);
        askFullscreen(1);
    } else if (!strncmp(line, "fullscreen", 10))
        askFullscreen(1);
    else if (sscanf(line, "windowed %d %d", &width, &height) == 2) {
        askFullscreen(0);
        XMoveResizeWindow(d, w, mon.x + (mon.w - width) / 2, mon.y + (mon.h - height) / 2, width, height);
    } else if (!strncmp(line, "hidecursor", 10))
        XDefineCursor(d, w, hidden);
    else if (!strncmp(line, "showcursor", 10))
        XDefineCursor(d, w, shown);
    else if (!strncmp(line, "attention", 9))
        askState(1, NET_WM_STATE_DEMANDS_ATTENTION);
    else if (!strncmp(line, "report", 6))
        report(1);
    XFlush(d);
}

int main(int argc, char** argv) {
    d = XOpenDisplay(NULL);
    if (!d)
        return 2;
    root                    = DefaultRootWindow(d);
    NET_WM_STATE            = XInternAtom(d, "_NET_WM_STATE", False);
    NET_WM_STATE_FULLSCREEN = XInternAtom(d, "_NET_WM_STATE_FULLSCREEN", False);
    NET_WM_STATE_DEMANDS_ATTENTION = XInternAtom(d, "_NET_WM_STATE_DEMANDS_ATTENTION", False);
    w                       = XCreateSimpleWindow(d, root, 40, 40, 800, 450, 0, 0, 0x2a5a08);
    // a solid 24 px cyan square as its cursor
    static char bits[24 * 24 / 8];
    memset(bits, 0xff, sizeof bits);
    const Pixmap shape = XCreateBitmapFromData(d, w, bits, 24, 24);
    XColor       cyan = {.red = 0, .green = 0xffff, .blue = 0xffff}, black = {0};
    shown = XCreatePixmapCursor(d, shape, shape, &cyan, &black, 12, 12);
    XDefineCursor(d, w, shown);
    // Wine's hidden cursor: an empty 1x1 one
    static char none[1] = {0};
    const Pixmap empty = XCreateBitmapFromData(d, w, none, 1, 1);
    hidden             = XCreatePixmapCursor(d, empty, empty, &black, &black, 0, 0);
    XStoreName(d, w, argc > 1 ? argv[1] : "x11-game");
    XSelectInput(d, w, StructureNotifyMask | PropertyChangeMask | ExposureMask | KeyPressMask);
    XMapWindow(d, w);
    XFlush(d);
    setbuf(stdout, NULL);

    char   buffer[256];
    size_t used = 0;
    for (;;) {
        while (XPending(d)) {
            XEvent e;
            XNextEvent(d, &e);
            if (e.type == ConfigureNotify || e.type == PropertyNotify || e.type == MapNotify)
                report(0);
            else if (e.type == KeyPress) {
                const char* name = XKeysymToString(XLookupKeysym(&e.xkey, 0));
                printf("key %s\n", name ? name : "?");
            }
        }
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(ConnectionNumber(d), &fds);
        FD_SET(0, &fds);
        struct timeval tv = {0, 200000};
        if (select(ConnectionNumber(d) + 1, &fds, NULL, NULL, &tv) <= 0) {
            report(0);
            continue;
        }
        if (FD_ISSET(0, &fds)) {
            const ssize_t got = read(0, buffer + used, sizeof buffer - 1 - used);
            if (got <= 0)
                return 0;
            used += (size_t)got;
            buffer[used] = 0;
            char* newline;
            while ((newline = strchr(buffer, '\n'))) {
                *newline = 0;
                command(buffer);
                memmove(buffer, newline + 1, strlen(newline + 1) + 1);
                used = strlen(buffer);
            }
        }
    }
}
