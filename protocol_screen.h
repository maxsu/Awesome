/*
 * protocol_screen.h - X protocol screen definition
 *
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
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

#ifndef AWESOME_PROTOCOL_SCREEN_H
#define AWESOME_PROTOCOL_SCREEN_H

#include "objects/window.h"

/** Structure defining a screen in the sense of the X protocol means it.  */
typedef struct
{
    /** The default visual, used to draw */
    xcb_visualtype_t *visual;
    /** The monitor of startup notifications */
    SnMonitorContext *snmonitor;
    /** Window that contains the systray */
    window_t *systray;
    /** Screen's root window */
    window_t *root;
    /** Embedded windows */
    xembed_window_array_t embedded;
} protocol_screen_t;

DO_ARRAY(protocol_screen_t, protocol_screen, DO_NOTHING)

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
