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

int luaA_settype(lua_State *, lua_class_t *);
void luaA_object_setup(lua_State *);
void luaA_object_store_registry(lua_State *, int);
int luaA_object_push(lua_State *, const void *);
void * luaA_object_ref(lua_State *, int);
void luaA_object_unref(lua_State *, const void *);
void * luaA_object_ref_item(lua_State *, int, int);
void luaA_object_unref_item(lua_State *, int, const void *);

void signal_object_emit(lua_State *, const signal_array_t *, const char *, int);

void luaA_object_connect_signal(lua_State *, int, const char *, lua_CFunction);
void luaA_object_disconnect_signal(lua_State *, int, const char *, lua_CFunction);
void luaA_object_connect_signal_from_stack(lua_State *, int, const char *, int);
void luaA_object_disconnect_signal_from_stack(lua_State *, int, const char *, int);
void luaA_object_emit_signal(lua_State *, int, const char *, int);

#define LUA_OBJECT_FUNCS(lua_class, type, prefix)                              \
    LUA_CLASS_FUNCS(prefix, lua_class)                                         \
    static inline void                                                         \
    prefix##_emit_signal(lua_State *L, type *obj, const char *name, int nargs) \
    {                                                                          \
        /* Push object */                                                      \
        luaA_object_push(L, obj);                                              \
        /* Insert it before args */                                            \
        lua_insert(L, - nargs - 1);                                            \
        /* Emit signal */                                                      \
        luaA_object_emit_signal(L, - nargs - 1, name, nargs);                  \
        /* Remove object */                                                    \
        lua_pop(L, 1);                                                         \
    }                                                                          \
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

#define OBJECT_EXPORT_PROPERTY(pfx, type, field) \
    fieldtypeof(type, field) \
    pfx##_get_##field(type *object) \
    { \
        return object->field; \
    }

#define LUA_OBJECT_EXPORT_PROPERTY(pfx, type, field, pusher) \
    int \
    luaA_##pfx##_get_##field(lua_State *L, type *object) \
    { \
        pusher(L, object->field); \
        return 1; \
    }

#define LUA_OBJECT_DO_SET_PROPERTY_FUNC(pfx, lua_class, type, prop) \
    void \
    pfx##_set_##prop(lua_State *L, int idx, fieldtypeof(type, prop) value) \
    { \
        type *item = luaA_checkudata(L, idx, lua_class); \
        if(item->prop != value) \
        { \
            item->prop = value; \
            luaA_object_emit_signal(L, idx, "property::" #prop, 0); \
        } \
    }

#define LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(pfx, type, prop, checker) \
    int \
    luaA_##pfx##_set_##prop(lua_State *L, type *c) \
    { \
        pfx##_set_##prop(L, -3, checker(L, -1)); \
        return 0; \
    }

#define LUA_OBJECT_DO_SET_PROPERTY_WITH_REF_FUNC(prefix, lua_class, target_class, type, prop) \
    void \
    prefix##_set_##prop(lua_State *L, int idx, int vidx) \
    { \
        type *item = luaA_checkudata(L, idx, (lua_class)); \
        idx = luaA_absindex(L, idx); \
        vidx = luaA_absindex(L, vidx); \
        luaA_checkudata(L, vidx, (target_class)); \
        item->prop = luaA_object_ref_item(L, idx, vidx); \
        luaA_object_emit_signal(L, idx < vidx ? idx : idx - 1, "property::" #prop, 0); \
    }

#endif

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
