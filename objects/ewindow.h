/*
 * ewindow.h - Extended window object header
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

#ifndef AWESOME_OBJECTS_EWINDOW_H
#define AWESOME_OBJECTS_EWINDOW_H

#include "strut.h"
#include "objects/window.h"
#include "objects/button.h"
#include "common/luaclass.h"

#define EWINDOW_OBJECT_HEADER \
    WINDOW_OBJECT_HEADER \
    /** Opacity */ \
    double opacity; \
    /** Strut */ \
    strut_t strut; \
    /** Border color */ \
    xcolor_t border_color; \
    /** Border width */ \
    uint16_t border_width; \
    /** Window tags */ \
    tag_array_t tags; \
    /** True if the window is sticky */ \
    bool sticky;

/** Window structure */
typedef struct
{
    EWINDOW_OBJECT_HEADER
} ewindow_t;

lua_interface_window_t ewindow_class;

void ewindow_class_setup(lua_State *);

bool ewindow_isvisible(ewindow_t *);

void ewindow_set_opacity(lua_State *, int, double);
void ewindow_set_border_width(lua_State *, int, int);
void ewindow_set_sticky(lua_State *, int, bool);

DO_ARRAY(ewindow_t *, ewindow, DO_NOTHING)

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
