/*
 * luaobject.c - useful functions for handling Lua objects
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

#include "common/luaobject.h"

static const int LUAA_OBJECT_REGISTRY_KEY;
static const int LUAA_OBJECT_REGISTRY_REFCOUNT_KEY;

static void
luaA_object_registry_push(lua_State *L)
{
    lua_pushlightuserdata(L, (void *) &LUAA_OBJECT_REGISTRY_KEY);
    lua_rawget(L, LUA_REGISTRYINDEX);
}

void
luaA_object_registry_refcount_push(lua_State *L)
{
    lua_pushlightuserdata(L, (void *) &LUAA_OBJECT_REGISTRY_REFCOUNT_KEY);
    lua_rawget(L, LUA_REGISTRYINDEX);
}

/** Setup the object system at startup.
 * \param L The Lua VM state.
 */
void
luaA_object_setup(lua_State *L)
{
    /* Push identification string */
    lua_pushlightuserdata(L, (void *) &LUAA_OBJECT_REGISTRY_KEY);
    /* Create an empty table */
    lua_newtable(L);
    /* Create its metatable with __mode = 'v'  */
    lua_newtable(L);
    lua_pushliteral(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);
    /* Register table inside registry */
    lua_rawset(L, LUA_REGISTRYINDEX);

    /* Push identification string */
    lua_pushlightuserdata(L, (void *) &LUAA_OBJECT_REGISTRY_REFCOUNT_KEY);
    /* Create an empty table */
    lua_newtable(L);
    /* Register table inside registry */
    lua_rawset(L, LUA_REGISTRYINDEX);
}

static int
luaA_object_get_refcount(lua_State *L, int tud, int oud)
{
    tud = luaA_absindex(L, tud);
    lua_pushvalue(L, oud);
    /* Get the number of references */
    lua_rawget(L, tud);
    /* Get the number of references */
    int count = lua_tonumber(L, -1);
    /* Remove counter */
    lua_pop(L, 1);
    return count;
}

static void
luaA_object_set_refcount(lua_State *L, int tud, int oud, int count)
{
    tud = luaA_absindex(L, tud);
    lua_pushvalue(L, oud);
    /* Push value */
    if(count)
        lua_pushinteger(L, count);
    else
        lua_pushnil(L);
    /* Get the number of references */
    lua_rawset(L, tud);
}

/** Increment a object reference in its store table.
 * \param L The Lua VM state.
 * \param tud The table index on the stack, where to store reference counting.
 * \param oud The object index on the stack.
 * \return A pointer to the object.
 */
void *
luaA_object_incref(lua_State *L, int tud, int oud)
{
    /* Get pointer value of the item */
    void *pointer = (void *) lua_topointer(L, oud);

    /* Not reference able. */
    if(pointer)
    {
        int count = luaA_object_get_refcount(L, tud, oud) + 1;

        luaA_object_set_refcount(L, tud, oud, count);

        /* New guy! */
        if(count == 1)
        {
            oud = luaA_absindex(L, oud);
            /* Store into registry for futur use */
            luaA_object_registry_push(L);
            lua_pushlightuserdata(L, pointer);
            lua_pushvalue(L, oud);
            /* Set registry[data pointer] = data */
            lua_rawset(L, -3);
            /* Remove registry */
            lua_pop(L, 1);
        }
    }

    /* Remove referenced item */
    lua_remove(L, oud);
    return pointer;
}

/** Decrement a object reference in its store table.
 * \param L The Lua VM state.
 * \param tud The table index on the stack, where to store reference counting.
 * \param oud The object index on the stack.
 * \return A pointer to the object.
 */
void
luaA_object_decref(lua_State *L, int tud, int oud)
{
    /* Check that the object is referencable */
    if(lua_topointer(L, oud))
        luaA_object_set_refcount(L, tud, oud, luaA_object_get_refcount(L, tud, oud) - 1);
}

/** Push a referenced object onto the stack.
 * \param L The Lua VM state.
 * \param pointer The object to push.
 * \return The number of element pushed on stack.
 */
int
luaA_object_push(lua_State *L, void *pointer)
{
    luaA_object_registry_push(L);
    lua_pushlightuserdata(L, pointer);
    lua_rawget(L, -2);
    lua_remove(L, -2);
    return 1;
}

int
luaA_settype(lua_State *L, lua_class_t *lua_class)
{
    lua_pushlightuserdata(L, lua_class);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_setmetatable(L, -2);
    return 1;
}

void
luaA_object_connect_signal(lua_State *L, int oud,
                           const char *name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn);
    luaA_object_connect_signal_from_stack(L, oud, name, -1);
}

