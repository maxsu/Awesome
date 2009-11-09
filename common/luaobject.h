/*
 * luaobject.h - useful functions for handling Lua objects
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

#ifndef AWESOME_COMMON_LUAOBJECT
#define AWESOME_COMMON_LUAOBJECT

#include "common/luaclass.h"

int luaA_settype(lua_State *, lua_class_t *);
void luaA_object_setup(lua_State *);
void * luaA_object_incref(lua_State *, int, int);
void luaA_object_decref(lua_State *, int, int);
void luaA_object_registry_refcount_push(lua_State *);
void luaA_object_registry_push(lua_State *);
int luaA_object_push(lua_State *, const void *);
void luaA_object_store_registry(lua_State *, int);

/** Reference an object and return a pointer to it.
 * That only works with userdata, table, thread or function.
 * \param L The Lua VM state.
 * \param oud The object index on the stack.
 * \return The object reference, or NULL if not referenceable.
 */
static inline void *
luaA_object_ref(lua_State *L, int oud)
{
    oud = luaA_absindex(L, oud);
    luaA_object_registry_refcount_push(L);
    void *p = luaA_object_incref(L, -1, oud);
    lua_pop(L, 1);
    return p;
}

/** Reference an object and return a pointer to it checking its type.
 * That only works with userdata.
 * \param L The Lua VM state.
 * \param oud The object index on the stack.
 * \param class The class of object expected
 * \return The object reference, or NULL if not referenceable.
 */
static inline void *
luaA_object_ref_class(lua_State *L, int oud, lua_class_t *class)
{
    luaA_checkudata(L, oud, class);
    return luaA_object_ref(L, oud);
}

/** Unreference an object and return a pointer to it.
 * That only works with userdata, table, thread or function.
 * \param L The Lua VM state.
 * \param oud The object index on the stack.
 */
static inline void
luaA_object_unref_from_stack(lua_State *L, int oud)
{
    oud = luaA_absindex(L, oud);
    luaA_object_registry_refcount_push(L);
    luaA_object_decref(L, -1, oud);
    lua_pop(L, 1);
}

/** Unreference an object and return a pointer to it.
 * That only works with userdata, table, thread or function.
 * \param L The Lua VM state.
 * \param pointer The object pointer.
 */
static inline void
luaA_object_unref(lua_State *L, const void *pointer)
{
    luaA_object_push(L, pointer);
    luaA_object_unref_from_stack(L, -1);
    lua_pop(L, 1);
}

/** Store an item in the environment table of an object.
 * \param L The Lua VM state.
 * \param ud The index of the object on the stack.
 * \param iud The index of the item on the stack.
 * \return The item reference.
 */
static inline void *
luaA_object_ref_item(lua_State *L, int ud, int iud)
{
    iud = luaA_absindex(L, iud);
    /* Get the env table from the object */
    lua_getfenv(L, ud);
    void *pointer;
    /* If it has not an env table, it might be a lightuserdata, so use the
     * global registry */
    if(lua_isnil(L, -1))
        pointer = luaA_object_ref(L, iud);
    else
        pointer = luaA_object_incref(L, -1, iud);
    /* Remove env table (or nil) */
    lua_pop(L, 1);
    return pointer;
}

/** Unref an item from the environment table of an object.
 * \param L The Lua VM state.
 * \param ud The index of the object on the stack.
 * \param iud The index of the item on the stack.
 */
static inline void
luaA_object_unref_item_from_stack(lua_State *L, int ud, int iud)
{
    iud = luaA_absindex(L, iud);
    /* Get the env table from the object */
    lua_getfenv(L, ud);
    /* No env table? Then we unref from the global registry */
    if(lua_isnil(L, -1))
        luaA_object_unref_from_stack(L, iud);
    else
        /* Decrement */
        luaA_object_decref(L, -1, iud);
    /* Remove env table (or nil) */
    lua_pop(L, 1);
}

/** Unref an item from the environment table of an object.
 * \param L The Lua VM state.
 * \param ud The index of the object on the stack.
 * \param pointer The address of the object.
 */
static inline void
luaA_object_unref_item(lua_State *L, int ud, const void *pointer)
{
    ud = luaA_absindex(L, ud);
    luaA_object_push(L, pointer);
    luaA_object_unref_item_from_stack(L, ud, -1);
    /* Remove object */
    lua_pop(L, 1);
}

void signal_object_emit(lua_State *, const signal_array_t *, const char *, int);

void luaA_object_connect_signal(lua_State *, int, const char *, lua_CFunction);
void luaA_object_disconnect_signal(lua_State *, int, const char *, lua_CFunction);
void luaA_object_connect_signal_from_stack(lua_State *, int, const char *, int);
void luaA_object_disconnect_signal_from_stack(lua_State *, int, const char *, int);
void luaA_object_emit_signal(lua_State *, int, const char *, int);

#define LUA_OBJECT_FUNCS(lua_class, type, prefix)                              \
    LUA_CLASS_FUNCS(prefix, lua_class)                                         \
    static inline type *                                                       \
    prefix##_new(lua_State *L)                                                 \
    {                                                                          \
        type *p = lua_newuserdata(L, sizeof(type));                            \
        p_clear(p, 1);                                                         \
        luaA_settype(L, (lua_class));                                          \
        lua_newtable(L);                                                       \
        lua_newtable(L);                                                       \
        lua_setmetatable(L, -2);                                               \
        lua_setfenv(L, -2);                                                    \
        lua_pushvalue(L, -1);                                                  \
        luaA_class_emit_signal(L, (lua_class), "new", 1);                      \
        return p;                                                              \
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
