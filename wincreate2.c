/* stolen from wikipedia and modified a bit,
 * compile like this:
 * gcc -o test test.c -lxcb -std=c99
 */

#include <xcb/xcb.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    int                  nwindow = 1;
    int                  i;
    xcb_connection_t    *c;
    xcb_screen_t        *s;
    xcb_window_t         w;
    xcb_gcontext_t       g;
    xcb_generic_event_t *e;
    uint32_t             mask;
    uint32_t             values[2];
    int                  done = 0;
    xcb_rectangle_t      r = { 20, 20, 60, 60 };

    xcb_window_t *missing;
    xcb_configure_notify_event_t *conf;

    if (argc > 1)
        nwindow = atoi(argv[1]);
    if (nwindow < 1)
    {
        fprintf(stderr, "Usage:\n%s <n>\tmap n windows\n", argv[0]);
        exit(-1);
    }

    missing = calloc(sizeof(*missing), nwindow);

    c = xcb_connect(NULL,NULL);
    if (xcb_connection_has_error(c))
    {
        printf("Cannot open display\n");
        exit(1);
    }

    s = xcb_setup_roots_iterator( xcb_get_setup(c) ).data;
    fprintf(stderr, "width=%d height=%d\n", s->width_in_pixels, s->height_in_pixels);

    mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    values[0] = s->white_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_KEY_PRESS | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_PROPERTY_CHANGE;

    for(i = 0; i < nwindow; i++)
    {
        w = xcb_generate_id(c);
        xcb_create_window(c, s->root_depth, w, s->root,
                            (10 * i) / s->width_in_pixels, (10 * i) % s->height_in_pixels, 100, 100, 1,
                            XCB_WINDOW_CLASS_INPUT_OUTPUT, s->root_visual,
                            mask, values);

        xcb_map_window(c, w);
	missing[i] = w;
    }

    xcb_flush(c);

    printf("Done with setup\n");

    while (!done && (e = xcb_wait_for_event(c)))
    {
        switch (e->response_type & ~0x80)
        {
            case XCB_KEY_PRESS:
                done = 1;
                break;
	    case XCB_CONFIGURE_NOTIFY:
		conf = (xcb_configure_notify_event_t *) e;

		done = 1;
		for(i = 0; i < nwindow; i++) {
			// This checks if the window touches the right screen
			// edge. This way you can also use this in multi-screen
			// if you run this on the right screen.
			if (conf->width + conf->x + 2 * conf->border_width == s->width_in_pixels
					&& missing[i] == conf->window) {
				missing[i] = 0;
			}
			if (missing[i] != 0) {
				done = 0;
			}
		}
        }
        free(e);
    }

    xcb_disconnect(c);

    return 0;
}
