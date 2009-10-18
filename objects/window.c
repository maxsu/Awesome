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
#include "xwindow.h"
#include "ewmh.h"
#include "screen.h"
#include "objects/window.h"
#include "objects/tag.h"
#include "common/luaobject.h"
#include "common/xutil.h"
#include "common/xcursor.h"

/** Focused window */
window_t *window_focused;

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
    if(window_focused == window)
    {
        xcb_window_t root_win = xutil_screen_get(globalconf.connection, window->screen->phys_screen)->root;
        /* Set focus on root window, so no events leak to the current window. */
        xcb_set_input_focus(globalconf.connection, XCB_INPUT_FOCUS_PARENT, root_win, XCB_CURRENT_TIME);
    }
}

/** Ban window.
 * \param window The window.
 */
void
window_ban(window_t *window)
{
    if(!window->banned)
    {
        xcb_unmap_window(globalconf.connection, window->window);
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
    if(window->banned)
    {
        xcb_map_window(globalconf.connection, window->window);
        window->banned = false;
    }
}

/** Record that a window got focus.
 * \param window The window that got focus.
 */
void
window_focus_update(window_t *window)
{
    window_focused = window;
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
    window_focused = NULL;
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
    /* If the window is banned but isvisible, unban it right now because you
     * can't set focus on unmapped window */
    if(window_isvisible(L, idx))
        window_unban(window);
    else
        return;

    /* Sets focus on window - using xcb_set_input_focus or WM_TAKE_FOCUS */
    if(window->focusable)
        xcb_set_input_focus(globalconf.connection, XCB_INPUT_FOCUS_PARENT,
                            window->window, XCB_CURRENT_TIME);

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

    return luaA_button_array_get(L, 1, &window->buttons);
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
        xcb_ungrab_key(globalconf.connection, XCB_GRAB_ANY, window->window, XCB_BUTTON_MASK_ANY);
        xwindow_grabkeys(window->window, &window->keys);
    }

    return luaA_key_array_get(L, 1, &window->keys);
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
            xcb_cursor_t cursor = xcursor_new(globalconf.connection, cursor_font);
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
LUA_OBJECT_EXPORT_PROPERTY(window, window_t, focusable, lua_pushboolean)

void
window_class_setup(lua_State *L)
{
    static const struct luaL_reg window_methods[] =
    {
        LUA_CLASS_METHODS(window)
        { NULL, NULL }
    };

    static const struct luaL_reg window_meta[] =
    {
        { "buttons", luaA_window_buttons },
        { "focus", luaA_window_focus },
        { "keys", luaA_window_keys },
        { "isvisible", luaA_window_isvisible },
        { NULL, NULL }
    };

    luaA_class_setup(L, &window_class, "window", NULL,
                     (lua_class_allocator_t) window_new, (lua_class_collector_t) window_wipe, NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     window_methods, window_meta);

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
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
