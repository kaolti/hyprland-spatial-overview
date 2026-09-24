// An X11 test window for the canvas tests. Prints every button press with
// its window and root coordinates; a right click opens a magenta
// override-redirect "menu" 40,60 px inside the window, placed in root
// coordinates the way X11 apps (Wine, Battle.net) place their menus; a left
// click closes it.
//
//   x11-menu [TITLE]
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>

int main(int argc, char** argv) {
    Display* d = XOpenDisplay(NULL);
    if (!d)
        return 2;
    const Window root = DefaultRootWindow(d);
    const Window w    = XCreateSimpleWindow(d, root, 30, 30, 500, 300, 0, 0, 0x5a2a08);
    XStoreName(d, w, argc > 1 ? argv[1] : "x11-menu-test");
    XSelectInput(d, w, ButtonPressMask | ButtonReleaseMask | StructureNotifyMask | ExposureMask);
    XMapWindow(d, w);
    XFlush(d);
    setbuf(stdout, NULL);

    Window menu = 0;
    for (;;) {
        XEvent e;
        XNextEvent(d, &e);
        if (e.type != ButtonPress || e.xbutton.window != w)
            continue;
        printf("press %u at %d,%d root %d,%d\n", e.xbutton.button, e.xbutton.x, e.xbutton.y, e.xbutton.x_root, e.xbutton.y_root);
        if (menu) {
            XDestroyWindow(d, menu);
            menu = 0;
        }
        if (e.xbutton.button == 3) {
            int    rx, ry;
            Window child;
            XTranslateCoordinates(d, w, root, 40, 60, &rx, &ry, &child);
            XSetWindowAttributes a = {.background_pixel = 0xff00ff, .override_redirect = True};
            menu = XCreateWindow(d, root, rx, ry, 200, 140, 0, CopyFromParent, InputOutput, CopyFromParent, CWOverrideRedirect | CWBackPixel, &a);
            XMapRaised(d, menu);
            printf("menu at root %d,%d\n", rx, ry);
        }
        XFlush(d);
    }
}
