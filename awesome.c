/*
 * awesome.c - awesome main functions
 *
 * Copyright © 2007-2009 Julien Danjou <julien@danjou.info>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <getopt.h>

#include <locale.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>

#include <xcb/bigreq.h>
#include <xcb/randr.h>
#include <xcb/shape.h>
#include <xcb/xcb_event.h>
#include <xcb/xinerama.h>
#include <xcb/xtest.h>

#include "keyresolv.h"
#include "font.h"
#include "spawn.h"
#include "xwindow.h"
#include "ewmh.h"
#include "dbus.h"
#include "systray.h"
#include "event.h"
#include "property.h"
#include "screen.h"
#include "common/version.h"
#include "common/xutil.h"
#include "common/backtrace.h"

awesome_t globalconf;

/** argv used to run awesome */
static char *awesome_argv;

/** Call before exiting.
 */
void
awesome_atexit(void)
{
    signal_object_emit(_G_L, &global_signals, "exit", 0);

    a_dbus_cleanup();

    systray_cleanup();

    /* Close Lua */
    lua_close(_G_L);

    xcb_flush(_G_connection);

    /* Disconnect *after* closing lua */
    xcb_disconnect(_G_connection);

    ev_default_destroy();
}

/** Scan X to find windows to manage.
 */
static void
scan(void)
{
    int i, tree_c_len;
    xcb_query_tree_cookie_t tree_c;
    xcb_query_tree_reply_t *tree_r;
    xcb_window_t *wins = NULL;
    xcb_get_window_attributes_reply_t *attr_r;
    xcb_get_geometry_reply_t *geom_r;
    long state;

    /* Get the window tree associated to this screen */
    tree_c = xcb_query_tree_unchecked(_G_connection,
                                      globalconf.screen->root);

    tree_r = xcb_query_tree_reply(_G_connection,
                                  tree_c,
                                  NULL);

    if(!tree_r)
        return;

    /* Get the tree of the children windows of the current root window */
    if(!(wins = xcb_query_tree_children(tree_r)))
        fatal("cannot get tree children");

    tree_c_len = xcb_query_tree_children_length(tree_r);
    xcb_get_window_attributes_cookie_t attr_wins[tree_c_len];
    xcb_get_property_cookie_t state_wins[tree_c_len];

    for(i = 0; i < tree_c_len; i++)
    {
        attr_wins[i] = xcb_get_window_attributes_unchecked(_G_connection,
                                                           wins[i]);

        state_wins[i] = xwindow_get_state_unchecked(wins[i]);
    }

    xcb_get_geometry_cookie_t *geom_wins[tree_c_len];

    for(i = 0; i < tree_c_len; i++)
    {
        attr_r = xcb_get_window_attributes_reply(_G_connection,
                                                 attr_wins[i],
                                                 NULL);

        state = xwindow_get_state_reply(state_wins[i]);

        if(!attr_r || attr_r->override_redirect
           || attr_r->map_state == XCB_MAP_STATE_UNMAPPED
           || state == XCB_WM_STATE_WITHDRAWN)
        {
            geom_wins[i] = NULL;
            p_delete(&attr_r);
            continue;
        }

        p_delete(&attr_r);

        /* Get the geometry of the current window */
        geom_wins[i] = p_alloca(xcb_get_geometry_cookie_t, 1);
        *(geom_wins[i]) = xcb_get_geometry_unchecked(_G_connection, wins[i]);
    }

    for(i = 0; i < tree_c_len; i++)
    {
        if(!geom_wins[i] || !(geom_r = xcb_get_geometry_reply(_G_connection,
                                                              *(geom_wins[i]), NULL)))
            continue;

        client_manage(wins[i], geom_r, true);

        p_delete(&geom_r);
    }

    p_delete(&tree_r);
}

static void
a_refresh_cb(EV_P_ ev_prepare *w, int revents)
{
    awesome_refresh();
}

static void
a_xcb_check_cb(EV_P_ ev_check *w, int revents)
{
    xcb_generic_event_t *mouse = NULL, *event;

    while((event = xcb_poll_for_event(_G_connection)))
    {
        /* We will treat mouse events later.
         * We cannot afford to treat all mouse motion events,
         * because that would be too much CPU intensive, so we just
         * take the last we get after a bunch of events. */
        if(XCB_EVENT_RESPONSE_TYPE(event) == XCB_MOTION_NOTIFY)
        {
            p_delete(&mouse);
            mouse = event;
        }
        else
        {
            event_handle(event);
            p_delete(&event);
        }
    }

    if(mouse)
    {
        event_handle(mouse);
        p_delete(&mouse);
    }
}

