/*
 * luaobject.h - useful functions for handling Lua objects
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

#ifndef AWESOME_COMMON_LUAOBJECT
#define AWESOME_COMMON_LUAOBJECT

#include "common/luaclass.h"

void luaA_object_setup(lua_State *);
void luaA_object_store_registry(lua_State *, int);
int luaA_object_push(lua_State *, const void *);
void * luaA_object_ref(lua_State *, int);
void luaA_object_unref(lua_State *, const void *);
void * luaA_object_ref_item_from_stack(lua_State *, int, int);
void * luaA_object_ref_item(lua_State *, lua_object_t *, int);
void luaA_object_unref_item(lua_State *, int, const void *);

int signal_object_emit(lua_State *, const signal_array_t *, const char *, int)
    __attribute__ ((warn_unused_result));

void luaA_object_connect_signal(lua_State *, int, const char *, lua_CFunction);
void luaA_object_disconnect_signal(lua_State *, int, const char *, lua_CFunction);
void luaA_object_connect_signal_from_stack(lua_State *, int, const char *, int);
void luaA_object_disconnect_signal_from_stack(lua_State *, int, const char *, int);
int luaA_object_emit_signal(lua_State *, int, const char *, int)
    __attribute__ ((warn_unused_result));

static inline void
luaA_object_emit_signal_noret(lua_State *L, int idx, const char *name, int nargs)
{
    lua_pop(L, luaA_object_emit_signal(L, idx, name, nargs));
}

#define LUA_OBJECT_SIGNAL_FUNCS(prefix, type)                                  \
    static inline int                                                          \
    prefix##_emit_signal(lua_State *L, type *obj, const char *name, int nargs) \
    {                                                                          \
        /* Push object */                                                      \
        luaA_object_push(L, obj);                                              \
        /* Insert it before args */                                            \
        lua_insert(L, - nargs - 1);                                            \
        /* Emit signal */                                                      \
        int nret = luaA_object_emit_signal(L, - nargs - 1, name, nargs);       \
        /* Remove object */                                                    \
        lua_pop(L, 1);                                                         \
        return nret;                                                           \
    }                                                                          \
    static inline void                                                         \
    prefix##_emit_signal_noret(lua_State *L, type *obj, const char *name,      \
                               int nargs)                                      \
    {                                                                          \
        lua_pop(L, prefix##_emit_signal(L, obj, name, nargs));                 \
    }

#define LUA_OBJECT_FUNCS(lua_class, type, prefix)                              \
    static inline type *                                                       \
    prefix##_make_light(lua_State *L, type *item)                              \
    {                                                                          \
        lua_pushlightuserdata(L, item);                                        \
        luaA_settype(L, (lua_class));                                          \
        /* Store into registry for futur use */                                \
        luaA_object_store_registry(L, -1);                                     \
        /* Remove light userdata */                                            \
        lua_pop(L, 1);                                                         \
        return item;                                                           \
    }                                                                          \
    static inline type *                                                       \
    prefix##_new_light(lua_State *L)                                           \
    {                                                                          \
        return prefix##_make_light(L, p_new(type, 1));                         \
    }

#endif

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
