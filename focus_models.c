/* This example experiments with the 4 ICCCM focus modes. */

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>

char *window_name[] = { "No Input", "Passive", "Locally Active",
                        "Globally Active", "Satellite" };

int input_hint[] = { False, True, True, False, False };
int take_focus[] = { False, False, True, True, False };

Window
input_window (Display *d, int screen, int i)
{
  Window win;
  XWMHints wm_hints;

  win = XCreateSimpleWindow (d, RootWindow (d, screen),
                             10, 10, 150, 10, 0,
                             WhitePixel (d, screen), WhitePixel (d, screen));

  XSelectInput (d, win, ButtonPressMask|FocusChangeMask|KeyPressMask);

  XStoreName (d, win, window_name[i]);

  wm_hints.flags = InputHint;
  wm_hints.input = input_hint[i];

  XSetWMHints (d, win, &wm_hints);
  if (take_focus[i])
    {
      Atom protocols[1];
      protocols[0] = XInternAtom (d, "WM_TAKE_FOCUS", False);
      XSetWMProtocols (d, win, protocols, 1);
    }

  return win;
}

int main (int argc, char *argv[])
{
  Display *d;
  int screen, i;
  Window window, win[5];
  XWindowAttributes xatt;
  Atom atom;
  XEvent ev;

  d = XOpenDisplay (NULL);

  screen = DefaultScreen (d);

  for (i = 0; i < 5; i++)
    {
      win[i] = input_window (d, screen, i);
      XMapWindow (d, win[i]);
    }

  while (1)
    {
      XNextEvent (d, &ev);

      for (i = 0; i < 5; i++)
        {
          window = ev.xany.window;
          if (win[i] == window)
            {
              printf("event on window \"%s\": ", window_name[i]);
              switch (ev.xany.type)
                {
                case FocusIn:
                  printf ("FocusIn\n");
                  break;
                case FocusOut:
                  printf ("FocusOut\n");
                  break;
                case ButtonPress:
                  printf ("ButtonPress\n");
                case KeyPress:
                  printf ("KeyPress %s\n",
                          XKeysymToString (XLookupKeysym (&ev.xkey, 0)));
                  break;
                case ClientMessage:
                  atom = ev.xclient.data.l[0];
                  printf ("ClientMessage %s\n", XGetAtomName (d, atom));
                  if (atom == XInternAtom (d, "WM_TAKE_FOCUS", False))
                    {
                      if (i == 2)
                        {
                          printf ("\t...do nothing\n");
                        }
                      else if (i == 3)
                        {
                          if (XGetWindowAttributes (d, win[4], &xatt)
                               && (xatt.map_state == IsViewable))
                            {
                              printf ("\t...setting focus on our own\n");
                              XSetInputFocus (d, win[4], RevertToParent,
                                              ev.xclient.data.l[1]);
                            }
                          else
                             {
                              printf ("\t...but we are not viewable\n");
                            }
                        }
                    }
                  break;
                default:
                  printf ("event %d\n", ev.xany.type);
                }
            }
        }
    }

  return 0;
}
