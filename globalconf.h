/*
 * globalconf.h - basic globalconf.header
 *
 * Copyright © 2007-2009 Julien Danjou <julien@danjou.info>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#ifndef AWESOME_GLOBALCONF_H
#define AWESOME_GLOBALCONF_H

#include <ev.h>

#include <xcb/xcb_icccm.h>
#include <xcb/xcb_keysyms.h>

#define SN_API_NOT_YET_FROZEN
#include <libsn/sn.h>

#include "objects/key.h"
#include "objects/button.h"
#include "color.h"
#include "common/xembed.h"

typedef struct wibox_t wibox_t;
typedef struct a_screen screen_t;
typedef struct widget_t widget_t;
typedef struct client_t client_t;
typedef struct tag tag_t;
typedef struct ewindow_t ewindow_t;

ARRAY_TYPE(tag_t *, tag)
ARRAY_TYPE(screen_t, screen)
ARRAY_TYPE(client_t *, client)
ARRAY_TYPE(wibox_t *, wibox)
ARRAY_TYPE(ewindow_t *, ewindow)

/** Main configuration structure */
typedef struct
{
    /** Keys symbol table */
    xcb_key_symbols_t *keysyms;
    /** Logical screens */
    screen_array_t screens;
    /** Root window key bindings */
    key_array_t keys;
    /** Clients list */
    client_array_t clients;
    /** Embedded windows */
    xembed_window_array_t embedded;
    /** Lua VM state */
    lua_State *L;
    /** Default colors */
    struct
    {
        xcolor_t fg, bg;
    } colors;
    /** The event loop */
    struct ev_loop *loop;
    /** Wiboxes */
    wibox_array_t wiboxes;
    /** Latest timestamp we got from the X server */
    xcb_timestamp_t timestamp;
    /** Window that contains the systray */
    struct
    {
        xcb_window_t window;
        /** Systray window parent */
        xcb_window_t parent;
        /** Is awesome the systray owner? */
        bool registered;
    } systray;
    /** The monitor of startup notifications */
    SnMonitorContext *snmonitor;
    /** The default visual, used to draw */
    xcb_visualtype_t *visual;
    /** The screen's information */
    xcb_screen_t *screen;
    /** A graphic context. */
    xcb_gcontext_t gc;
} awesome_t;

extern awesome_t globalconf;

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
