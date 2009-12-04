/*
 * luaclass.c - useful functions for handling Lua classes
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
#include "common/luaclass.h"
#include "common/luaobject.h"

/** The default class for all object */
lua_class_t luaobject_class = { .name = "object" };

struct lua_class_property
{
    /** ID matching the property */
    unsigned long id;
    /** Callback function called when the property is found in object creation. */
    lua_class_propfunc_t new;
    /** Callback function called when the property is found in object __index. */
    lua_class_propfunc_t index;
    /** Callback function called when the property is found in object __newindex. */
    lua_class_propfunc_t newindex;
};

int
luaA_settype(lua_State *L, lua_class_t *lua_class)
{
    lua_pushlightuserdata(L, lua_class);
    lua_rawget(L, LUA_REGISTRYINDEX);
    lua_setmetatable(L, -2);
    return 1;
}

/** Convert a object to a udata if possible.
 * \param L The Lua VM state.
 * \param ud The index.
 * \param class The wanted class.
 * \return A pointer to the object, NULL otherwise.
 */
void *
luaA_toudata(lua_State *L, int ud, lua_class_t *class)
{
    void *p = lua_touserdata(L, ud);
    if(p && lua_getmetatable(L, ud)) /* does it have a metatable? */
    {
        /* Get the lua_class_t that matches this metatable */
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_class_t *metatable_class = lua_touserdata(L, -1);

        /* remove lightuserdata (lua_class pointer) */
        lua_pop(L, 1);

        /* Now, check that the class given in argument is the same as the
         * metatable's object, or one of its parent (inheritance) */
        for(; metatable_class; metatable_class = metatable_class->parent)
            if(metatable_class == class)
                return p;
    }
    return NULL;
}

/** Check for a udata class.
 * \param L The Lua VM state.
 * \param ud The object index on the stack.
 * \param class The wanted class.
 */
void *
luaA_checkudata(lua_State *L, int ud, lua_class_t *class)
{
    void *p = luaA_toudata(L, ud, class);
    if(!p)
        luaL_typerror(L, ud, class->name);
    else if(class->checker && !class->checker(p))
        luaL_error(L, "invalid object");
    return p;
}

/** Get an object lua_class.
 * \param L The Lua VM state.
 * \param idx The index of the object on the stack.
 */
lua_class_t *
luaA_class_get_from_stack(lua_State *L, int idx)
{
    int type = lua_type(L, idx);

    if((type == LUA_TUSERDATA || type == LUA_TLIGHTUSERDATA)
       && lua_getmetatable(L, idx))
    {
        /* Use the metatable has key to get the class from registry */
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_class_t *class = lua_touserdata(L, -1);
        lua_pop(L, 1);
        return class;
    }

    return NULL;
}

/** Get an object Lua class.
 * \param L The Lua VM state.
 * \param idx The index of the object on the stack.
 */
lua_class_t *
luaA_class_get(lua_State *L, lua_object_t *object)
{
    luaA_object_push(L, object);
    lua_class_t *lua_class = luaA_class_get_from_stack(L, -1);
    lua_pop(L, 1);
    return lua_class;
}

/** Enhanced version of lua_typename that recognizes setup Lua classes.
 * \param L The Lua VM state.
 * \param idx The index of the object on the stack.
 */
const char *
luaA_classname(lua_State *L, int idx)
{
    int type = lua_type(L, idx);

    if(type == LUA_TUSERDATA || type == LUA_TLIGHTUSERDATA)
    {
        lua_class_t *lua_class = luaA_class_get_from_stack(L, idx);
        if(lua_class)
            return lua_class->name;
    }

    return lua_typename(L, type);
}

static int
lua_class_property_cmp(const void *a, const void *b)
{
    const lua_class_property_t *x = a, *y = b;
    return x->id > y->id ? 1 : (x->id < y->id ? -1 : 0);
}

BARRAY_FUNCS(lua_class_property_t, lua_class_property, DO_NOTHING, lua_class_property_cmp)

void
luaA_class_add_property(lua_class_t *lua_class,
                        const char *name,
                        lua_class_propfunc_t cb_new,
                        lua_class_propfunc_t cb_index,
                        lua_class_propfunc_t cb_newindex)
{
    lua_class_property_array_insert(&lua_class->properties, (lua_class_property_t)
                                    {
                                        .id = a_strhash((const unsigned char *) name),
                                        .new = cb_new,
                                        .index = cb_index,
                                        .newindex = cb_newindex
                                    });
}

