/*
 * window.h - window object header
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

#ifndef AWESOME_OBJECTS_WINDOW_H
#define AWESOME_OBJECTS_WINDOW_H

#include "globalconf.h"
#include "area.h"
#include "objects/button.h"
#include "objects/key.h"
#include "common/luaclass.h"

#define WINDOW_OBJECT_HEADER \
    LUA_OBJECT_HEADER \
    /** The X window number */ \
    xcb_window_t window; \
    /** The frame window, might be XCB_NONE */ \
    xcb_window_t frame_window; \
    /** Cursor */ \
    char *cursor; \
    /** Client logical screen */ \
    screen_t *screen; \
    /** Button bindings */ \
    button_array_t buttons; \
    /** True if the window is banned from the view */ \
    bool banned; \
    /** True if the window can have focus */ \
    bool focusable; \
    /** Key bindings */ \
    key_array_t keys; \
    /** Parent window */ \
    window_t *parent; \
    /** Window geometry */ \
    area_t geometry; \
    /** Window stack */ \
    ewindow_array_t stack;

typedef struct window_t window_t;
/** Window structure */
struct window_t
{
    WINDOW_OBJECT_HEADER
};

typedef bool (*lua_interface_window_isvisible_t)(window_t *);

typedef struct
{
    LUA_CLASS_HEADER
    /** The function to call to know if a window is visible */
    lua_interface_window_isvisible_t isvisible;
} lua_interface_window_t;

lua_class_t window_class;
LUA_OBJECT_FUNCS(&window_class, window_t, window)

void window_class_setup(lua_State *);

void window_ban(window_t *);
void window_ban_unfocus(window_t *);
void window_unban(window_t *);
void window_focus_update(window_t *);
void window_unfocus_update(window_t *);
void window_focus(lua_State *, int);

void window_set_opacity(lua_State *, int, double);
void window_set_border_width(lua_State *, int, int);
void window_set_sticky(lua_State *, int, bool);

bool window_isvisible(lua_State *, int);

int luaA_window_get_focusable(lua_State *, window_t *);
int luaA_window_get_screen(lua_State *, window_t *);

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
