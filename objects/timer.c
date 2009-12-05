/*
 * timer.c - Timer signals management
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

#include <ev.h>

#include "awesome.h"
#include "luaa.h"
#include "timer.h"
#include "common/luaobject.h"
#include "common/luaclass_property.h"

typedef struct
{
    LUA_OBJECT_HEADER
    bool started;
    struct ev_timer timer;
} atimer_t;

typedef struct
{
    lua_State *L;
    atimer_t *timer;
} timer_callback_data_t;

static lua_class_t timer_class;
LUA_OBJECT_SIGNAL_FUNCS(timer, atimer_t)

static void
timer_wipe(atimer_t *timer)
{
    p_delete(&timer->timer.data);
}

static void
ev_timer_emit_signal(struct ev_loop *loop, struct ev_timer *w, int revents)
{
    timer_callback_data_t *data = w->data;
    timer_emit_signal_noret(data->L, data->timer, "timeout", 0);
}

static int
luaA_timer_new(lua_State *L)
{
    luaA_class_new(L, &timer_class);
    atimer_t *timer = luaA_checkudata(L, -1, &timer_class);
    timer_callback_data_t *data = p_new(timer_callback_data_t, 1);
    data->L = L;
    data->timer = timer;
    timer->timer.data = data;
    ev_set_cb(&timer->timer, ev_timer_emit_signal);
    return 1;
}

static int
luaA_timer_set_timeout(lua_State *L, atimer_t *timer)
{
    double timeout = luaL_checknumber(L, 3);
    ev_timer_set(&timer->timer, timeout, timeout);
    luaA_object_emit_signal_noret(L, 1, "property::timeout", 0);
    return 0;
}

static int
luaA_timer_get_timeout(lua_State *L, atimer_t *timer)
{
    lua_pushnumber(L, timer->timer.repeat);
    return 1;
}

static int
luaA_timer_start(lua_State *L)
{
    atimer_t *timer = luaA_checkudata(L, 1, &timer_class);
    if(timer->started)
        luaA_warn(L, "timer already started");
    else
    {
        luaA_object_ref(L, 1);
        ev_timer_start(_G_loop, &timer->timer);
        timer->started = true;
    }
    return 0;
}

static int
luaA_timer_stop(lua_State *L)
{
    atimer_t *timer = luaA_checkudata(L, 1, &timer_class);
    if(timer->started)
    {
        ev_timer_stop(_G_loop, &timer->timer);
        luaA_object_unref(L, timer);
        timer->started = false;
    }
    else
        luaA_warn(L, "timer not started");
    return 0;
}

static LUA_OBJECT_EXPORT_PROPERTY(timer, atimer_t, started, lua_pushboolean)

LUA_CLASS_FUNCS(timer, &timer_class)

void
timer_class_setup(lua_State *L)
{
    static const struct luaL_reg timer_methods[] =
    {
        LUA_CLASS_METHODS(timer)
        { "start", luaA_timer_start },
        { "stop", luaA_timer_stop },
        { NULL, NULL }
    };

    static const struct luaL_reg timer_module_meta[] =
    {
        { "__call", luaA_timer_new },
        { NULL, NULL },
    };

    luaA_class_setup(L, &timer_class, "timer", NULL, sizeof(atimer_t),
                     NULL, (lua_class_collector_t) timer_wipe, NULL,
                     timer_methods, timer_module_meta, NULL);

    static const lua_class_property_entry_t timer_property_get[] =
    {
        { "timeout", (lua_class_propfunc_t) luaA_timer_get_timeout },
        { "started", (lua_class_propfunc_t) luaA_timer_get_started },
        { NULL, NULL },
    };

    static const lua_class_property_entry_t timer_property_set[] =
    {
        { "timeout", (lua_class_propfunc_t) luaA_timer_set_timeout },
        { NULL, NULL },
    };

    luaA_class_property_setup(L, &timer_class, timer_property_get, timer_property_set);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
