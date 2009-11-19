/*
 * luaa.c - Lua configuration management
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
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

#define _GNU_SOURCE

#include <ev.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <xcb/xtest.h>

#include <basedir_fs.h>

#include "awesome.h"
#include "config.h"
#include "objects/timer.h"
#include "awesome-version-internal.h"
#include "ewmh.h"
#include "luaa.h"
#include "spawn.h"
#include "objects/tag.h"
#include "objects/client.h"
#include "screen.h"
#include "event.h"
#include "selection.h"
#include "font.h"
#include "common/xcursor.h"
#include "common/xutil.h"
#include "common/buffer.h"
#include "common/backtrace.h"

#ifdef WITH_DBUS
extern const struct luaL_reg awesome_dbus_lib[];
#endif
extern const struct luaL_reg awesome_keygrabber_lib[];
extern const struct luaL_reg awesome_mousegrabber_lib[];
extern const struct luaL_reg awesome_mouse_methods[];
extern const struct luaL_reg awesome_mouse_meta[];

/** Path to config file */
static char *conffile;

/** Quit awesome.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_quit(lua_State *L __attribute__ ((unused)))
{
    ev_unloop(_G_loop, 1);
    return 0;
}

/** Execute another application, probably a window manager, to replace
 * awesome.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam The command line to execute.
 */
static int
luaA_exec(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);

    awesome_atexit();

    a_exec(cmd);
    return 0;
}

/** Restart awesome.
 */
static int
luaA_restart(lua_State *L __attribute__ ((unused)))
{
    awesome_restart();
    return 0;
}

/** Use XTest extension to fake input from mouse or keyboard.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_awesome_fake_input(lua_State *L)
{
    static int have_xtest = -11;

    if(unlikely(have_xtest == -1))
    {
        /* check for xtest extension */
        const xcb_query_extension_reply_t *xtest_query;
        xtest_query = xcb_get_extension_data(_G_connection, &xcb_test_id);
        have_xtest = xtest_query->present;
    }

    if(!have_xtest)
    {
        luaA_warn(L, "XTest extension is not available, cannot fake input.");
        return 0;
    }

    size_t tlen;
    const char *stype = luaL_checklstring(L, 1, &tlen);
    uint8_t type, detail;
    int x = 0, y = 0;

    switch(a_tokenize(stype, tlen))
    {
      case A_TK_KEY_PRESS:
        type = XCB_KEY_PRESS;
        detail = luaL_checknumber(L, 2); /* keycode */
        break;
      case A_TK_KEY_RELEASE:
        type = XCB_KEY_RELEASE;
        detail = luaL_checknumber(L, 2); /* keycode */
        break;
      case A_TK_BUTTON_PRESS:
        type = XCB_BUTTON_PRESS;
        detail = luaL_checknumber(L, 2); /* button number */
        break;
      case A_TK_BUTTON_RELEASE:
        type = XCB_BUTTON_RELEASE;
        detail = luaL_checknumber(L, 2); /* button number */
        break;
      case A_TK_MOTION_NOTIFY:
        type = XCB_MOTION_NOTIFY;
        detail = luaA_checkboolean(L, 2); /* relative to the current position or not */
        x = luaL_checknumber(L, 3);
        y = luaL_checknumber(L, 4);
        break;
      default:
        return 0;
    }

    xcb_test_fake_input(_G_connection,
                        type,
                        detail,
                        XCB_CURRENT_TIME,
                        XCB_NONE,
                        x, y,
                        0);
    return 0;
}

/** UTF-8 aware string length computing.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_mbstrlen(lua_State *L)
{
    const char *cmd = luaL_checkstring(L, 1);
    lua_pushnumber(L, (ssize_t) mbstowcs(NULL, NONULL(cmd), 0));
    return 1;
}

/** Enhanced type() function which recognize awesome objects.
 * \param L The Lua VM state.
 * \return The number of arguments pushed on the stack.
 */
static int
luaA_classof(lua_State *L)
{
    luaL_checkany(L, 1);
    lua_pushstring(L, luaA_classname(L, 1));
    return 1;
}

/** Check that an object is an instance of a class.
 * Push true or false.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack
 */