/** Garbage collect a Lua object.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_class_gc(lua_State *L)
{
    lua_object_t *item = luaA_checkudata(L, 1, &luaobject_class);
    signal_array_wipe(&item->signals);
    /* Get the object class */
    lua_class_t *class = luaA_class_get_from_stack(L, 1);
    /* Call the collector function of the class, and all its parent classes */
    for(; class; class = class->parent)
        if(class->collector)
            class->collector(item);
    return 0;
}

static int
luaA_class_tostring(lua_State *L)
{
    lua_class_t *lua_class = luaA_class_get_from_stack(L, 1);
    lua_object_t *object = luaA_checkudata(L, 1, lua_class);

    int i = 0;
    for(; lua_class; lua_class = lua_class->parent, i++)
    {
        if(i)
        {
            lua_pushliteral(L, "/");
            lua_insert(L, - (i * 2));
        }
        lua_pushstring(L, NONULL(lua_class->name));
        lua_insert(L, - (i * 2) - 1);
    }

    lua_pushfstring(L, ": %p", object);

    lua_concat(L, i * 2);

    return 1;
}

/** Try to use the methods of a module.
 * \param L The Lua VM state.
 * \param idxobj The index of the object.
 * \param idxfield The index of the field (attribute) to get.
 * \return The number of element pushed on stack.
 */
static int
luaA_use_class_fields(lua_State *L, int idxobj, int idxfield)
{
    lua_class_t *class = luaA_class_get_from_stack(L, idxobj);

    for(; class; class = class->parent)
    {
        /* Push the class */
        lua_pushlightuserdata(L, class);
        /* Get its metatable from registry */
        lua_rawget(L, LUA_REGISTRYINDEX);
        /* Does it have a metatable? */
        if(lua_isnil(L, -1))
        {
            /* No, remove nil, continue */
            lua_pop(L, 1);
            continue;
        }
        /* Get the __methods table */
        lua_getfield(L, -1, "__methods");
        /* Remove the metatable */
        lua_remove(L, -2);
        /* Check we have a __methods table */
        if(lua_isnil(L, -1))
            /* No, then remove nil */
            lua_pop(L, 1);
        else
        {
            /* Push the field */
            lua_pushvalue(L, idxfield);
            /* Get the field in the methods table */
            lua_rawget(L, -2);
            /* Do we have a field like that? */
            if(!lua_isnil(L, -1))
                /* Yes, so return it! */
                return 1;
            /* No, so remove the field value (nil) */
            lua_pop(L, 1);
        }
    }

    return 0;
}

static lua_class_property_t *
lua_class_property_array_getbyid(lua_class_property_array_t *arr,
                                 unsigned long id)
{
    return lua_class_property_array_lookup(arr, (lua_class_property_t) { .id = id });
}

/** Get a property of a object.
 * \param L The Lua VM state.
 * \param lua_class The Lua class.
 * \param fieldidx The index of the field name.
 * \return The object property if found, NULL otherwise.
 */
static lua_class_property_t *
luaA_class_property_get(lua_State *L, lua_class_t *lua_class, int fieldidx)
{
    /* Lookup the property using id */
    unsigned long id = a_strhash((const unsigned char*) luaL_checkstring(L, fieldidx));

    /* Look for the property in the class; if not found, go in the parent class. */
    for(; lua_class; lua_class = lua_class->parent)
    {
        lua_class_property_t *prop =
            lua_class_property_array_getbyid(&lua_class->properties, id);

        if(prop)
            return prop;
    }

    return NULL;
}

/** This is a wrapper for calling property function in a clean env.
 * On the stack it gets:
 * 1. function to call
 * 2. object
 * (3. key
 *  4. value,
 *  etc, we don't care)
 *  It then pop 1. function to call, and calls it with L and 2. object has
 *  argument.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_class_property_call_function_wrapper(lua_State *L)
{
    lua_class_propfunc_t func = lua_topointer(L, 1);
    lua_remove(L, 1);
    lua_object_t *object = lua_touserdata(L, 1);
    return func(L, object);
}

/** Generic index meta function for objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_class_index(lua_State *L)
{
    lua_class_t *class = luaA_class_get_from_stack(L, 1);

    lua_class_property_t *prop = luaA_class_property_get(L, class, 2);

    /* Property does exist and has an index callback */
    if(prop && prop->index)
    {
        /* Push wrapper function */
        lua_pushcfunction(L, luaA_class_property_call_function_wrapper);
        /* Push property function */
        lua_pushlightuserdata(L, prop->index);
        /* Push object */
        lua_pushvalue(L, 1);
        /* Duplicate key */
        lua_pushvalue(L, 2);
        if(luaA_dofunction(L, 3, 1) > 0)
            return 1;
    }

    /* Try to use class fields then */
    if(luaA_use_class_fields(L, 1, 2))
        return 1;

    return 0;
}

