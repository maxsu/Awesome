/*
 * padding.h - padding definition
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

#ifndef AWESOME_PADDING_H
#define AWESOME_PADDING_H

#include "luaa.h"

/** Padding type */
typedef struct
{
    /** Padding at top */
    int top;
    /** Padding at bottom */
    int bottom;
    /** Padding at left */
    int left;
    /** Padding at right */
    int right;
} padding_t;

/** Get an optional padding table from a Lua table.
 * \param L The Lua VM state.
 * \param idx The table index on the stack.
 * \param dpadding The default padding value to use.
 */
static inline padding_t
luaA_getopt_padding(lua_State *L, int idx, padding_t *dpadding)
{
    padding_t padding;

    luaA_checktable(L, idx);

    padding.right = luaA_getopt_number(L, idx, "right", dpadding->right);
    padding.left = luaA_getopt_number(L, idx, "left", dpadding->left);
    padding.top = luaA_getopt_number(L, idx, "top", dpadding->top);
    padding.bottom = luaA_getopt_number(L, idx, "bottom", dpadding->bottom);

    return padding;
}

/** Push a padding structure into a table on the Lua stack.
 * \param L The Lua VM state.
 * \param padding The padding struct pointer.
 * \return The number of elements pushed on stack.
 */
static inline int
luaA_pushpadding(lua_State *L, padding_t *padding)
{
    lua_createtable(L, 0, 4);
    lua_pushnumber(L, padding->right);
    lua_setfield(L, -2, "right");
    lua_pushnumber(L, padding->left);
    lua_setfield(L, -2, "left");
    lua_pushnumber(L, padding->top);
    lua_setfield(L, -2, "top");
    lua_pushnumber(L, padding->bottom);
    lua_setfield(L, -2, "bottom");
    return 1;
}

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
