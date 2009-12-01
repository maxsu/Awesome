/*
 * client.c - client management
 *
 * Copyright © 2007-2009 Julien Danjou <julien@danjou.info>
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

#include <xcb/xcb_atom.h>

#include "awesome.h"
#include "ewmh.h"
#include "screen.h"
#include "wibox.h"
#include "systray.h"
#include "property.h"
#include "spawn.h"
#include "luaa.h"
#include "xwindow.h"
#include "objects/client.h"
#include "objects/tag.h"
#include "common/atoms.h"
#include "common/xutil.h"

/** Collect a client.
 * \param L The Lua VM state.
 * \return The number of element pushed on stack.
 */
static void
client_wipe(client_t *c)
{
    xcb_get_wm_protocols_reply_wipe(&c->protocols);
    p_delete(&c->machine);
    p_delete(&c->class);
    p_delete(&c->instance);
    p_delete(&c->icon_name);
    p_delete(&c->alt_icon_name);
    p_delete(&c->name);
    p_delete(&c->alt_name);
}

/** Change the clients urgency flag.
 * \param L The Lua VM state.
 * \param c The client.
 * \param urgent The new flag state.
 */
void
client_set_urgent(lua_State *L, client_t *c, bool urgent)
{
    if(c->urgent != urgent)
    {
        /* Need to grab server to avoid race condition */
        xcb_grab_server(_G_connection);

        xcb_get_property_cookie_t hints =
            xcb_get_wm_hints_unchecked(_G_connection, c->window);

        c->urgent = urgent;

        /* update ICCCM hints */
        xcb_wm_hints_t wmh;
        xcb_get_wm_hints_reply(_G_connection, hints, &wmh, NULL);

        if(urgent)
            wmh.flags |= XCB_WM_HINT_X_URGENCY;
        else
            wmh.flags &= ~XCB_WM_HINT_X_URGENCY;

        xcb_set_wm_hints(_G_connection, c->window, &wmh);

        xcb_ungrab_server(_G_connection);

        client_emit_signal(L, c, "property::urgent", 0);
    }
}

LUA_OBJECT_DO_SET_PROPERTY_FUNC(client, (lua_class_t *) &client_class, client_t, group_window)
LUA_OBJECT_DO_SET_PROPERTY_FUNC(client, (lua_class_t *) &client_class, client_t, pid)
LUA_OBJECT_DO_SET_PROPERTY_FUNC(client, (lua_class_t *) &client_class, client_t, skip_taskbar)

