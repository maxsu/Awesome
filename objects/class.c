/*
 * class.c - Class management
 *
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
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

#include "class.h"
#include "luaa.h"
#include "common/luaobject.h"

static lua_class_t luaclass_class;
LUA_OBJECT_SIGNAL_FUNCS(class, lua_class_t)

static int
luaA_luaclass_new(lua_State *L)
{
    return luaA_class_new(L, &luaclass_class);
}

static int
luaA_luaclass_set_parent(lua_State *L, lua_class_t *luaclass)
{
    return 0;
}

static int
luaA_luaclass_get_parent(lua_State *L, lua_class_t *luaclass)
{
    luaA_object_push(L, NULL);
    return 1;
}

LUA_CLASS_FUNCS(luaclass, &luaclass_class)

void
luaclass_class_setup(lua_State *L)
{
    static const struct luaL_reg luaclass_methods[] =
    {
        LUA_CLASS_METHODS(luaclass)
        { NULL, NULL }
    };

    static const struct luaL_reg luaclass_module_meta[] =
    {
        { "__call", luaA_luaclass_new },
        { NULL, NULL },
    };

    luaA_class_setup(L, &luaclass_class, "class", NULL, sizeof(lua_class_t),
                     NULL, NULL, NULL,
                     luaclass_methods, luaclass_module_meta, NULL);
    luaA_class_add_property(&luaclass_class, "parent",
                            (lua_class_propfunc_t) luaA_luaclass_set_parent,
                            (lua_class_propfunc_t) luaA_luaclass_get_parent,
                            NULL);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
