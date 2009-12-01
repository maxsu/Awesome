/*
 * thread.c - Thread signals management
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

#include <lualib.h>
#include <pthread.h>

#include "thread.h"
#include "luaa.h"
#include "globalconf.h"
#include "common/luaobject.h"

typedef struct
{
    LUA_OBJECT_HEADER
    /** The Lua state representing the thread */
    lua_State *L;
    /** The code to execute in this thread */
    char *code;
    /** Is the thread running or not */
    bool running;
    /** The POSIX thread */
    pthread_t thread;
} thread_t;

static lua_class_t thread_class;
LUA_OBJECT_FUNCS(&thread_class, thread_t, thread)

static void
thread_wipe(thread_t *thread)
{
    lua_close(thread->L);
    p_delete(&thread->code);
}

static void *
thread_run(void *data)
{
    thread_t *thread = (thread_t *) data;
    warn("running thread %p", thread);
    int top = lua_gettop(thread->L);
    if(!luaL_dostring(thread->L, thread->code))
    {
        int newtop = lua_gettop(thread->L);
        warn("runned thread %p, newtop %d", thread, newtop);
        /*
        luaA_object_push(globalconf.L, thread);
        luaA_object_emit_signal(globalconf.L, -1, "finished", 0);
        lua_pop(globalconf.L, 1);
        */
    }
    /* XXX This need lock */
    // luaA_object_unref(globalconf.L, thread);
    //thread->running = false;
    return NULL;
}

static int
luaA_thread_new(lua_State *L)
{
    luaA_class_new(L, &thread_class);
    thread_t *thread = luaA_checkudata(L, -1, &thread_class);
    thread->L = luaL_newstate();
    luaL_openlibs(thread->L);
    return 1;
}

static int
luaA_thread_set_code(lua_State *L, thread_t *thread)
{
    size_t len;
    const char *code = luaL_checklstring(L, -1, &len);
    p_delete(&thread->code);
    thread->code = p_dup(code, len + 1);
    return 0;
}

static int
luaA_thread_get_code(lua_State *L, thread_t *thread)
{
    lua_pushstring(L, thread->code);
    return 1;
}

static int
luaA_thread_run(lua_State *L)
{
    thread_t *thread = luaA_checkudata(L, 1, &thread_class);
    if(thread->running)
        luaA_warn(L, "thread already running");
    else
    {
        /* start thread */
        luaA_object_ref(globalconf.L, 1);
        pthread_create(&thread->thread, NULL, thread_run, (void *) thread);
        thread->running = true;
    }
    return 0;
}

static LUA_OBJECT_EXPORT_PROPERTY(thread, thread_t, running, lua_pushboolean)

void
thread_class_setup(lua_State *L)
{
    static const struct luaL_reg thread_methods[] =
    {
        LUA_CLASS_METHODS(thread)
        { "run", luaA_thread_run },
        { NULL, NULL }
    };

    static const struct luaL_reg thread_module_meta[] =
    {
        { "__call", luaA_thread_new },
        { NULL, NULL }
    };

    luaA_class_setup(L, &thread_class, "thread", NULL,
                     sizeof(thread_t), NULL,
                     (lua_class_collector_t) thread_wipe, NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     thread_methods, thread_module_meta, NULL);
    luaA_class_add_property(&thread_class, "code",
                            (lua_class_propfunc_t) luaA_thread_set_code,
                            (lua_class_propfunc_t) luaA_thread_get_code,
                            (lua_class_propfunc_t) luaA_thread_set_code);
    luaA_class_add_property(&thread_class, "running",
                            NULL,
                            (lua_class_propfunc_t) luaA_thread_get_running,
                            NULL);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