void
luaA_object_disconnect_signal(lua_State *L, int oud,
                              const char *name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn);
    luaA_object_disconnect_signal_from_stack(L, oud, name, -1);
}

/** Add a signal to an object.
 * \param L The Lua VM state.
 * \param oud The object index on the stack.
 * \param name The name of the signal.
 * \param ud The index of function to call when signal is emitted.
 */
void
luaA_object_connect_signal_from_stack(lua_State *L, int oud,
                                      const char *name, int ud)
{
    luaA_checkfunction(L, ud);
    lua_object_t *obj = lua_touserdata(L, oud);
    signal_add(&obj->signals, name, luaA_object_ref_item(L, oud, ud));
}

/** Remove a signal to an object.
 * \param L The Lua VM state.
 * \param oud The object index on the stack.
 * \param name The name of the signal.
 * \param ud The index of function to call when signal is emitted.
 */
void
luaA_object_disconnect_signal_from_stack(lua_State *L, int oud,
                                         const char *name, int ud)
{
    luaA_checkfunction(L, ud);
    lua_object_t *obj = lua_touserdata(L, oud);
    void *ref = (void *) lua_topointer(L, ud);
    signal_remove(&obj->signals, name, ref);
    luaA_object_unref_item(L, oud, ref);
    lua_remove(L, ud);
}

void
signal_object_emit(lua_State *L, signal_array_t *arr, const char *name, int nargs)
{
    signal_t *sigfound = signal_array_getbyid(arr,
                                              a_strhash((const unsigned char *) name));

    if(sigfound)
    {
        int nbfunc = sigfound->sigfuncs.len;
        luaL_checkstack(L, lua_gettop(L) + nbfunc + nargs + 1, "too much signal");
        /* Push all functions and then execute, because this list can change
         * while executing funcs. */
        foreach(func, sigfound->sigfuncs)
            luaA_object_push(L, (void *) *func);

        for(int i = 0; i < nbfunc; i++)
        {
            /* push all args */
            for(int j = 0; j < nargs; j++)
                lua_pushvalue(L, - nargs - nbfunc + i);
            /* push first function */
            lua_pushvalue(L, - nargs - nbfunc + i);
            /* remove this first function */
            lua_remove(L, - nargs - nbfunc - 1 + i);
            luaA_dofunction(L, nargs, 0);
        }
    }
    /* remove args */
    lua_pop(L, nargs);
}

/** Emit a signal to an object.
 * \param L The Lua VM state.
 * \param oud The object index on the stack.
 * \param name The name of the signal.
 * \param nargs The number of arguments to pass to the called functions.
 */
void
luaA_object_emit_signal(lua_State *L, int oud,
                        const char *name, int nargs)
{
    int oud_abs = luaA_absindex(L, oud);
    lua_object_t *obj = lua_touserdata(L, oud);
    if(!obj)
        luaL_error(L, "trying to emit signal on non-object");
    signal_t *sigfound = signal_array_getbyid(&obj->signals,
                                              a_strhash((const unsigned char *) name));
    if(sigfound)
    {
        int nbfunc = sigfound->sigfuncs.len;
        luaL_checkstack(L, lua_gettop(L) + nbfunc + nargs + 2, "too much signal");
        /* Push all functions and then execute, because this list can change
         * while executing funcs. */
        foreach(func, sigfound->sigfuncs)
            luaA_object_push_item(L, oud_abs, (void *) *func);

        for(int i = 0; i < nbfunc; i++)
        {
            /* push object */
            lua_pushvalue(L, oud_abs);
            /* push all args */
            for(int j = 0; j < nargs; j++)
                lua_pushvalue(L, - nargs - nbfunc - 1 + i);
            /* push first function */
            lua_pushvalue(L, - nargs - nbfunc - 1 + i);
            /* remove this first function */
            lua_remove(L, - nargs - nbfunc - 2 + i);
            luaA_dofunction(L, nargs + 1, 0);
        }
    }
    /* Then emit signal on the class */
    lua_pushvalue(L, oud);
    lua_insert(L, - nargs - 1);
    luaA_class_emit_signal(L, luaA_class_get(L, - nargs - 1), name, nargs + 1);
}

int
luaA_object_connect_signal_simple(lua_State *L)
{
    luaA_object_connect_signal_from_stack(L, 1, luaL_checkstring(L, 2), 3);
    return 0;
}

int
luaA_object_disconnect_signal_simple(lua_State *L)
{
    luaA_object_disconnect_signal_from_stack(L, 1, luaL_checkstring(L, 2), 3);
    return 0;
}

int
luaA_object_emit_signal_simple(lua_State *L)
{
    luaA_object_emit_signal(L, 1, luaL_checkstring(L, 2), lua_gettop(L) - 2);
    return 0;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