static int
luaA_instanceof(lua_State *L)
{
    luaL_checkany(L, 1);
    const char *name = luaL_checkstring(L, 2);

    lua_class_t *lua_class = luaA_class_get(L, 1);
    for(; lua_class; lua_class = lua_class->parent)
        if(!a_strcmp(lua_class->name, name))
        {
            lua_pushboolean(L, true);
            return 1;
        }

    lua_pushboolean(L, false);
    return 1;
}

/** Replace various standards Lua functions with our own.
 * \param L The Lua VM state.
 */
static void
luaA_fixups(lua_State *L)
{
    /* export string.wlen */
    lua_getglobal(L, "string");
    lua_pushcfunction(L, luaA_mbstrlen);
    lua_setfield(L, -2, "wlen");
    lua_pop(L, 1);
    /* set type */
    lua_pushcfunction(L, luaA_classof);
    lua_setfield(L, LUA_GLOBALSINDEX, "type");
    /* set classof */
    lua_pushcfunction(L, luaA_classof);
    lua_setfield(L, LUA_GLOBALSINDEX, "classof");
    /* set classof */
    lua_pushcfunction(L, luaA_instanceof);
    lua_setfield(L, LUA_GLOBALSINDEX, "instanceof");
    /* set selection */
    lua_pushliteral(L, "selection");
    lua_pushcfunction(L, luaA_selection_get);
    lua_settable(L, LUA_GLOBALSINDEX);
}

/** Try to use the metatable of an object.
 * \param L The Lua VM state.
 * \param idxobj The index of the object.
 * \param idxfield The index of the field (attribute) to get.
 * \return The number of element pushed on stack.
 */
static int
luaA_usemetatable(lua_State *L, int idxobj, int idxfield)
{
    lua_class_t *class = luaA_class_get(L, idxobj);

    for(; class; class = class->parent)
    {
        /* Push the class */
        lua_pushlightuserdata(L, class);
        /* Get its metatable from registry */
        lua_rawget(L, LUA_REGISTRYINDEX);
        /* Push the field */
        lua_pushvalue(L, idxfield);
        /* Get the field in the metatable */
        lua_rawget(L, -2);
        /* Do we have a field like that? */
        if(!lua_isnil(L, -1))
        {
            /* Yes, so remove the metatable and return it! */
            lua_remove(L, -2);
            return 1;
        }
        /* No, so remove the metatable and its value */
        lua_pop(L, 2);
    }

    return 0;
}

/** awesome global table.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lfield font The default font.
 * \lfield font_height The default font height.
 * \lfield conffile The configuration file which has been loaded.
 */
static int
luaA_awesome_index(lua_State *L)
{
    if(luaA_usemetatable(L, 1, 2))
        return 1;

    size_t len;
    const char *buf = luaL_checklstring(L, 2, &len);

    switch(a_tokenize(buf, len))
    {
      case A_TK_FONT:
        {
            char *font = pango_font_description_to_string(_G_font.desc);
            lua_pushstring(L, font);
            g_free(font);
        }
        break;
      case A_TK_FONT_HEIGHT:
        lua_pushnumber(L, _G_font.height);
        break;
      case A_TK_CONFFILE:
        lua_pushstring(L, conffile);
        break;
      case A_TK_FG:
        luaA_pushxcolor(L, globalconf.colors.fg);
        break;
      case A_TK_BG:
        luaA_pushxcolor(L, globalconf.colors.bg);
        break;
      case A_TK_VERSION:
        lua_pushliteral(L, AWESOME_VERSION);
        break;
      case A_TK_RELEASE:
        lua_pushliteral(L, AWESOME_RELEASE);
        break;
      default:
        return 0;
    }

    return 1;
}

