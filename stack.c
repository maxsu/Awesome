/*
 * stack.c - window stack management
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
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

#include "awesome.h"
#include "ewmh.h"
#include "stack.h"
#include "objects/client.h"
#include "objects/wibox.h"

static void
stack_window_remove(window_t *window)
{
    window_array_find_and_remove(&window->parent->childrens, window);
}

/** Push the window at the beginning of the window stack.
 * \param window The window to push.
 */
static void
stack_window_push(window_t *window)
{
    stack_window_remove(window);
    window_array_push(&window->parent->childrens, window);
}

/** Push the window at the end of the window stack.
 * \param window The window to push.
 */
static void
stack_window_append(window_t *window)
{
    stack_window_remove(window);
    window_array_append(&window->parent->childrens, window);
}

/** Put window on bottom of the stack.
 * \param L The Lua VM state.
 * \param idx The index of the window to raise.
 */
void
stack_window_lower(lua_State *L, int idx)
{
    window_t *window = luaA_checkudata(L, idx, &window_class);
    stack_window_push(window);
    luaA_object_emit_signal(L, idx, "lower", 0);
}

/** Put window on top of the stack.
 * \param L The Lua VM state.
 * \param idx The index of the window to raise.
 */
void
stack_window_raise(lua_State *L, int idx)
{
    window_t *window = luaA_checkudata(L, idx, &window_class);
    /* Push window on top of the stack. */
    stack_window_append(window);
    luaA_object_emit_signal(L, idx, "raise", 0);
}

/** Restack windows.
 * \todo It might be worth stopping to restack everyone and only stack `c'
 * relatively to the first matching in the list.
 */
static int
stack_refresh(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);
    xcb_window_t next = XCB_NONE;

    if(window->parent)
        for(int layer = INT8_MIN; layer <= INT8_MAX; layer++)
            foreach(node, window->parent->childrens)
                if((*node)->layer == layer)
                {
                    xcb_configure_window(_G_connection, (*node)->window,
                                         XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE,
                                         (uint32_t[]) { next, XCB_STACK_MODE_ABOVE });

                    next = (*node)->window;
                }

    return 0;
}

static int
luaA_stack_window_remove(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);
    if(!window->window)
        stack_window_remove(window);
    return 0;
}

void
stack_init(void)
{
    luaA_class_connect_signal(globalconf.L, &window_class, "property::layer", stack_refresh);
    luaA_class_connect_signal(globalconf.L, &window_class, "raise", stack_refresh);
    luaA_class_connect_signal(globalconf.L, &window_class, "lower", stack_refresh);
    luaA_class_connect_signal(globalconf.L, &window_class, "property::window", luaA_stack_window_remove);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