#define DO_CLIENT_SET_STRING_PROPERTY(prop) \
    void \
    client_set_##prop(lua_State *L, int cidx, char *value) \
    { \
        client_t *c = luaA_checkudata(L, cidx, (lua_class_t *) &client_class); \
        p_delete(&c->prop); \
        c->prop = value; \
        luaA_object_emit_signal(L, cidx, "property::" #prop, 0); \
    }
DO_CLIENT_SET_STRING_PROPERTY(name)
DO_CLIENT_SET_STRING_PROPERTY(alt_name)
DO_CLIENT_SET_STRING_PROPERTY(icon_name)
DO_CLIENT_SET_STRING_PROPERTY(alt_icon_name)
DO_CLIENT_SET_STRING_PROPERTY(role)
DO_CLIENT_SET_STRING_PROPERTY(machine)
#undef DO_CLIENT_SET_STRING_PROPERTY

void
client_set_class_instance(lua_State *L, int cidx, const char *class, const char *instance)
{
    client_t *c = luaA_checkudata(L, cidx, (lua_class_t *) &client_class);
    p_delete(&c->class);
    p_delete(&c->instance);
    c->class = a_strdup(class);
    luaA_object_emit_signal(L, cidx, "property::class", 0);
    c->instance = a_strdup(instance);
    luaA_object_emit_signal(L, cidx, "property::instance", 0);
}

/** Get a client by its window.
 * \param w The client window to find.
 * \return A client pointer if found, NULL otherwise.
 */
client_t *
client_getbywin(xcb_window_t w)
{
    client_t **c = client_array_lookup(&_G_clients, &(client_t) { .window = w });
    return c ? *c : NULL;
}

/** Check if client supports atom a protocol in WM_PROTOCOL.
 * \param c The client.
 * \param atom The protocol atom to check for.
 * \return True if client has the atom in protocol, false otherwise.
 */
bool
client_hasproto(client_t *c, xcb_atom_t atom)
{
    for(uint32_t i = 0; i < c->protocols.atoms_len; i++)
        if(c->protocols.atoms[i] == atom)
            return true;
    return false;
}

/** Manage a new client.
 * \param w The window.
 * \param wgeom Window geometry.
 * \param phys_screen Physical screen number.
 * \param startup True if we are managing at startup time.
 */
void
client_manage(xcb_window_t w, xcb_get_geometry_reply_t *wgeom, bool startup)
{
    /* If this is a new client that just has been launched, then request its
     * startup id. */
    xcb_get_property_cookie_t startup_id_q = { 0 };

    if(!startup)
        startup_id_q = xcb_get_any_property(_G_connection,
                                            false, w, _NET_STARTUP_ID, UINT_MAX);

    xcb_change_window_attributes(_G_connection, w, XCB_CW_EVENT_MASK,
                                 (uint32_t[]) { XCB_EVENT_MASK_STRUCTURE_NOTIFY
                                                | XCB_EVENT_MASK_PROPERTY_CHANGE
                                                | XCB_EVENT_MASK_ENTER_WINDOW
                                                | XCB_EVENT_MASK_LEAVE_WINDOW
                                                | XCB_EVENT_MASK_FOCUS_CHANGE });

    /* Add window to save set */
    xcb_change_save_set(_G_connection, XCB_SET_MODE_INSERT, w);

    client_t *c = (client_t *) luaA_object_new(globalconf.L, (lua_class_t *) &client_class);

    /* Store window */
    c->window = w;
    luaA_object_emit_signal(globalconf.L, -1, "property::window", 0);
    /* Store parent */
    c->parent = _G_root;
    luaA_object_emit_signal(globalconf.L, -1, "property::parent", 0);
    /* Consider window is focusable by default */
    c->focusable = true;
    /* Consider the window banned */
    c->banned = true;
    /* Consider window movable/resizable by default */
    c->movable = c->resizable = true;

    /* Duplicate client and push it in client list */
    lua_pushvalue(globalconf.L, -1);
    client_array_insert(&_G_clients, luaA_object_ref(globalconf.L, -1));
    ewindow_binary_array_insert(&_G_ewindows, (ewindow_t *) c);

    /* Store initial geometry and emits signals so we inform that geometry have
     * been set. */
#define HANDLE_GEOM(attr) \
    c->geometry.attr = wgeom->attr; \
    luaA_object_emit_signal(globalconf.L, -1, "property::" #attr, 0);
HANDLE_GEOM(x)
HANDLE_GEOM(y)
HANDLE_GEOM(width)
HANDLE_GEOM(height)
#undef HANDLE_GEOM

    luaA_object_emit_signal(globalconf.L, -1, "property::geometry", 0);

    /* Set border width */
    ewindow_set_border_width(globalconf.L, -1, wgeom->border_width);

    /* update hints */
    property_update_wm_normal_hints(c, NULL);
    property_update_wm_hints(c, NULL);
    property_update_wm_transient_for(c, NULL);
    property_update_wm_client_leader(c, NULL);
    property_update_wm_client_machine(c, NULL);
    property_update_wm_window_role(c, NULL);
    property_update_net_wm_pid(c, NULL);
    property_update_net_wm_icon(c, NULL);
    property_update_wm_name(c, NULL);
    property_update_net_wm_name(c, NULL);
    property_update_wm_icon_name(c, NULL);
    property_update_net_wm_icon_name(c, NULL);
    property_update_wm_class(c, NULL);
    property_update_wm_protocols(c, NULL);
    /* Then check clients hints */
    ewmh_client_check_hints(c);
    ewmh_process_client_strut(c, NULL);

    ewindow_set_opacity(globalconf.L, -1, xwindow_get_opacity(c->window));

    /* Push client in stack */
    stack_window_raise(globalconf.L, -1);

    xwindow_set_state(c->window, XCB_WM_STATE_NORMAL);

    if(!startup)
    {
        /* Request our response */
        xcb_get_property_reply_t *reply =
            xcb_get_property_reply(_G_connection, startup_id_q, NULL);
        /* Say spawn that a client has been started, with startup id as argument */
        char *startup_id = xutil_get_text_property_from_reply(reply);
        p_delete(&reply);
        spawn_start_notify(c, startup_id);
        p_delete(&startup_id);
    }

    /* client is still on top of the stack; push startup value,
     * and emit signals with one arg */
    lua_pushboolean(globalconf.L, startup);
    luaA_object_emit_signal(globalconf.L, -2, "manage", 1);
    /* pop client */
    lua_pop(globalconf.L, 1);
}

/** Unmanage a client.
 * \param c The client.
 */
void
client_unmanage(client_t *c)
{
    /* remove client from global list and everywhere else */
    client_array_lookup_and_remove(&_G_clients, &(client_t) { .window = c->window });
    ewindow_binary_array_lookup_and_remove(&_G_ewindows, &(ewindow_t) { .window = c->window });

    /* Tag and window reference each other so there are tight forever.
     * We don't want the tag the unmanaged client to be referenced forever in a
     * tag so we untag it. */
    luaA_object_push(globalconf.L, c);
    foreach(tag, c->tags)
    {
        luaA_object_push(globalconf.L, *tag);
        untag_ewindow(globalconf.L, -2, -1);
        lua_pop(globalconf.L, 1);
    }

    luaA_object_emit_signal(globalconf.L, -1, "unmanage", 0);
    lua_pop(globalconf.L, 1);

    if(strut_has_value(&c->strut))
    {
        lua_pushlightuserdata(globalconf.L, screen_getbycoord(c->geometry.x, c->geometry.y));
        luaA_object_emit_signal(globalconf.L, -1, "property::workarea", 0);
        lua_pop(globalconf.L, 1);
    }

    xwindow_set_state(c->window, XCB_WM_STATE_WITHDRAWN);

    /* set client as invalid */
    c->window = XCB_NONE;

    luaA_object_unref(globalconf.L, c);
}

/** Kill a client via a WM_DELETE_WINDOW request or KillClient if not
 * supported.
 * \param c The client to kill.
 */
void
client_kill(client_t *c)
{
    if(client_hasproto(c, WM_DELETE_WINDOW))
    {
        xcb_client_message_event_t ev;

        /* Initialize all of event's fields first */
        p_clear(&ev, 1);

        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.window = c->window;
        ev.format = 32;
        ev.data.data32[1] = XCB_CURRENT_TIME;
        ev.type = WM_PROTOCOLS;
        ev.data.data32[0] = WM_DELETE_WINDOW;

        xcb_send_event(_G_connection, false, c->window,
                       XCB_EVENT_MASK_NO_EVENT, (char *) &ev);
    }
    else
        xcb_kill_client(_G_connection, c->window);
}

/** Get all clients into a table.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_client_get(lua_State *L)
{
    int i = 1;

    lua_createtable(L, _G_clients.len, 0);

    foreach(c, _G_clients)
    {
        luaA_object_push(L, *c);
        lua_rawseti(L, -2, i++);
    }

    return 1;
}

LUA_OBJECT_DO_SET_PROPERTY_WITH_REF_FUNC(client, (lua_class_t *) &client_class, &image_class, client_t, icon)

/** Kill a client.
 * \param L The Lua VM state.
 *
 * \luastack
 * \lvalue A client.
 */
static int
luaA_client_kill(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);
    client_kill(c);
    return 0;
}

/** Stop managing a client.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lvalue A client.
 */
static int
luaA_client_unmanage(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);
    client_unmanage(c);
    return 0;
}

