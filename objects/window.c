/*
 * window.c - window object
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

#include "luaa.h"
#include "awesome.h"
#include "xwindow.h"
#include "ewmh.h"
#include "screen.h"
#include "objects/window.h"
#include "objects/tag.h"
#include "common/luaobject.h"
#include "common/xutil.h"
#include "common/xcursor.h"

/** Focused window */
window_t *_G_window_focused;

static void
window_wipe(window_t *window)
{
    p_delete(&window->cursor);
    button_array_wipe(&window->buttons);
    key_array_wipe(&window->keys);
}

bool
window_isvisible(lua_State *L, int idx)
{
    window_t *window = luaA_checkudata(L, idx, (lua_class_t *) &window_class);
    lua_interface_window_t *interface = (lua_interface_window_t *) luaA_class_get(L, idx);
    if(interface->isvisible)
        return interface->isvisible(window);
    return true;
}

/** Prepare banning a window by running all needed Lua events.
 * \param window The window.
 */
void
window_ban_unfocus(window_t *window)
{
    /* Wait until the last moment to take away the focus from the window. */
    if(_G_window_focused == window)
        /* Set focus on root window, so no events leak to the current window. */
        xcb_set_input_focus(_G_connection, XCB_INPUT_FOCUS_PARENT,
                            window->screen->protocol_screen->root->window, XCB_CURRENT_TIME);
}

/** Ban window.
 * \param window The window.
 */
void
window_ban(window_t *window)
{
    if(!window->banned && window->window)
    {
        xcb_unmap_window(_G_connection, window->window);
        window->banned = true;
        window_ban_unfocus(window);
    }
}


/** Unban a window.
 * \param window The window.
 */
void
window_unban(window_t *window)
{
    if(window->banned && window->window)
    {
        xcb_map_window(_G_connection, window->window);
        window->banned = false;
    }
}

/** Record that a window got focus.
 * \param window The window that got focus.
 */
void
window_focus_update(window_t *window)
{
    _G_window_focused = window;
    luaA_object_push(globalconf.L, window);
    luaA_object_emit_signal(globalconf.L, -1, "focus", 0);
    lua_pop(globalconf.L, 1);
}

/** Record that a window lost focus.
 * \param c Client being unfocused
 */
void
window_unfocus_update(window_t *window)
{
    _G_window_focused = NULL;
    luaA_object_push(globalconf.L, window);
    luaA_object_emit_signal(globalconf.L, -1, "unfocus", 0);
    lua_pop(globalconf.L, 1);
}

/** Give focus to window.
 * \param L The Lua VM state.
 * \param idx The window index on the stack.
 */
void
window_focus(lua_State *L, int idx)
{
    window_t *window = luaA_checkudata(L, idx, (lua_class_t *) &window_class);

    if(window->window)
    {
        /* If the window is banned but isvisible, unban it right now because you
         * can't set focus on unmapped window */
        if(window_isvisible(L, idx))
            window_unban(window);
        else
            return;

        /* Sets focus on window - using xcb_set_input_focus or WM_TAKE_FOCUS */
        if(window->focusable)
            xcb_set_input_focus(_G_connection, XCB_INPUT_FOCUS_PARENT,
                                window->window, XCB_CURRENT_TIME);
    }
}

/** Compute window geometry with respect to its geometry hints.
 * \param window The window.
 * \param geometry The geometry that the window might receive.
 * \return The geometry the window must take respecting its hints.
 */
