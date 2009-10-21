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
#include <xcb/xcb_image.h>

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

extern window_t *window_focused;

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

/** Returns true if a client is visible.
 * Banned client are considered as visible even if they are not (yet) mapped.
 * \param c The client to check.
 * \param screen Virtual screen number.
 * \return True if the client is visible, false otherwise.
 */
static bool
client_isvisible(client_t *c)
{
    return (!c->hidden && client_maybevisible(c));
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
            xcb_get_wm_hints_unchecked(globalconf.connection, c->window);

        c->urgent = urgent;

        /* update ICCCM hints */
        xcb_wm_hints_t wmh;
        xcb_get_wm_hints_reply(globalconf.connection, hints, &wmh, NULL);

        if(urgent)
            wmh.flags |= XCB_WM_HINT_X_URGENCY;
        else
            wmh.flags &= ~XCB_WM_HINT_X_URGENCY;

        xcb_set_wm_hints(globalconf.connection, c->window, &wmh);

        luaA_object_emit_signal(L, cidx, "property::urgent", 0);
    }
}

LUA_OBJECT_DO_SET_PROPERTY_FUNC(client, (lua_class_t *) &client_class, client_t, group_window)
LUA_OBJECT_DO_SET_PROPERTY_FUNC(client, (lua_class_t *) &client_class, client_t, type)
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

/** Returns true if a client is tagged
 * with one of the tags of the specified screen.
 * \param c The client to check.
 * \param screen Virtual screen.
 * \return true if the client is visible, false otherwise.
 */
bool
client_maybevisible(client_t *c)
{
    if(c->type == WINDOW_TYPE_DESKTOP)
        return true;
    return ewindow_isvisible((ewindow_t *) c);
}

/** Get a client by its window.
 * \param w The client window to find.
 * \return A client pointer if found, NULL otherwise.
 */