static void
a_xcb_io_cb(EV_P_ ev_io *w, int revents)
{
    /* empty */
}

static void
signal_fatal(int signum)
{
    buffer_t buf;
    backtrace_get(&buf);
    fatal("dumping backtrace\n%s", buf.s);
}

/** Function to exit on some signals.
 * \param w the signal received, unused
 * \param revents currently unused
 */
static void
exit_on_signal(EV_P_ ev_signal *w, int revents)
{
    ev_unloop(EV_A_ 1);
}

void
awesome_restart(void)
{
    awesome_atexit();
    a_exec(awesome_argv);
}

/** Function to restart awesome on some signals.
 * \param w the signal received, unused
 * \param revents Currently unused
 */
static void
restart_on_signal(EV_P_ ev_signal *w, int revents)
{
    awesome_restart();
}

/** Print help and exit(2) with given exit_code.
 * \param exit_code The exit code.
 */
static void __attribute__ ((noreturn))
exit_help(int exit_code)
{
    FILE *outfile = (exit_code == EXIT_SUCCESS) ? stdout : stderr;
    fprintf(outfile,
"Usage: awesome [OPTION]\n\
  -h, --help             show help\n\
  -v, --version          show version\n\
  -c, --config FILE      configuration file to use\n\
  -k, --check            check configuration file syntax\n");
    exit(exit_code);
}

/** Hello, this is main.
 * \param argc Who knows.
 * \param argv Who knows.
 * \return EXIT_SUCCESS I hope.
 */