area_t
window_geometry_hints(window_t *window, area_t geometry)
{
    int32_t basew, baseh, minw, minh;

    /* base size is substituted with min size if not specified */
    if(window->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
    {
        basew = window->size_hints.base_width;
        baseh = window->size_hints.base_height;
    }
    else if(window->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        basew = window->size_hints.min_width;
        baseh = window->size_hints.min_height;
    }
    else
        basew = baseh = 0;

    /* min size is substituted with base size if not specified */
    if(window->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        minw = window->size_hints.min_width;
        minh = window->size_hints.min_height;
    }
    else if(window->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
    {
        minw = window->size_hints.base_width;
        minh = window->size_hints.base_height;
    }
    else
        minw = minh = 0;

    if(window->size_hints.flags & XCB_SIZE_HINT_P_ASPECT
       && window->size_hints.min_aspect_num > 0
       && window->size_hints.min_aspect_den > 0
       && geometry.height - baseh > 0
       && geometry.width - basew > 0)
    {
        double dx = (double) (geometry.width - basew);
        double dy = (double) (geometry.height - baseh);
        double min = (double) window->size_hints.min_aspect_num / (double) window->size_hints.min_aspect_den;
        double max = (double) window->size_hints.max_aspect_num / (double) window->size_hints.min_aspect_den;
        double ratio = dx / dy;
        if(max > 0 && min > 0 && ratio > 0)
        {
            if(ratio < min)
            {
                dy = (dx * min + dy) / (min * min + 1);
                dx = dy * min;
                geometry.width = (int) dx + basew;
                geometry.height = (int) dy + baseh;
            }
            else if(ratio > max)
            {
                dy = (dx * min + dy) / (max * max + 1);
                dx = dy * min;
                geometry.width = (int) dx + basew;
                geometry.height = (int) dy + baseh;
            }
        }
    }

    if(minw)
        geometry.width = MAX(geometry.width, minw);
    if(minh)
        geometry.height = MAX(geometry.height, minh);

    if(window->size_hints.flags & XCB_SIZE_HINT_P_MAX_SIZE)
    {
        if(window->size_hints.max_width)
            geometry.width = MIN(geometry.width, window->size_hints.max_width);
        if(window->size_hints.max_height)
            geometry.height = MIN(geometry.height, window->size_hints.max_height);
    }

    if(window->size_hints.flags & (XCB_SIZE_HINT_P_RESIZE_INC | XCB_SIZE_HINT_BASE_SIZE)
       && window->size_hints.width_inc && window->size_hints.height_inc)
    {
        uint16_t t1 = geometry.width, t2 = geometry.height;
        unsigned_subtract(t1, basew);
        unsigned_subtract(t2, baseh);
        geometry.width -= t1 % window->size_hints.width_inc;
        geometry.height -= t2 % window->size_hints.height_inc;
    }

    return geometry;
}

static int
luaA_window_get_size_hints(lua_State *L, window_t *window)
{
    const char *u_or_p = NULL;

    lua_createtable(L, 0, 1);

    if(window->size_hints.flags & XCB_SIZE_HINT_US_POSITION)
        u_or_p = "user_position";
    else if(window->size_hints.flags & XCB_SIZE_HINT_P_POSITION)
        u_or_p = "program_position";

    if(u_or_p)
    {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, window->size_hints.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, window->size_hints.y);
        lua_setfield(L, -2, "y");
        lua_setfield(L, -2, u_or_p);
        u_or_p = NULL;
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_US_SIZE)
        u_or_p = "user_size";
    else if(window->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
        u_or_p = "program_size";

    if(u_or_p)
    {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, window->size_hints.width);
        lua_setfield(L, -2, "width");
        lua_pushnumber(L, window->size_hints.height);
        lua_setfield(L, -2, "height");
        lua_setfield(L, -2, u_or_p);
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        lua_pushnumber(L, window->size_hints.min_width);
        lua_setfield(L, -2, "min_width");
        lua_pushnumber(L, window->size_hints.min_height);
        lua_setfield(L, -2, "min_height");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_MAX_SIZE)
    {
        lua_pushnumber(L, window->size_hints.max_width);
        lua_setfield(L, -2, "max_width");
        lua_pushnumber(L, window->size_hints.max_height);
        lua_setfield(L, -2, "max_height");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_RESIZE_INC)
    {
        lua_pushnumber(L, window->size_hints.width_inc);
        lua_setfield(L, -2, "width_inc");
        lua_pushnumber(L, window->size_hints.height_inc);
        lua_setfield(L, -2, "height_inc");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_ASPECT)
    {
        lua_pushnumber(L, window->size_hints.min_aspect_num);
        lua_setfield(L, -2, "min_aspect_num");
        lua_pushnumber(L, window->size_hints.min_aspect_den);
        lua_setfield(L, -2, "min_aspect_den");
        lua_pushnumber(L, window->size_hints.max_aspect_num);
        lua_setfield(L, -2, "max_aspect_num");
        lua_pushnumber(L, window->size_hints.max_aspect_den);
        lua_setfield(L, -2, "max_aspect_den");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_BASE_SIZE)
    {
        lua_pushnumber(L, window->size_hints.base_width);
        lua_setfield(L, -2, "base_width");
        lua_pushnumber(L, window->size_hints.base_height);
        lua_setfield(L, -2, "base_height");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_WIN_GRAVITY)
    {
        switch(window->size_hints.win_gravity)
        {
          default:
            lua_pushliteral(L, "north_west");
            break;
          case XCB_GRAVITY_NORTH:
            lua_pushliteral(L, "north");
            break;
          case XCB_GRAVITY_NORTH_EAST:
            lua_pushliteral(L, "north_east");
            break;
          case XCB_GRAVITY_WEST:
            lua_pushliteral(L, "west");
            break;
          case XCB_GRAVITY_CENTER:
            lua_pushliteral(L, "center");
            break;
          case XCB_GRAVITY_EAST:
            lua_pushliteral(L, "east");
            break;
          case XCB_GRAVITY_SOUTH_WEST:
            lua_pushliteral(L, "south_west");
            break;
          case XCB_GRAVITY_SOUTH:
            lua_pushliteral(L, "south");
            break;
          case XCB_GRAVITY_SOUTH_EAST:
            lua_pushliteral(L, "south_east");
            break;
          case XCB_GRAVITY_STATIC:
            lua_pushliteral(L, "static");
            break;
        }
        lua_setfield(L, -2, "win_gravity");
    }

    return 1;
}

/** Get or set mouse buttons bindings on a window.
 * \param L The Lua VM state.
 * \return The number of elements pushed on the stack.
 */
static int
luaA_window_buttons(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);

    if(lua_gettop(L) == 2)
    {
        luaA_button_array_set(L, 1, 2, &window->buttons);
        luaA_object_emit_signal(L, 1, "property::buttons", 0);
        xwindow_buttons_grab(window->window, &window->buttons);
    }

    return luaA_button_array_get(L, &window->buttons);
}