/** Newindex function for the awesome global table.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_awesome_newindex(lua_State *L)
{
    if(luaA_usemetatable(L, 1, 2))
        return 1;

    size_t len;
    const char *buf = luaL_checklstring(L, 2, &len);

    switch(a_tokenize(buf, len))
    {
      case A_TK_FONT:
        {
            const char *newfont = luaL_checkstring(L, 3);
            font_wipe(&_G_font);
            font_init(&_G_font, newfont);
            /* refresh all wiboxes */
            foreach(wibox, globalconf.wiboxes)
                (*wibox)->need_update = true;
        }
        break;
      case A_TK_FG:
        if((buf = luaL_checklstring(L, 3, &len)))
           xcolor_init_reply(xcolor_init_unchecked(&globalconf.colors.fg, buf, len));
        break;
      case A_TK_BG:
        if((buf = luaL_checklstring(L, 3, &len)))
           xcolor_init_reply(xcolor_init_unchecked(&globalconf.colors.bg, buf, len));
        break;
      default:
        return 0;
    }

    return 0;
}

/** Add a global signal.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam A string with the event name.
 * \lparam The function to call.
 */
static int
luaA_awesome_connect_signal(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    luaA_checkfunction(L, 2);
    signal_add(&global_signals, name, luaA_object_ref(L, 2));
    return 0;
}

/** Remove a global signal.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam A string with the event name.
 * \lparam The function to call.
 */
static int
luaA_awesome_disconnect_signal(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    luaA_checkfunction(L, 2);
    const void *func = lua_topointer(L, 2);
    signal_remove(&global_signals, name, func);
    luaA_object_unref(L, (void *) func);
    return 0;
}

/** Emit a global signal.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam A string with the event name.
 * \lparam The function to call.
 */
static int
luaA_awesome_emit_signal(lua_State *L)
{
    signal_object_emit(L, &global_signals, luaL_checkstring(L, 1), lua_gettop(L) - 1);
    return 0;
}

static int
luaA_panic(lua_State *L)
{
    warn("unprotected error in call to Lua API (%s)",
         lua_tostring(L, -1));
    buffer_t buf;
    backtrace_get(&buf);
    warn("dumping backtrace\n%s", buf.s);
    warn("restarting awesome");
    awesome_restart();
    return 0;
}

static int
luaA_dofunction_on_error(lua_State *L)
{
    /* duplicate string error */
    lua_pushvalue(L, -1);
    /* emit error signal */
    signal_object_emit(L, &global_signals, "debug::error", 1);

    if(!luaL_dostring(L, "return debug.traceback(\"error while running function\", 3)"))
    {
        /* Move traceback before error */
        lua_insert(L, -2);
        /* Insert sentence */
        lua_pushliteral(L, "\nerror: ");
        /* Move it before error */
        lua_insert(L, -2);
        lua_concat(L, 3);
    }
    return 1;
}

/** Initialize the Lua VM
 * \param xdg An xdg handle to use to get XDG basedir.
 */