/** Generic newindex meta function for objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_class_newindex(lua_State *L)
{
    lua_class_t *class = luaA_class_get_from_stack(L, 1);

    lua_class_property_t *prop = luaA_class_property_get(L, class, 2);

    /* Property does exist and has a newindex callback */
    if(prop && prop->newindex)
    {
        /* Push wrapper function */
        lua_pushcfunction(L, luaA_class_property_call_function_wrapper);
        /* Push property function */
        lua_pushlightuserdata(L, prop->newindex);
        /* Push object */
        lua_pushvalue(L, 1);
        /* Duplicate key */
        lua_pushvalue(L, 2);
        /* Duplicate value */
        lua_pushvalue(L, 3);
        if(luaA_dofunction(L, 4, 0) != 0)
            return 0;
    }

    return 0;
}

/** Setup a new Lua class.
 * \param L The Lua VM state.
 * \param name The class name.
 * \param parent The parent class (inheritance).
 * \param object_size The object size.
 * \param initializer The initializer function used to init new objects.
 * \param collector The collector function used when garbage collecting an
 * object.
 * \param checker The check function to call when using luaA_checkudata().
 * \param methods The methods to set on the class table.
 * \param metatable_module The metatable to set on the module/class table.
 * \param metatable_object The metatable of the objects. Some field like __gc,
 * __index, __newindex and tostring are automagically set.
 */
