/*
 * mouse.c - mouse managing
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

#include "mouse.h"
#include "objects/client.h"
#include "awesome.h"

/** Get the pointer position.
 * \param window The window to get position on.
 * \param x will be set to the Pointer-x-coordinate relative to window
 * \param y will be set to the Pointer-y-coordinate relative to window
 * \param child Will be set to the window under the pointer.
 * \param mask will be set to the current buttons state
 * \return true on success, false if an error occurred
 **/
static bool
mouse_query_pointer(xcb_window_t window, int16_t *x, int16_t *y, xcb_window_t *child, uint16_t *mask)
{
    xcb_query_pointer_cookie_t query_ptr_c = xcb_query_pointer_unchecked(_G_connection, window);
    xcb_query_pointer_reply_t *query_ptr_r = xcb_query_pointer_reply(_G_connection, query_ptr_c, NULL);

    if(!query_ptr_r || !query_ptr_r->same_screen)
        return false;

    *x = query_ptr_r->win_x;
    *y = query_ptr_r->win_y;
    *mask = query_ptr_r->mask;
    *child = query_ptr_r->child;

    p_delete(&query_ptr_r);

    return true;
}

static int
luaA_mouse_pushmask(lua_State *L, uint16_t mask)
{
    lua_createtable(L, 5, 0);

    int i = 1;

    for(uint16_t maski = XCB_BUTTON_MASK_1; maski <= XCB_BUTTON_MASK_5; maski <<= 1)
    {
        if(mask & maski)
            lua_pushboolean(L, true);
        else
            lua_pushboolean(L, false);
        lua_rawseti(L, -2, i++);
    }

    return 1;
}

/** Push a table with mouse status.
 * \param L The Lua VM state.
 * \param x The x coordinate.
 * \param y The y coordinate.
 * \param mask The button mask.
 */
int
luaA_mouse_pushstatus(lua_State *L, int x, int y, uint16_t mask)
{
    lua_createtable(L, 0, 2);
    lua_pushnumber(L, x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, y);
    lua_setfield(L, -2, "y");

    luaA_mouse_pushmask(L, mask);
    lua_setfield(L, -2, "buttons");

    return 1;
}

/** Query mouse information.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_mouse_query(lua_State *L)
{
    uint16_t mask;
    int16_t x, y;
    xcb_window_t child;

    if(!mouse_query_pointer(globalconf.screen->root, &x, &y, &child, &mask))
        return 0;

    luaA_object_push(L, ewindow_getbywin(child));
    lua_pushinteger(L, x);
    lua_pushinteger(L, y);
    luaA_mouse_pushmask(L, mask);

    return 4;
}

static int
luaA_mouse_warp(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);
    int x = luaL_checknumber(L, 2), y = luaL_checknumber(L, 3);
    xcb_warp_pointer(_G_connection, XCB_NONE, window->window, 0, 0, 0, 0, x, y);
    return 0;
}

const struct luaL_reg awesome_mouse_methods[] =
{
    { "query", luaA_mouse_query },
    { "warp", luaA_mouse_warp },
    { NULL, NULL }
};
const struct luaL_reg awesome_mouse_meta[] =
{
    { NULL, NULL }
};

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
