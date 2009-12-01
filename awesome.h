/*
 * awesome.h - awesome main header
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
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

#ifndef AWESOME_AWESOME_H
#define AWESOME_AWESOME_H

#include "color.h"

void awesome_restart(void);
void awesome_atexit(void);

/** Connection ref */
xcb_connection_t *_G_connection;
/** Default screen number */
int _G_default_screen;
/** The event loop */
struct ev_loop *_G_loop;
/** Default foreground and background colors */
xcolor_t _G_fg, _G_bg;

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