static int
luaA_client_set_icon(lua_State *L, client_t *c)
{
    client_set_icon(L, -3, -1);
    return 0;
}

static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(client, client_t, urgent, luaA_checkboolean)

static int
luaA_client_set_skip_taskbar(lua_State *L, client_t *c)
{
    client_set_skip_taskbar(L, -3, luaA_checkboolean(L, -1));
    return 0;
}

static int
luaA_client_get_name(lua_State *L, client_t *c)
{
    lua_pushstring(L, c->name ? c->name : c->alt_name);
    return 1;
}

static int
luaA_client_get_icon_name(lua_State *L, client_t *c)
{
    lua_pushstring(L, c->icon_name ? c->icon_name : c->alt_icon_name);
    return 1;
}

static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, class, lua_pushstring)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, instance, lua_pushstring)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, machine, lua_pushstring)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, role, lua_pushstring)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, skip_taskbar, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, leader_window, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, group_window, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, pid, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, urgent, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, icon, luaA_object_push)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, transient_for, luaA_object_push)
LUA_OBJECT_DO_SET_PROPERTY_WITH_REF_FUNC(client, (lua_class_t *) &client_class, (lua_class_t *) &client_class, client_t, transient_for)

static bool
client_checker(client_t *c)
{
    return c->window != XCB_NONE;
}