void
luaA_init(xdgHandle* xdg)
{
    lua_State *L;
    static const struct luaL_reg awesome_lib[] =
    {
        { "quit", luaA_quit },
        { "exec", luaA_exec },
        { "spawn", luaA_spawn },
        { "restart", luaA_restart },
        { "fake_input", luaA_awesome_fake_input },
        { "connect_signal", luaA_awesome_connect_signal },
        { "disconnect_signal", luaA_awesome_disconnect_signal },
        { "emit_signal", luaA_awesome_emit_signal },
        { "__index", luaA_awesome_index },
        { "__newindex", luaA_awesome_newindex },
        { NULL, NULL }
    };

    L = globalconf.L = luaL_newstate();

    /* Set panic function */
    lua_atpanic(L, luaA_panic);

    /* Set error handling function */
    lualib_dofunction_on_error = luaA_dofunction_on_error;

    luaL_openlibs(L);

    luaA_fixups(L);

    luaA_object_setup(L);

    /* Export awesome lib */
    luaA_openlib(L, "awesome", awesome_lib, awesome_lib);

#ifdef WITH_DBUS
    /* Export D-Bus lib */
    luaL_register(L, "dbus", awesome_dbus_lib);
    lua_pop(L, 1); /* luaL_register() leaves the table on stack */
#endif

    /* Export keygrabber lib */
    luaL_register(L, "keygrabber", awesome_keygrabber_lib);
    lua_pop(L, 1); /* luaL_register() leaves the table on stack */

    /* Export mousegrabber lib */
    luaL_register(L, "mousegrabber", awesome_mousegrabber_lib);
    lua_pop(L, 1); /* luaL_register() leaves the table on stack */

    /* Export mouse */
    luaA_openlib(L, "mouse", awesome_mouse_methods, awesome_mouse_meta);

    /* Export button */
    button_class_setup(L);

    /* Export image */
    image_class_setup(L);

    /* Export tag */
    tag_class_setup(L);

    /* Export window */
    window_class_setup(L);

    /* Export window */
    ewindow_class_setup(L);

    /* Export wibox */
    wibox_class_setup(L);

    /* Export client */
    client_class_setup(L);

    /* Export keys */
    key_class_setup(L);

    /* Export timer */
    timer_class_setup(L);

    /* Export screen */
    screen_class_setup(L);

    /* add Lua search paths */
    lua_getglobal(L, "package");
    if (LUA_TTABLE != lua_type(L, 1))
    {
        warn("package is not a table");
        return;
    }
    lua_getfield(L, 1, "path");
    if (LUA_TSTRING != lua_type(L, 2))
    {
        warn("package.path is not a string");
        lua_pop(L, 1);
        return;
    }

    /* add XDG_CONFIG_DIR as include path */
    const char * const *xdgconfigdirs = xdgSearchableConfigDirectories(xdg);
    for(; *xdgconfigdirs; xdgconfigdirs++)
    {
        size_t len = a_strlen(*xdgconfigdirs);
        lua_pushliteral(L, ";");
        lua_pushlstring(L, *xdgconfigdirs, len);
        lua_pushliteral(L, "/awesome/?.lua");
        lua_concat(L, 3);

        lua_pushliteral(L, ";");
        lua_pushlstring(L, *xdgconfigdirs, len);
        lua_pushliteral(L, "/awesome/?/init.lua");
        lua_concat(L, 3);

        lua_concat(L, 3); /* concatenate with package.path */
    }

    /* add Lua lib path (/usr/share/awesome/lib by default) */
    lua_pushliteral(L, ";" AWESOME_LUA_LIB_PATH "/?.lua");
    lua_pushliteral(L, ";" AWESOME_LUA_LIB_PATH "/?/init.lua");
    lua_concat(L, 3); /* concatenate with package.path */
    lua_setfield(L, 1, "path"); /* package.path = "concatenated string" */
    /* remove package table */
    lua_pop(L, 1);
}

static bool
luaA_loadrc(const char *confpath, bool run)
{
    if(!luaL_loadfile(globalconf.L, confpath))
    {
        if(run)
        {
            if(lua_pcall(globalconf.L, 0, LUA_MULTRET, 0))
                fprintf(stderr, "%s\n", lua_tostring(globalconf.L, -1));
            else
            {
                conffile = a_strdup(confpath);
                return true;
            }
        }
        else
            lua_pop(globalconf.L, 1);
        return true;
    }
    else
        fprintf(stderr, "%s\n", lua_tostring(globalconf.L, -1));

    return false;
}

/** Load a configuration file.
 * \param xdg An xdg handle to use to get XDG basedir.
 * \param confpatharg The configuration file to load.
 * \param run Run the configuration file.
 */
bool
luaA_parserc(xdgHandle* xdg, const char *confpatharg, bool run)
{
    char *confpath = NULL;
    bool ret = false;

    /* try to load, return if it's ok */
    if(confpatharg)
    {
        if(luaA_loadrc(confpatharg, run))
        {
            ret = true;
            goto bailout;
        }
        else if(!run)
            goto bailout;
    }

    confpath = xdgConfigFind("awesome/rc.lua", xdg);

    char *tmp = confpath;

    /* confpath is "string1\0string2\0string3\0\0" */
    while(*tmp)
    {
        if(luaA_loadrc(tmp, run))
        {
            ret = true;
            goto bailout;
        }
        else if(!run)
            goto bailout;
        tmp += a_strlen(tmp) + 1;
    }

bailout:

    p_delete(&confpath);

    return ret;
}

int
luaA_class_index_miss_property(lua_State *L, lua_object_t *obj)
{
    signal_object_emit(L, &global_signals, "debug::index::miss", 2);
    return 0;
}

int
luaA_class_newindex_miss_property(lua_State *L, lua_object_t *obj)
{
    signal_object_emit(L, &global_signals, "debug::newindex::miss", 3);
    return 0;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
