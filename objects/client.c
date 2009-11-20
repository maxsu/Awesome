/*
 * client.c - client management
 *
 * Copyright © 2007-2009 Julien Danjou <julien@danjou.info>
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
 * \param cidx The client index on the stack.
 * \param urgent The new flag state.
 */
void
client_set_urgent(lua_State *L, int cidx, bool urgent)
{
    client_t *c = luaA_checkudata(L, cidx, (lua_class_t *) &client_class);

    if(c->urgent != urgent)
    {
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

        luaA_object_emit_signal(L, cidx, "property::urgent", 0);
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
    client_t **c = client_array_lookup(&globalconf.clients, &(client_t) { .window = w });
    return c ? *c : NULL;
}

/** Get a client by its frame window.
 * \param w The client window to find.
 * \return A client pointer if found, NULL otherwise.
 */
client_t *
client_getbyframewin(xcb_window_t w)
{
    foreach(c, globalconf.clients)
        if((*c)->frame_window == w)
            return *c;

    return NULL;
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

static void
client_update_properties(client_t *c)
{
    /* get all hints */
    xcb_get_property_cookie_t wm_normal_hints   = property_get_wm_normal_hints(c);
    xcb_get_property_cookie_t wm_hints          = property_get_wm_hints(c);
    xcb_get_property_cookie_t wm_transient_for  = property_get_wm_transient_for(c);
    xcb_get_property_cookie_t wm_client_leader  = property_get_wm_client_leader(c);
    xcb_get_property_cookie_t wm_client_machine = property_get_wm_client_machine(c);
    xcb_get_property_cookie_t wm_window_role    = property_get_wm_window_role(c);
    xcb_get_property_cookie_t net_wm_pid        = property_get_net_wm_pid(c);
    xcb_get_property_cookie_t net_wm_icon       = property_get_net_wm_icon(c);
    xcb_get_property_cookie_t wm_name           = property_get_wm_name(c);
    xcb_get_property_cookie_t net_wm_name       = property_get_net_wm_name(c);
    xcb_get_property_cookie_t wm_icon_name      = property_get_wm_icon_name(c);
    xcb_get_property_cookie_t net_wm_icon_name  = property_get_net_wm_icon_name(c);
    xcb_get_property_cookie_t wm_class          = property_get_wm_class(c);
    xcb_get_property_cookie_t wm_protocols      = property_get_wm_protocols(c);
    xcb_get_property_cookie_t opacity           = xwindow_get_opacity_unchecked(c->window);

    /* update strut */
    ewmh_process_client_strut(c);

    /* Now process all replies */
    property_update_wm_normal_hints(c, wm_normal_hints);
    property_update_wm_hints(c, wm_hints);
    property_update_wm_transient_for(c, wm_transient_for);
    property_update_wm_client_leader(c, wm_client_leader);
    property_update_wm_client_machine(c, wm_client_machine);
    property_update_wm_window_role(c, wm_window_role);
    property_update_net_wm_pid(c, net_wm_pid);
    property_update_net_wm_icon(c, net_wm_icon);
    property_update_wm_name(c, wm_name);
    property_update_net_wm_name(c, net_wm_name);
    property_update_wm_icon_name(c, wm_icon_name);
    property_update_net_wm_icon_name(c, net_wm_icon_name);
    property_update_wm_class(c, wm_class);
    property_update_wm_protocols(c, wm_protocols);
    ewindow_set_opacity(globalconf.L, -1, xwindow_get_opacity_from_cookie(opacity));
}

/** Manage a new client.
 * \param w The window.
 * \param wgeom Window geometry.
 * \param startup True if we are managing at startup time.
 */
void
client_manage(xcb_window_t w, xcb_get_geometry_reply_t *wgeom, bool startup)
{
    const uint32_t select_input_val[] = { CLIENT_SELECT_INPUT_EVENT_MASK };

    if(systray_iskdedockapp(w))
    {
        systray_request_handle(w, NULL);
        return;
    }

    /* If this is a new client that just has been launched, then request its
     * startup id. */
    xcb_get_property_cookie_t startup_id_q = { 0 };
    if(!startup)
        startup_id_q = xcb_get_property(_G_connection, false, w,
                                        _NET_STARTUP_ID, XCB_GET_PROPERTY_TYPE_ANY, 0, UINT_MAX);

    /* Make sure the window is automatically mapped if awesome exits or dies. */
    xcb_change_save_set(_G_connection, XCB_SET_MODE_INSERT, w);

    client_t *c = client_new(globalconf.L);
    xcb_screen_t *s = globalconf.screen;

    /* consider the window banned */
    c->banned = true;
    /* Store window */
    c->window = w;
    /* Store parent */
    c->parent = globalconf.screens.tab[0].root;

    c->frame_window = xcb_generate_id(_G_connection);
    xcb_create_window(_G_connection, s->root_depth, c->frame_window, s->root,
                      wgeom->x, wgeom->y, wgeom->width, wgeom->height,
                      wgeom->border_width, XCB_COPY_FROM_PARENT, s->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_BIT_GRAVITY
                      | XCB_CW_WIN_GRAVITY | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                      (const uint32_t [])
                      {
                          globalconf.colors.bg.pixel,
                          globalconf.colors.bg.pixel,
                          XCB_GRAVITY_NORTH_WEST,
                          XCB_GRAVITY_NORTH_WEST,
                          1,
                          FRAME_SELECT_INPUT_EVENT_MASK
                      });
    xcb_reparent_window(_G_connection, w, c->frame_window, 0, 0);
    xcb_map_window(_G_connection, w);

    /* Do this now so that we don't get any events for the above
     * (Else, reparent could cause an UnmapNotify) */
    xcb_change_window_attributes(_G_connection, w, XCB_CW_EVENT_MASK, select_input_val);

    luaA_object_emit_signal(globalconf.L, -1, "property::window", 0);
    /* Consider window is focusable by default */
    c->focusable = true;
    /* Consider window movable/resizable by default */
    c->movable = c->resizable = true;

    /* The frame window gets the border, not the real client window */
    xcb_configure_window(_G_connection, w,
                         XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         (uint32_t[]) { 0 });

    /* Move this window to the bottom of the stack. Without this we would force
     * other windows which will be above this one to redraw themselves because
     * this window occludes them for a tiny moment. The next stack_refresh()
     * will fix this up and move the window to its correct place. */
    xcb_configure_window(_G_connection, c->frame_window,
                         XCB_CONFIG_WINDOW_STACK_MODE,
                         (uint32_t[]) { XCB_STACK_MODE_BELOW});

    /* Duplicate client and push it in client list */
    lua_pushvalue(globalconf.L, -1);
    client_array_insert(&globalconf.clients, luaA_object_ref(globalconf.L, -1));
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

    /* update all properties */
    client_update_properties(c);

    /* Then check clients hints */
    ewmh_client_check_hints(c);

    /* Push client in stack */
    stack_window_raise(globalconf.L, -1);

    /* Always stay in NORMAL_STATE. Even though iconified seems more
     * appropriate sometimes. The only possible loss is that clients not using
     * visibility events may continue to process data (when banned).
     * Without any exposes or other events the cost should be fairly limited though.
     *
     * Some clients may expect the window to be unmapped when STATE_ICONIFIED.
     * Two conflicting parts of the ICCCM v2.0 (section 4.1.4):
     *
     * "Normal -> Iconic - The client should send a ClientMessage event as described later in this section."
     * (note no explicit mention of unmapping, while Normal->Widthdrawn does mention that)
     *
     * "Once a client's window has left the Withdrawn state, the window will be mapped
     * if it is in the Normal state and the window will be unmapped if it is in the Iconic state."
     *
     * At this stage it's just safer to keep it in normal state and avoid confusion.
     */
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
    client_array_lookup_and_remove(&globalconf.clients, &(client_t) { .window = c->window });
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

    /* Clear our event mask so that we don't receive any events from now on,
     * especially not for the following requests. */
    xcb_change_window_attributes(_G_connection,
                                 c->window,
                                 XCB_CW_EVENT_MASK,
                                 (const uint32_t []) { 0 });
    xcb_change_window_attributes(_G_connection,
                                 c->frame_window,
                                 XCB_CW_EVENT_MASK,
                                 (const uint32_t []) { 0 });

    xcb_unmap_window(_G_connection, c->window);
    xcb_reparent_window(_G_connection, c->window, globalconf.screen->root,
            c->geometry.x, c->geometry.y);
    xcb_destroy_window(_G_connection, c->frame_window);

    /* Remove this window from the save set since this shouldn't be made visible
     * after a restart anymore. */
    xcb_change_save_set(_G_connection, XCB_SET_MODE_DELETE, c->window);

    /* Do this last to avoid races with clients. According to ICCCM, clients
     * arent allowed to re-use the window until after this. */
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
        ev.data.data32[1] = globalconf.timestamp;
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

    lua_createtable(L, globalconf.clients.len, 0);

    foreach(c, globalconf.clients)
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

static int
luaA_client_set_urgent(lua_State *L, client_t *c)
{
    client_set_urgent(L, -3, luaA_checkboolean(L, -1));
    return 0;
}

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
                     (lua_class_allocator_t) client_new,
                     (lua_class_collector_t) client_wipe,
                     (lua_class_checker_t) client_checker,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     client_methods, NULL, NULL);

    luaA_class_add_property((lua_class_t *) &client_class, A_TK_NAME,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_name,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_SKIP_TASKBAR,
                            (lua_class_propfunc_t) luaA_client_set_skip_taskbar,
                            (lua_class_propfunc_t) luaA_client_get_skip_taskbar,
                            (lua_class_propfunc_t) luaA_client_set_skip_taskbar);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_CLASS,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_class,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_INSTANCE,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_instance,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_ROLE,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_role,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_PID,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_pid,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_LEADER_WINDOW,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_leader_window,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_MACHINE,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_machine,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_ICON_NAME,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_icon_name,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_GROUP_WINDOW,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_group_window,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_ICON,
                            (lua_class_propfunc_t) luaA_client_set_icon,
                            (lua_class_propfunc_t) luaA_client_get_icon,
                            (lua_class_propfunc_t) luaA_client_set_icon);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_URGENT,
                            (lua_class_propfunc_t) luaA_client_set_urgent,
                            (lua_class_propfunc_t) luaA_client_get_urgent,
                            (lua_class_propfunc_t) luaA_client_set_urgent);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_TRANSIENT_FOR,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_transient_for,
                            NULL);
    /* Property overrides */
    /* Cursor is not available */
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_CURSOR, NULL, NULL, NULL);
    /* Type is not modifiable */
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_TYPE,
                            NULL,
                            (lua_class_propfunc_t) luaA_ewindow_get_type,
                            NULL);

    luaA_class_connect_signal(L, (lua_class_t *) &client_class, "focus", client_take_focus);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