static int
client_take_focus(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);
    if(client_hasproto(c, WM_TAKE_FOCUS))
        xwindow_takefocus(c->window);
    return 0;
}

void
client_class_setup(lua_State *L)
{
    static const struct luaL_reg client_methods[] =
    {
        LUA_CLASS_METHODS(client)
        { "get", luaA_client_get },
        { "kill", luaA_client_kill },
        { "unmanage", luaA_client_unmanage },
        { NULL, NULL }
    };

    luaA_class_setup(L, (lua_class_t *) &client_class, "client", (lua_class_t *) &ewindow_class,
                     sizeof(client_t), NULL,
                     (lua_class_collector_t) client_wipe,
                     (lua_class_checker_t) client_checker,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     client_methods, NULL, NULL);

    luaA_class_add_property((lua_class_t *) &client_class, "name",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_name,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "skip_taskbar",
                            (lua_class_propfunc_t) luaA_client_set_skip_taskbar,
                            (lua_class_propfunc_t) luaA_client_get_skip_taskbar,
                            (lua_class_propfunc_t) luaA_client_set_skip_taskbar);
    luaA_class_add_property((lua_class_t *) &client_class, "class",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_class,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "instance",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_instance,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "role",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_role,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "pid",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_pid,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "leader_window",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_leader_window,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "machine",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_machine,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "icon_name",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_icon_name,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "group_window",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_group_window,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "icon",
                            (lua_class_propfunc_t) luaA_client_set_icon,
                            (lua_class_propfunc_t) luaA_client_get_icon,
                            (lua_class_propfunc_t) luaA_client_set_icon);
    luaA_class_add_property((lua_class_t *) &client_class, "urgent",
                            (lua_class_propfunc_t) luaA_client_set_urgent,
                            (lua_class_propfunc_t) luaA_client_get_urgent,
                            (lua_class_propfunc_t) luaA_client_set_urgent);
    luaA_class_add_property((lua_class_t *) &client_class, "focusable",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_focusable,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, "transient_for",
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_transient_for,
                            NULL);
    /* Property overrides */
    /* Cursor is not available */
    luaA_class_add_property((lua_class_t *) &client_class, "cursor", NULL, NULL, NULL);
    /* Type is not modifiable */
    luaA_class_add_property((lua_class_t *) &client_class, "type",
                            NULL,
                            (lua_class_propfunc_t) luaA_ewindow_get_type,
                            NULL);

    luaA_class_connect_signal(L, (lua_class_t *) &client_class, "focus", client_take_focus);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
