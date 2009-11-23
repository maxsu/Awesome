/*
 *x window.h -x window handling functions header
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

#ifndef AWESOME_WINDOW_H
#define AWESOME_WINDOW_H

#include "draw.h"
#include "objects/key.h"

void xwindow_set_state(xcb_window_t, long);
xcb_get_property_cookie_t xwindow_get_state_unchecked(xcb_window_t);
uint32_t xwindow_get_state_reply(xcb_get_property_cookie_t);
void xwindow_configure(xcb_window_t, area_t, int);
double xwindow_get_opacity(xcb_window_t);
double xwindow_get_opacity_from_reply(xcb_get_property_reply_t *);
void xwindow_set_opacity(xcb_window_t, double);
void xwindow_grabkeys(xcb_window_t, key_array_t *);
void xwindow_takefocus(xcb_window_t);
void xwindow_set_cursor(xcb_window_t, xcb_cursor_t);
void xwindow_set_border_color(xcb_window_t, xcolor_t *);

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