void
luaA_class_setup(lua_State *L, lua_class_t *class,
                 const char *name,
                 lua_class_t *parent,
                 size_t object_size,
                 lua_class_initializer_t initializer,
                 lua_class_collector_t collector,
                 lua_class_checker_t checker,
                 const struct luaL_reg methods[],
                 const struct luaL_reg metatable_module[],
                 const struct luaL_reg metatable_object[])
{
    /* Create the object metatable */
    lua_newtable(L);
    /* Register it with class pointer as key in the registry
     * class-pointer -> metatable */
    lua_pushlightuserdata(L, class);
    /* Duplicate the object metatable */
    lua_pushvalue(L, -2);
    lua_rawset(L, LUA_REGISTRYINDEX);
    /* Now register class pointer with metatable as key in the registry
     * metatable -> class-pointer */
    lua_pushvalue(L, -1);
    lua_pushlightuserdata(L, class);
    lua_rawset(L, LUA_REGISTRYINDEX);

    /* Set garbage collector in the metatable */
    lua_pushcfunction(L, luaA_class_gc);
    lua_setfield(L, -2, "__gc");

    /* Protect metatable from userland */
    lua_pushstring(L, name);
    lua_setfield(L, -2, "__metatable");

    /* Set __index and __newindex metamethod in the metatable */
    lua_pushcfunction(L, luaA_class_index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, luaA_class_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_pushcfunction(L, luaA_class_tostring);
    lua_setfield(L, -2, "__tostring");

    /* Register all functions provided in metatable.
     * This is done now so the developer can replace __gc, __index, etc. */
    if(metatable_object)
        luaL_register(L, NULL, metatable_object);

    if(methods)
    {
        /* Register all methods in the module table */
        luaL_register(L, name, methods);

        if(metatable_module)
        {
            /* Create a new table for the metatable of the module */
            lua_newtable(L);
            /* Register metatable functions of the module */
            luaL_register(L, NULL, metatable_module);
            /* Set module metatable */
            lua_setmetatable(L, -2);
        }

        /* Also, store the 'module' table directly in the __methods field so
         * object can access module easily from their __index */
        lua_setfield(L, -2, "__methods");
    }

    /* Remove object metatable */
    lua_pop(L, 1);

    class->collector = collector;
    class->initializer = initializer;
    class->object_size = object_size;
    class->name = name;
    class->checker = checker;
    if(!parent) parent = &luaobject_class;
    class->parent = parent;
}

void
luaA_class_connect_signal(lua_State *L, lua_class_t *lua_class, const char *name, lua_CFunction fn)
{
    lua_pushcfunction(L, fn);
    luaA_class_connect_signal_from_stack(L, lua_class, name, -1);
}

void
luaA_class_connect_signal_from_stack(lua_State *L, lua_class_t *lua_class,
                                     const char *name, int ud)
{
    luaA_checkfunction(L, ud);
    signal_add(&lua_class->signals, name, luaA_object_ref(L, ud));
}

void
luaA_class_disconnect_signal_from_stack(lua_State *L, lua_class_t *lua_class,
                                        const char *name, int ud)
{
    luaA_checkfunction(L, ud);
    void *ref = (void *) lua_topointer(L, ud);
    signal_remove(&lua_class->signals, name, ref);
    luaA_object_unref(L, (void *) ref);
    lua_remove(L, ud);
}

int
luaA_class_emit_signal(lua_State *L, lua_class_t *lua_class,
                       const char *name, int nargs)
{
    int nret = 0;

    /* emit signal on parent classes */
    for(; lua_class; lua_class = lua_class->parent)
    {
        /* duplicate arguments */
        for(int i = 0; i < nargs; i++)
            lua_pushvalue(L, - nargs - nret);
        /* emit signal on class */
       nret += signal_object_emit(L, &lua_class->signals, name, nargs);
    }

    lua_pop(L, nargs);

    return nret;
}

static lua_class_t *
lua_class_get_child(lua_class_t *base, lua_class_t *parent)
{
    for(; base && base->parent != parent; base = base->parent);
    return base;
}

lua_object_t *
luaA_object_new(lua_State *L, lua_class_t *lua_class)
{
    lua_object_t *object = lua_newuserdata(L, lua_class->object_size);
    /* don't use p_clear, the size it's different of sizeof(lua_object_t) */
    memset(object, 0, lua_class->object_size);

    /* Set object type */
    luaA_settype(L, lua_class);
    /* Create a env table */
    lua_newtable(L);
    /* Create a metatable for the env table */
    lua_newtable(L);
    /* Set metatable on en table */
    lua_setmetatable(L, -2);
    /* Set env table on object */
    lua_setfenv(L, -2);

    /* Store the object in the registry so it can be pushed whenever we want,
     * unless there's no ref of it at all (neither in Lua), but in this case, we
     * won't push it :) */
    luaA_object_store_registry(L, -1);

    /* Get the top level class in the hierarchy */
    lua_class_t *top_class;
    for(top_class = lua_class; top_class->parent; top_class = top_class->parent);

    /* Now, go backward from top_class (top level class) to our lua_class */
    for(; top_class != lua_class; top_class = lua_class_get_child(lua_class, top_class))
        if(top_class->initializer)
            top_class->initializer(L, object);

    if(lua_class->initializer)
        lua_class->initializer(L, object);

    /* Emit class signal */
    lua_pushvalue(L, -1);
    lua_pop(L, luaA_class_emit_signal(L, lua_class, "new", 1));
    return object;
}

/** Generic constructor function for objects.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
int
luaA_class_new(lua_State *L, lua_class_t *lua_class)
{
    /* Check we have a table that should contains some properties */
    luaA_checktable(L, 2);

    luaA_object_new(L, lua_class);

    /* Push the first key before iterating */
    lua_pushnil(L);
    /* Iterate over the property keys */
    while(lua_next(L, 2))
    {
        /* Check that the key is a string.
         * We cannot call tostring blindly or Lua will convert a key that is a
         * number TO A STRING, confusing lua_next() */
        if(lua_isstring(L, -2))
        {
            lua_class_property_t *prop = luaA_class_property_get(L, lua_class, -2);

            if(prop && prop->new)
            {
                /* Push wrapper function */
                lua_pushcfunction(L, luaA_class_property_call_function_wrapper);
                /* Push property function */
                lua_pushlightuserdata(L, prop->new);
                /* Push object */
                lua_pushvalue(L, 3);
                /* Duplicate key */
                lua_pushvalue(L, -5);
                /* Duplicate value */
                lua_pushvalue(L, -5);
                if(luaA_dofunction(L, 4, 0) != 0)
                    continue;
            }
        }
        /* Remove value */
        lua_pop(L, 1);
    }

    return 1;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
