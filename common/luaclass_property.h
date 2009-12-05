/*
 * luaclass_property.h - useful functions for handling Lua objects properties
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

#ifndef AWESOME_COMMON_LUACLASS_PROPERTY
#define AWESOME_COMMON_LUACLASS_PROPERTY

#include "common/luaclass.h"

typedef int (*lua_class_propfunc_t)(lua_State *, lua_object_t *);

typedef struct
{
    const char *name;
    lua_class_propfunc_t func;
} lua_class_property_entry_t;

void luaA_class_property_setup(lua_State *, lua_class_t *,
                               const lua_class_property_entry_t[], const lua_class_property_entry_t[]);

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

#define LUA_OBJECT_DO_SET_PROPERTY_FUNC(pfx, type, prop) \
    void \
    pfx##_set_##prop(lua_State *L, type *item, fieldtypeof(type, prop) value) \
    { \
        if(item->prop != value) \
        { \
            item->prop = value; \
            pfx##_emit_signal_noret(L, item, "property::" #prop, 0); \
        } \
    }

#define LUA_OBJECT_FAKE_CHECKER(L, idx) idx

#define LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(pfx, type, prop, checker) \
    int \
    luaA_##pfx##_set_##prop(lua_State *L, type *c) \
    { \
        pfx##_set_##prop(L, c, checker(L, 3)); \
        return 0; \
    }

#define LUA_OBJECT_DO_SET_PROPERTY_WITH_REF_FUNC(prefix, target_class, type, prop) \
    void \
    prefix##_set_##prop(lua_State *L, type *item, int vidx) \
    { \
        vidx = luaA_absindex(L, vidx); \
        luaA_checkudataornil(L, vidx, (target_class)); \
        item->prop = luaA_object_ref_item(L, (lua_object_t *) item, vidx); \
        prefix##_emit_signal_noret(L, item, "property::" #prop, 0); \
    }

#endif

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
