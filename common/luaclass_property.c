/*
 * luaclass_property.c - useful functions for handling Lua objects properties
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

#include "common/lualib.h"
#include "common/luaclass_property.h"

static int
luaA_class_property_on_signal_set(lua_State *L)
{
    /* 3 args: object, key, value */
    luaL_checkany(L, 3);
    lua_settop(L, 3);

    /* Push key */
    lua_pushvalue(L, 2);
    /* Get upvalue[key] */
    lua_rawget(L, lua_upvalueindex(1));
    lua_class_propfunc_t func = lua_touserdata(L, -1);
    lua_remove(L, -1);
    if(func)
    {
        lua_object_t *object = luaA_checkudata(L, 1, lua_touserdata(L, lua_upvalueindex(2)));
        return func(L, object);
    }
    return 0;
}

static int
luaA_class_property_on_signal_get(lua_State *L)
{
    /* 2 args: object, key */
    luaL_checkany(L, 2);
    lua_settop(L, 2);

    /* Push key */
    lua_pushvalue(L, 2);
    /* Get upvalue[key] */
    lua_rawget(L, lua_upvalueindex(1));
    lua_class_propfunc_t func = lua_touserdata(L, -1);
    lua_remove(L, -1);
    if(func)
    {
        lua_object_t *object = luaA_checkudata(L, 1, lua_touserdata(L, lua_upvalueindex(2)));
        return func(L, object);
    }
    return 0;
}

void
luaA_class_property_setup(lua_State *L, lua_class_t *lua_class,
                          const lua_class_property_entry_t getter[], const lua_class_property_entry_t setter[])
{
    /* Put setter in a table */
    lua_newtable(L);
    for(const lua_class_property_entry_t *entry = setter; entry->name; entry++)
    {
        lua_pushstring(L, entry->name);
        lua_pushlightuserdata(L, entry->func);
        lua_rawset(L, -3);
    }
    lua_pushlightuserdata(L, lua_class);
    /* Push C closure with its upvalue, the table and the class */
    lua_pushcclosure(L, luaA_class_property_on_signal_set, 2);
    /* Register C closure with property signal */
    luaA_class_connect_signal_from_stack(L, lua_class, "property::set", -1);

    /* Put getter in a table */
    lua_newtable(L);
    for(const lua_class_property_entry_t *entry = getter; entry->name; entry++)
    {
        lua_pushstring(L, entry->name);
        lua_pushlightuserdata(L, entry->func);
        lua_rawset(L, -3);
    }
    lua_pushlightuserdata(L, lua_class);
    /* Push C closure with its upvalue, the table and the class */
    lua_pushcclosure(L, luaA_class_property_on_signal_get, 2);
    /* Register C closure with property signal */
    luaA_class_connect_signal_from_stack(L, lua_class, "property::get", -1);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
