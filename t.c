/*
  Simple Xlib application drawing a box in a window.
  gcc input.c -o output -lX11
*/

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
  Display *d;
  Window w;
  XEvent e;
  char *msg = "Hello, World!";
  int s;

                       /* open connection with the server */
  d = XOpenDisplay(NULL);
  if (d == NULL) {
    fprintf(stderr, "Cannot open display\n");
    exit(1);
  }

  s = DefaultScreen(d);

                       /* create window */
  w = XCreateSimpleWindow(d, RootWindow(d, s), 10, 10, 200, 200, 1,
                          BlackPixel(d, s), WhitePixel(d, s));

                       /* select kind of events we are interested in */
  XSelectInput(d, w, ExposureMask | KeyPressMask);

  XSizeHints *h = XAllocSizeHints();
  h->min_aspect.x = 1;
  h->min_aspect.y = 2;
  h->max_aspect.x = 2;
  h->max_aspect.y = 1;
  h->flags |= PAspect;
  XSetWMNormalHints(d, w, h);

                       /* map (show) the window */
  XMapWindow(d, w);

                       /* event loop */
  while (1) {
    XNextEvent(d, &e);
                       /* draw or redraw the window */
    if (e.type == Expose) {
      XFillRectangle(d, w, DefaultGC(d, s), 20, 20, 10, 10);
      XDrawString(d, w, DefaultGC(d, s), 50, 50, msg, strlen(msg));
    }
    continue;
                       /* exit on key press */
    if (e.type == KeyPress)
      break;
  }

                       /* close connection to server */
  XCloseDisplay(d);

  return 0;
}