int
main(int argc, char **argv)
{
    char *confpath = NULL;
    int xfd, i, opt, colors_nbr;
    xcolor_init_request_t colors_reqs[2];
    ssize_t cmdlen = 1;
    xdgHandle xdg;
    static struct option long_options[] =
    {
        { "help",    0, NULL, 'h' },
        { "version", 0, NULL, 'v' },
        { "config",  1, NULL, 'c' },
        { "check",   0, NULL, 'k' },
        { NULL,      0, NULL, 0 }
    };

    /* event loop watchers */
    ev_io xio    = { .fd = -1 };
    ev_check xcheck;
    ev_prepare a_refresh;
    ev_signal sigint;
    ev_signal sigterm;
    ev_signal sighup;

    /* save argv */
    for(i = 0; i < argc; i++)
        cmdlen += a_strlen(argv[i]) + 1;

    awesome_argv = p_new(char, cmdlen);
    a_strcpy(awesome_argv, cmdlen, argv[0]);

    for(i = 1; i < argc; i++)
    {
        a_strcat(awesome_argv, cmdlen, " ");
        a_strcat(awesome_argv, cmdlen, argv[i]);
    }

    /* Text won't be printed correctly otherwise */
    setlocale(LC_CTYPE, "");

    /* Get XDG basedir data */
    xdgInitHandle(&xdg);

    /* init lua */
    luaA_init(&xdg);

    /* check args */
    while((opt = getopt_long(argc, argv, "vhkc:",
                             long_options, NULL)) != -1)
        switch(opt)
        {
          case 'v':
            eprint_version();
            break;
          case 'h':
            exit_help(EXIT_SUCCESS);
            break;
          case 'k':
            if(!luaA_parserc(&xdg, confpath, false))
            {
                fprintf(stderr, "✘ Configuration file syntax error.\n");
                return EXIT_FAILURE;
            }
            else
            {
                fprintf(stderr, "✔ Configuration file syntax OK.\n");
                return EXIT_SUCCESS;
            }
          case 'c':
            if(a_strlen(optarg))
                confpath = a_strdup(optarg);
            else
                fatal("-c option requires a file name");
            break;
        }

    _G_loop = ev_default_loop(EVFLAG_NOSIGFD);

    /* register function for signals */
    ev_signal_init(&sigint, exit_on_signal, SIGINT);
    ev_signal_init(&sigterm, exit_on_signal, SIGTERM);
    ev_signal_init(&sighup, restart_on_signal, SIGHUP);
    ev_signal_start(_G_loop, &sigint);
    ev_signal_start(_G_loop, &sigterm);
    ev_signal_start(_G_loop, &sighup);
    ev_unref(_G_loop);
    ev_unref(_G_loop);
    ev_unref(_G_loop);

    struct sigaction sa = { .sa_handler = signal_fatal, .sa_flags = 0 };
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, 0);

    /* X stuff */
    _G_connection = xcb_connect(NULL, &_G_default_screen);
    if(xcb_connection_has_error(_G_connection))
        fatal("cannot open display");

    globalconf.screen = xcb_aux_get_screen(_G_connection, _G_default_screen);

    /* The default GC is just a newly created associated to the root window */
    globalconf.gc = xcb_generate_id(_G_connection);
    xcb_create_gc(_G_connection, globalconf.gc, globalconf.screen->root, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND,
                  (const uint32_t[]) { globalconf.screen->black_pixel, globalconf.screen->white_pixel });

    /* Prefetch all the extensions we might need */
    xcb_prefetch_extension_data(_G_connection, &xcb_big_requests_id);
    xcb_prefetch_extension_data(_G_connection, &xcb_test_id);
    xcb_prefetch_extension_data(_G_connection, &xcb_randr_id);
    xcb_prefetch_extension_data(_G_connection, &xcb_xinerama_id);
    xcb_prefetch_extension_data(_G_connection, &xcb_shape_id);

    /* initialize dbus */
    a_dbus_init();

    /* Grab server */
    xcb_grab_server(_G_connection);
    xcb_flush(_G_connection);

    /* Get the file descriptor corresponding to the X connection */
    xfd = xcb_get_file_descriptor(_G_connection);
    ev_io_init(&xio, &a_xcb_io_cb, xfd, EV_READ);
    ev_io_start(_G_loop, &xio);
    ev_check_init(&xcheck, &a_xcb_check_cb);
    ev_check_start(_G_loop, &xcheck);
    ev_unref(_G_loop);
    ev_prepare_init(&a_refresh, &a_refresh_cb);
    ev_prepare_start(_G_loop, &a_refresh);
    ev_unref(_G_loop);

    {
        const uint32_t select_input_val = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

        /* This causes an error if some other window manager is running */
        xcb_change_window_attributes(_G_connection,
                                     globalconf.screen->root,
                                     XCB_CW_EVENT_MASK, &select_input_val);
    }

    /* Need to xcb_flush to validate error handler */
    xcb_aux_sync(_G_connection);

    /* Process all errors in the queue if any. There can be no events yet, so if
     * this function returns something, it must be an error. */
    if (xcb_poll_for_event(_G_connection) != NULL)
        fatal("another window manager is already running");

    /* Prefetch the maximum request length */
    xcb_prefetch_maximum_request_length(_G_connection);

    /* Allocate the key symbols */
    _G_keysyms = xcb_key_symbols_alloc(_G_connection);
    xcb_get_modifier_mapping_cookie_t xmapping_cookie =
        xcb_get_modifier_mapping_unchecked(_G_connection);

    /* init atom cache */
    atoms_init(_G_connection);

    /* init screens information */
    screen_scan(_G_L);

    /* init default font and colors */
    colors_reqs[0] = xcolor_init_unchecked(&_G_fg,
                                           "black", sizeof("black") - 1);

    colors_reqs[1] = xcolor_init_unchecked(&_G_bg,
                                           "white", sizeof("white") - 1);

    font_init(&_G_font, "sans 8");

    for(colors_nbr = 0; colors_nbr < 2; colors_nbr++)
        xcolor_init_reply(colors_reqs[colors_nbr]);

    keyresolv_lock_mask_refresh(_G_connection, xmapping_cookie, _G_keysyms);

    systray_init();
    ewmh_init(_G_L);
    spawn_init();
    banning_init(_G_L);
    stack_init(_G_L);

    /* Parse and run configuration file */
    if (!luaA_parserc(&xdg, confpath, true))
        fatal("couldn't find any rc file");

    p_delete(&confpath);

    xdgWipeHandle(&xdg);

    /* scan existing windows */
    scan();

    {
        /* select for events */
        const uint32_t change_win_vals[] =
        {
            XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
                | XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW
                | XCB_EVENT_MASK_STRUCTURE_NOTIFY
                | XCB_EVENT_MASK_PROPERTY_CHANGE
                | XCB_EVENT_MASK_FOCUS_CHANGE
        };

        xcb_change_window_attributes(_G_connection,
                                     globalconf.screen->root,
                                     XCB_CW_EVENT_MASK,
                                     change_win_vals);
    }

    /* we will receive events, stop grabbing server */
    xcb_ungrab_server(_G_connection);
    xcb_flush(_G_connection);

    /* main event loop */
    ev_loop(_G_loop, 0);

    /* cleanup event loop */
    ev_ref(_G_loop);
    ev_check_stop(_G_loop, &xcheck);
    ev_ref(_G_loop);
    ev_prepare_stop(_G_loop, &a_refresh);
    ev_ref(_G_loop);
    ev_io_stop(_G_loop, &xio);

    awesome_atexit();

    return EXIT_SUCCESS;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