/** Get or set keys bindings for a window.
 * \param L The Lua VM state.
 * \return The number of element pushed on stack.
 */
static int
luaA_window_keys(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, (lua_class_t *) &window_class);

    if(lua_gettop(L) == 2)
    {
        luaA_key_array_set(L, 1, 2, &window->keys);
        luaA_object_emit_signal(L, 1, "property::keys", 0);
        xwindow_grabkeys(window->window, &window->keys);
    }

    return luaA_key_array_get(L, &window->keys);
}

/** Check if a window is visible.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_isvisible(lua_State *L)
{
    lua_pushboolean(L, window_isvisible(L, 1));
    return 1;
}

static LUA_OBJECT_DO_SET_PROPERTY_FUNC(window, &window_class, window_t, focusable)

static int
luaA_window_set_cursor(lua_State *L, window_t *window)
{
    const char *buf = luaL_checkstring(L, -1);
    if(buf)
    {
        uint16_t cursor_font = xcursor_font_fromstr(buf);
        if(cursor_font)
        {
            xcb_cursor_t cursor = xcursor_new(_G_connection, cursor_font);
            p_delete(&window->cursor);
            window->cursor = a_strdup(buf);
            xwindow_set_cursor(window->window, cursor);
            luaA_object_emit_signal(L, -3, "property::cursor", 0);
        }
    }
    return 0;
}

static int
luaA_window_set_focusable(lua_State *L, window_t *c)
{
    window_set_focusable(L, -3, luaA_checkboolean(L, -1));
    return 0;
}

/** Give the focus to a window.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_focus(lua_State *L)
{
    window_focus(L, 1);
    return 0;
}

static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, window, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, cursor, lua_pushstring)
static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, parent, luaA_object_push)
LUA_OBJECT_EXPORT_PROPERTY(window, window_t, focusable, lua_pushboolean)

/** Get the window screen.
 * \param L The Lua VM state.
 * \param window The window object.
 * \return The number of elements pushed on stack.
 */
int
luaA_window_get_screen(lua_State *L, window_t *window)
{
    if(!window->screen)
        return 0;
    lua_pushnumber(L, screen_array_indexof(&globalconf.screens, window->screen) + 1);
    return 1;
}

void
window_class_setup(lua_State *L)
{
    static const struct luaL_reg window_methods[] =
    {
        LUA_CLASS_METHODS(window)
        { "buttons", luaA_window_buttons },
        { "focus", luaA_window_focus },
        { "keys", luaA_window_keys },
        { "isvisible", luaA_window_isvisible },
        { NULL, NULL }
    };

    luaA_class_setup(L, &window_class, "window", NULL,
                     (lua_class_allocator_t) window_new, (lua_class_collector_t) window_wipe, NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     window_methods, NULL, NULL);

    luaA_class_add_property(&window_class, A_TK_WINDOW,
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_window,
                            NULL);
    luaA_class_add_property(&window_class, A_TK_FOCUSABLE,
                            (lua_class_propfunc_t) luaA_window_set_focusable,
                            (lua_class_propfunc_t) luaA_window_get_focusable,
                            (lua_class_propfunc_t) luaA_window_set_focusable);
    luaA_class_add_property(&window_class, A_TK_CURSOR,
                            (lua_class_propfunc_t) luaA_window_set_cursor,
                            (lua_class_propfunc_t) luaA_window_get_cursor,
                            (lua_class_propfunc_t) luaA_window_set_cursor);
    luaA_class_add_property(&window_class, A_TK_SCREEN,
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_screen,
                            NULL);
    luaA_class_add_property(&window_class, A_TK_PARENT,
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_parent,
                            NULL);
    luaA_class_add_property(&window_class, A_TK_SIZE_HINTS,
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_size_hints,
                            NULL);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