client_t *
client_getbywin(xcb_window_t w)
{
    foreach(c, globalconf.clients)
        if((*c)->window == w)
            return *c;

    return NULL;
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

/** This is part of The Bob Marley Algorithm: we ignore enter and leave window
 * in certain cases, like map/unmap or move, so we don't get spurious events.
 */
void
client_ignore_enterleave_events(void)
{
    foreach(c, globalconf.clients)
    {
        xcb_change_window_attributes(globalconf.connection,
                                     (*c)->window,
                                     XCB_CW_EVENT_MASK,
                                     (const uint32_t []) { CLIENT_SELECT_INPUT_EVENT_MASK & ~(XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW) });
        xcb_change_window_attributes(globalconf.connection,
                                     (*c)->frame_window,
                                     XCB_CW_EVENT_MASK,
                                     (const uint32_t []) { FRAME_SELECT_INPUT_EVENT_MASK & ~(XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW) });
    }
}

void
client_restore_enterleave_events(void)
{
    foreach(c, globalconf.clients)
    {
        xcb_change_window_attributes(globalconf.connection,
                                     (*c)->window,
                                     XCB_CW_EVENT_MASK,
                                     (const uint32_t []) { CLIENT_SELECT_INPUT_EVENT_MASK });
        xcb_change_window_attributes(globalconf.connection,
                                     (*c)->frame_window,
                                     XCB_CW_EVENT_MASK,
                                     (const uint32_t []) { FRAME_SELECT_INPUT_EVENT_MASK });
    }
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
        startup_id_q = xcb_get_property(globalconf.connection, false, w,
                                        _NET_STARTUP_ID, XCB_GET_PROPERTY_TYPE_ANY, 0, UINT_MAX);

    /* Make sure the window is automatically mapped if awesome exits or dies. */
    xcb_change_save_set(globalconf.connection, XCB_SET_MODE_INSERT, w);

    client_t *c = client_new(globalconf.L);
    xcb_screen_t *s = globalconf.screen;

    /* Set initial screen */
    c->screen = &globalconf.screens.tab[0];
    /* consider the window banned */
    c->banned = true;
    /* Store window */
    c->window = w;
    c->frame_window = xcb_generate_id(globalconf.connection);
    xcb_create_window(globalconf.connection, s->root_depth, c->frame_window, s->root,
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
    xcb_reparent_window(globalconf.connection, w, c->frame_window, 0, 0);
    xcb_map_window(globalconf.connection, w);

    /* Do this now so that we don't get any events for the above
     * (Else, reparent could cause an UnmapNotify) */
    xcb_change_window_attributes(globalconf.connection, w, XCB_CW_EVENT_MASK, select_input_val);

    luaA_object_emit_signal(globalconf.L, -1, "property::window", 0);
    /* Consider window is focusable by default */
    c->focusable = true;

    /* The frame window gets the border, not the real client window */
    xcb_configure_window(globalconf.connection, w,
                         XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         (uint32_t[]) { 0 });

    /* Move this window to the bottom of the stack. Without this we would force
     * other windows which will be above this one to redraw themselves because
     * this window occludes them for a tiny moment. The next stack_refresh()
     * will fix this up and move the window to its correct place. */
    xcb_configure_window(globalconf.connection, c->frame_window,
                         XCB_CONFIG_WINDOW_STACK_MODE,
                         (uint32_t[]) { XCB_STACK_MODE_BELOW});

    /* Duplicate client and push it in client list */
    lua_pushvalue(globalconf.L, -1);
    client_array_push(&globalconf.clients, luaA_object_ref(globalconf.L, -1));

    /* Set the right screen */
    screen_client_moveto(c, screen_getbycoord(wgeom->x, wgeom->y), false);

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

    /* we honor size hints by default */
    c->size_hints_honor = true;
    luaA_object_emit_signal(globalconf.L, -1, "property::size_hints_honor", 0);

    /* update all properties */
    client_update_properties(c);

    /* Then check clients hints */
    ewmh_client_check_hints(c);

    /* Push client in stack */
    client_raise(c);

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
            xcb_get_property_reply(globalconf.connection, startup_id_q, NULL);
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

/** Compute client geometry with respect to its geometry hints.
 * \param c The client.
 * \param geometry The geometry that the client might receive.
 * \return The geometry the client must take respecting its hints.
 */
area_t
client_geometry_hints(client_t *c, area_t geometry)
{
    int32_t basew, baseh, minw, minh;
    int32_t real_basew = 0, real_baseh = 0;

    /* base size is substituted with min size if not specified */
    if(c->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
    {
        basew = c->size_hints.base_width;
        baseh = c->size_hints.base_height;
        real_basew = basew;
        real_baseh = baseh;
    }
    else if(c->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        basew = c->size_hints.min_width;
        baseh = c->size_hints.min_height;
    }
    else
        basew = baseh = 0;

    /* min size is substituted with base size if not specified */
    if(c->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        minw = c->size_hints.min_width;
        minh = c->size_hints.min_height;
    }
    else if(c->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
    {
        minw = c->size_hints.base_width;
        minh = c->size_hints.base_height;
    }
    else
        minw = minh = 0;

    if(c->size_hints.flags & XCB_SIZE_HINT_P_ASPECT
       && c->size_hints.min_aspect_den > 0
       && c->size_hints.max_aspect_den > 0
       && geometry.height - real_baseh > 0
       && geometry.width - real_basew > 0)
    {
        /* ICCCM mandates:
         * If a base size is provided along with the aspect ratio fields, the
         * base size should be subtracted from the window size prior to checking
         * that the aspect ratio falls in range. If a base size is not provided,
         * nothing should be subtracted from the window size. (The minimum size
         * is not to be used in place of the base size for this purpose.) */
        double dx = (double) (geometry.width - real_basew);
        double dy = (double) (geometry.height - real_baseh);
        double min = (double) c->size_hints.min_aspect_num / (double) c->size_hints.min_aspect_den;
        double max = (double) c->size_hints.max_aspect_num / (double) c->size_hints.max_aspect_den;
        double ratio = dx / dy;
        if(max > 0 && min > 0 && ratio > 0)
        {
            if(ratio < min)
            {
                /* dx is lower than allowed, make dy lower to compensate this
                 * (+ 0.5 to force proper rounding). */
                dy = dx / min + 0.5;
                geometry.width = (int) dx + real_basew;
                geometry.height = (int) dy + real_baseh;
            }
            else if(ratio > max)
            {
                /* dx is too high, lower it (+0.5 for proper rounding) */
                dx = dy * max + 0.5;
                geometry.width = (int) dx + real_basew;
                geometry.height = (int) dy + real_baseh;
            }
        }
    }

    if(minw)
        geometry.width = MAX(geometry.width, minw);
    if(minh)
        geometry.height = MAX(geometry.height, minh);

    if(c->size_hints.flags & XCB_SIZE_HINT_P_MAX_SIZE)
    {
        if(c->size_hints.max_width)
            geometry.width = MIN(geometry.width, c->size_hints.max_width);
        if(c->size_hints.max_height)
            geometry.height = MIN(geometry.height, c->size_hints.max_height);
    }

    if(c->size_hints.flags & (XCB_SIZE_HINT_P_RESIZE_INC | XCB_SIZE_HINT_BASE_SIZE)
       && c->size_hints.width_inc && c->size_hints.height_inc)
    {
        uint16_t t1 = geometry.width, t2 = geometry.height;
        unsigned_subtract(t1, basew);
        unsigned_subtract(t2, baseh);
        geometry.width -= t1 % c->size_hints.width_inc;
        geometry.height -= t2 % c->size_hints.height_inc;
    }

    return geometry;
}

/** Resize client window.
 * The sizes given as parameters are with borders!
 * \param c Client to resize.
 * \param geometry New window geometry.
 * \param hints Use size hints.
 * \return true if an actual resize occurred.
 */
bool
client_resize(client_t *c, area_t geometry, bool hints)
{
    area_t area;

    /* offscreen appearance fixes */
    area = display_area_get();

    if(geometry.x > area.width)
        geometry.x = area.width - geometry.width;
    if(geometry.y > area.height)
        geometry.y = area.height - geometry.height;
    if(geometry.x + geometry.width < 0)
        geometry.x = 0;
    if(geometry.y + geometry.height < 0)
        geometry.y = 0;

    if(hints && !c->fullscreen)
        geometry = client_geometry_hints(c, geometry);

    if(geometry.width == 0 || geometry.height == 0)
        return false;

    if(c->geometry.x != geometry.x
       || c->geometry.y != geometry.y
       || c->geometry.width != geometry.width
       || c->geometry.height != geometry.height)
    {
        bool send_notice = false;
        screen_t *new_screen = screen_getbycoord(geometry.x, geometry.y);

        if(c->geometry.width == geometry.width
           && c->geometry.height == geometry.height)
            send_notice = true;

        /* Also store geometry including border */
        c->geometry = geometry;

        /* Ignore all spurious enter/leave notify events */
        client_ignore_enterleave_events();

        xcb_configure_window(globalconf.connection, c->window,
                XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                (uint32_t[]) { geometry.width, geometry.height });
        xcb_configure_window(globalconf.connection, c->frame_window,
                XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
                (uint32_t[]) { geometry.x, geometry.y, geometry.width, geometry.height });

        if(send_notice)
            /* We are moving without changing the size, see ICCCM 4.2.3 */
            xwindow_configure(c->window, geometry, c->border_width);

        client_restore_enterleave_events();

        screen_client_moveto(c, new_screen, false);

        luaA_object_push(globalconf.L, c);
        luaA_object_emit_signal(globalconf.L, -1, "property::geometry", 0);
        /** \todo This need to be VERIFIED before it is emitted! */
        luaA_object_emit_signal(globalconf.L, -1, "property::x", 0);
        luaA_object_emit_signal(globalconf.L, -1, "property::y", 0);
        luaA_object_emit_signal(globalconf.L, -1, "property::width", 0);
        luaA_object_emit_signal(globalconf.L, -1, "property::height", 0);
        lua_pop(globalconf.L, 1);

        return true;
    }

    return false;
}

/** Unmanage a client.
 * \param c The client.
 */
void
client_unmanage(client_t *c)
{
    /* remove client from global list and everywhere else */
    foreach(elem, globalconf.clients)
        if(*elem == c)
        {
            client_array_remove(&globalconf.clients, elem);
            break;
        }
    stack_client_remove(c);

    /* Tag and window reference each other so there are tight forever.
     * We don't want the tag the unmanaged client to be referenced forever in a
     * tag so we untag it. */
    luaA_object_push(globalconf.L, c);
    foreach(tag, c->tags)
    {
        luaA_object_push_item(globalconf.L, -1, *tag);
        untag_ewindow(globalconf.L, -2, -1);
        lua_pop(globalconf.L, 1);
    }

    luaA_object_emit_signal(globalconf.L, -1, "unmanage", 0);
    lua_pop(globalconf.L, 1);

    if(strut_has_value(&c->strut))
        screen_emit_signal(globalconf.L, c->screen, "property::workarea", 0);

    /* Clear our event mask so that we don't receive any events from now on,
     * especially not for the following requests. */
    xcb_change_window_attributes(globalconf.connection,
                                 c->window,
                                 XCB_CW_EVENT_MASK,
                                 (const uint32_t []) { 0 });
    xcb_change_window_attributes(globalconf.connection,
                                 c->frame_window,
                                 XCB_CW_EVENT_MASK,
                                 (const uint32_t []) { 0 });

    xcb_unmap_window(globalconf.connection, c->window);
    xcb_reparent_window(globalconf.connection, c->window, globalconf.screen->root,
            c->geometry.x, c->geometry.y);
    xcb_destroy_window(globalconf.connection, c->frame_window);

    /* Remove this window from the save set since this shouldn't be made visible
     * after a restart anymore. */
    xcb_change_save_set(globalconf.connection, XCB_SET_MODE_DELETE, c->window);

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

        xcb_send_event(globalconf.connection, false, c->window,
                       XCB_EVENT_MASK_NO_EVENT, (char *) &ev);
    }
    else
        xcb_kill_client(globalconf.connection, c->window);
}

/** Get all clients into a table.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam An optional screen number.
 * \lreturn A table with all clients.
 */
static int
luaA_client_get(lua_State *L)
{
    int i = 1, screen;

    screen = luaL_optnumber(L, 1, 0) - 1;

    lua_newtable(L);

    if(screen == -1)
        foreach(c, globalconf.clients)
        {
            luaA_object_push(L, *c);
            lua_rawseti(L, -2, i++);
        }
    else
    {
        luaA_checkscreen(screen);
        foreach(c, globalconf.clients)
            if((*c)->screen == &globalconf.screens.tab[screen])
            {
                luaA_object_push(L, *c);
                lua_rawseti(L, -2, i++);
            }
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

/** Raise a client on top of others which are on the same layer.
 * \param L The Lua VM state.
 * \luastack
 * \lvalue A client.
 */
static int
luaA_client_raise(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);
    client_raise(c);
    return 0;
}

/** Lower a client on bottom of others which are on the same layer.
 * \param L The Lua VM state.
 * \luastack
 * \lvalue A client.
 */
static int
luaA_client_lower(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);

    stack_client_push(c);

    /* Traverse all transient layers. */
    for(client_t *tc = c->transient_for; tc; tc = tc->transient_for)
        stack_client_push(tc);

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

/** Return client geometry.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam A table with new coordinates, or none.
 * \lreturn A table with client coordinates.
 */
static int
luaA_client_geometry(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);

    if(lua_gettop(L) == 2 && !lua_isnil(L, 2))
    {
        area_t geometry;

        luaA_checktable(L, 2);
        geometry.x = luaA_getopt_number(L, 2, "x", c->geometry.x);
        geometry.y = luaA_getopt_number(L, 2, "y", c->geometry.y);
        if(client_isfixed(c))
        {
            geometry.width = c->geometry.width;
            geometry.height = c->geometry.height;
        }
        else
        {
            geometry.width = luaA_getopt_number(L, 2, "width", c->geometry.width);
            geometry.height = luaA_getopt_number(L, 2, "height", c->geometry.height);
        }

        client_resize(c, geometry, c->size_hints_honor);
    }

    return luaA_pusharea(L, c->geometry);
}

static int
luaA_client_set_screen(lua_State *L, client_t *c)
{
    int screen = luaL_checknumber(L, -1) - 1;
    luaA_checkscreen(screen);
    screen_client_moveto(c, &globalconf.screens.tab[screen], true);

    return 0;
}

static int
luaA_client_set_hidden(lua_State *L, client_t *c)
{
    bool b = luaA_checkboolean(L, -1);
    if(b != c->hidden)
    {
        c->hidden = b;
        if(strut_has_value(&c->strut))
            screen_emit_signal(globalconf.L, c->screen, "property::workarea", 0);
        luaA_object_emit_signal(L, -3, "property::hidden", 0);
    }
    return 0;
}

static int
luaA_client_set_icon(lua_State *L, client_t *c)
{
    client_set_icon(L, -3, -1);
    return 0;
}

static int
luaA_client_set_size_hints_honor(lua_State *L, client_t *c)
{
    c->size_hints_honor = luaA_checkboolean(L, -1);
    luaA_object_emit_signal(L, -3, "property::size_hints_honor", 0);
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
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, hidden, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, urgent, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(client, client_t, size_hints_honor, lua_pushboolean)

static int
luaA_client_get_content(lua_State *L, client_t *c)
{
    xcb_image_t *ximage = xcb_image_get(globalconf.connection,
                                        c->window,
                                        0, 0,
                                        c->geometry.width,
                                        c->geometry.height,
                                        ~0, XCB_IMAGE_FORMAT_Z_PIXMAP);
    int retval = 0;

    if(ximage)
    {
        if(ximage->bpp >= 24)
        {
            uint32_t *data = p_alloca(uint32_t, ximage->width * ximage->height);

            for(int y = 0; y < ximage->height; y++)
                for(int x = 0; x < ximage->width; x++)
                {
                    data[y * ximage->width + x] = xcb_image_get_pixel(ximage, x, y);
                    data[y * ximage->width + x] |= 0xff000000; /* set alpha to 0xff */
                }

            retval = image_new_from_argb32(L, ximage->width, ximage->height, data);
        }
        xcb_image_destroy(ximage);
    }

    return retval;
}

static int
luaA_client_get_type(lua_State *L, client_t *c)
{
    switch(c->type)
    {
      case WINDOW_TYPE_DESKTOP:
        lua_pushliteral(L, "desktop");
        break;
      case WINDOW_TYPE_DOCK:
        lua_pushliteral(L, "dock");
        break;
      case WINDOW_TYPE_SPLASH:
        lua_pushliteral(L, "splash");
        break;
      case WINDOW_TYPE_DIALOG:
        lua_pushliteral(L, "dialog");
        break;
      case WINDOW_TYPE_MENU:
        lua_pushliteral(L, "menu");
        break;
      case WINDOW_TYPE_TOOLBAR:
        lua_pushliteral(L, "toolbar");
        break;
      case WINDOW_TYPE_UTILITY:
        lua_pushliteral(L, "utility");
        break;
      case WINDOW_TYPE_DROPDOWN_MENU:
        lua_pushliteral(L, "dropdown_menu");
        break;
      case WINDOW_TYPE_POPUP_MENU:
        lua_pushliteral(L, "popup_menu");
        break;
      case WINDOW_TYPE_TOOLTIP:
        lua_pushliteral(L, "tooltip");
        break;
      case WINDOW_TYPE_NOTIFICATION:
        lua_pushliteral(L, "notification");
        break;
      case WINDOW_TYPE_COMBO:
        lua_pushliteral(L, "combo");
        break;
      case WINDOW_TYPE_DND:
        lua_pushliteral(L, "dnd");
        break;
      case WINDOW_TYPE_NORMAL:
        lua_pushliteral(L, "normal");
        break;
    }
    return 1;
}

static int
luaA_client_get_screen(lua_State *L, client_t *c)
{
    if(!c->screen)
        return 0;
    lua_pushnumber(L, 1 + screen_array_indexof(&globalconf.screens, c->screen));
    return 1;
}

static int
luaA_client_get_icon(lua_State *L, client_t *c)
{
    return luaA_object_push_item(L, -2, c->icon);
}

static int
luaA_client_get_size_hints(lua_State *L, client_t *c)
{
    const char *u_or_p = NULL;

    lua_createtable(L, 0, 1);

    if(c->size_hints.flags & XCB_SIZE_HINT_US_POSITION)
        u_or_p = "user_position";
    else if(c->size_hints.flags & XCB_SIZE_HINT_P_POSITION)
        u_or_p = "program_position";

    if(u_or_p)
    {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, c->size_hints.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, c->size_hints.y);
        lua_setfield(L, -2, "y");
        lua_setfield(L, -2, u_or_p);
        u_or_p = NULL;
    }

    if(c->size_hints.flags & XCB_SIZE_HINT_US_SIZE)
        u_or_p = "user_size";
    else if(c->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
        u_or_p = "program_size";

    if(u_or_p)
    {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, c->size_hints.width);
        lua_setfield(L, -2, "width");
        lua_pushnumber(L, c->size_hints.height);
        lua_setfield(L, -2, "height");
        lua_setfield(L, -2, u_or_p);
    }

    if(c->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        lua_pushnumber(L, c->size_hints.min_width);
        lua_setfield(L, -2, "min_width");
        lua_pushnumber(L, c->size_hints.min_height);
        lua_setfield(L, -2, "min_height");
    }

    if(c->size_hints.flags & XCB_SIZE_HINT_P_MAX_SIZE)
    {
        lua_pushnumber(L, c->size_hints.max_width);
        lua_setfield(L, -2, "max_width");
        lua_pushnumber(L, c->size_hints.max_height);
        lua_setfield(L, -2, "max_height");
    }

    if(c->size_hints.flags & XCB_SIZE_HINT_P_RESIZE_INC)
    {
        lua_pushnumber(L, c->size_hints.width_inc);
        lua_setfield(L, -2, "width_inc");
        lua_pushnumber(L, c->size_hints.height_inc);
        lua_setfield(L, -2, "height_inc");
    }

    if(c->size_hints.flags & XCB_SIZE_HINT_P_ASPECT)
    {
        lua_pushnumber(L, c->size_hints.min_aspect_num);
        lua_setfield(L, -2, "min_aspect_num");
        lua_pushnumber(L, c->size_hints.min_aspect_den);
        lua_setfield(L, -2, "min_aspect_den");
        lua_pushnumber(L, c->size_hints.max_aspect_num);
        lua_setfield(L, -2, "max_aspect_num");
        lua_pushnumber(L, c->size_hints.max_aspect_den);
        lua_setfield(L, -2, "max_aspect_den");
    }

    if(c->size_hints.flags & XCB_SIZE_HINT_BASE_SIZE)
    {
        lua_pushnumber(L, c->size_hints.base_width);
        lua_setfield(L, -2, "base_width");
        lua_pushnumber(L, c->size_hints.base_height);
        lua_setfield(L, -2, "base_height");
    }

    if(c->size_hints.flags & XCB_SIZE_HINT_P_WIN_GRAVITY)
    {
        switch(c->size_hints.win_gravity)
        {
          default:
            lua_pushliteral(L, "north_west");
            break;
          case XCB_GRAVITY_NORTH:
            lua_pushliteral(L, "north");
            break;
          case XCB_GRAVITY_NORTH_EAST:
            lua_pushliteral(L, "north_east");
            break;
          case XCB_GRAVITY_WEST:
            lua_pushliteral(L, "west");
            break;
          case XCB_GRAVITY_CENTER:
            lua_pushliteral(L, "center");
            break;
          case XCB_GRAVITY_EAST:
            lua_pushliteral(L, "east");
            break;
          case XCB_GRAVITY_SOUTH_WEST:
            lua_pushliteral(L, "south_west");
            break;
          case XCB_GRAVITY_SOUTH:
            lua_pushliteral(L, "south");
            break;
          case XCB_GRAVITY_SOUTH_EAST:
            lua_pushliteral(L, "south_east");
            break;
          case XCB_GRAVITY_STATIC:
            lua_pushliteral(L, "static");
            break;
        }
        lua_setfield(L, -2, "win_gravity");
    }

    return 1;
}

/* Client module.
 * \param L The Lua VM state.
 * \return The number of pushed elements.
 */
static int
luaA_client_module_index(lua_State *L)
{
    size_t len;
    const char *buf = luaL_checklstring(L, 2, &len);

    switch(a_tokenize(buf, len))
    {
      case A_TK_FOCUS:
        return luaA_object_push(globalconf.L, window_focused);
        break;
      default:
        return 0;
    }
}

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
        { "geometry", luaA_client_geometry },
        { "kill", luaA_client_kill },
        { "raise", luaA_client_raise },
        { "lower", luaA_client_lower },
        { "unmanage", luaA_client_unmanage },
        { NULL, NULL }
    };

    static const struct luaL_reg client_module_meta[] =
    {
        { "__index", luaA_client_module_index },
        { NULL, NULL }
    };

    luaA_class_setup(L, (lua_class_t *) &client_class, "client", (lua_class_t *) &ewindow_class,
                     (lua_class_allocator_t) client_new,
                     (lua_class_collector_t) client_wipe,
                     (lua_class_checker_t) client_checker,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     client_methods, client_module_meta, NULL);

    luaA_class_add_property((lua_class_t *) &client_class, A_TK_NAME,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_name,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_SKIP_TASKBAR,
                            (lua_class_propfunc_t) luaA_client_set_skip_taskbar,
                            (lua_class_propfunc_t) luaA_client_get_skip_taskbar,
                            (lua_class_propfunc_t) luaA_client_set_skip_taskbar);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_CONTENT,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_content,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_TYPE,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_type,
                            NULL);
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
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_SCREEN,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_screen,
                            (lua_class_propfunc_t) luaA_client_set_screen);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_HIDDEN,
                            (lua_class_propfunc_t) luaA_client_set_hidden,
                            (lua_class_propfunc_t) luaA_client_get_hidden,
                            (lua_class_propfunc_t) luaA_client_set_hidden);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_GROUP_WINDOW,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_group_window,
                            NULL);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_ICON,
                            (lua_class_propfunc_t) luaA_client_set_icon,
                            (lua_class_propfunc_t) luaA_client_get_icon,
                            (lua_class_propfunc_t) luaA_client_set_icon);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_SIZE_HINTS_HONOR,
                            (lua_class_propfunc_t) luaA_client_set_size_hints_honor,
                            (lua_class_propfunc_t) luaA_client_get_size_hints_honor,
                            (lua_class_propfunc_t) luaA_client_set_size_hints_honor);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_URGENT,
                            (lua_class_propfunc_t) luaA_client_set_urgent,
                            (lua_class_propfunc_t) luaA_client_get_urgent,
                            (lua_class_propfunc_t) luaA_client_set_urgent);
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_SIZE_HINTS,
                            NULL,
                            (lua_class_propfunc_t) luaA_client_get_size_hints,
                            NULL);
    /* Property overrides */
    /* Cursor is not available */
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_CURSOR, NULL, NULL, NULL);
    /* Transient for is readonly */
    luaA_class_add_property((lua_class_t *) &client_class, A_TK_TRANSIENT_FOR,
                            NULL,
                            (lua_class_propfunc_t) luaA_ewindow_get_transient_for,
                            NULL);

    client_class.isvisible = (lua_interface_window_isvisible_t) client_isvisible;
    luaA_class_connect_signal(L, (lua_class_t *) &client_class, "focus", client_take_focus);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
