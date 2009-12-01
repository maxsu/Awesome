/*
 * property.c - property handlers
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
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

#include "awesome.h"
#include "event.h"
#include "property.h"
#include "ewmh.h"
#include "xwindow.h"
#include "systray.h"
#include "objects/screen.h"
#include "common/xutil.h"

#define HANDLE_TEXT_PROPERTY(funcname, atom, setfunc) \
    void \
    property_update_##funcname(client_t *c, xcb_get_property_reply_t *reply) \
    { \
        bool no_reply = !reply; \
        if(no_reply) \
            reply = xcb_get_property_reply(_G_connection, \
                                           xcb_get_any_property(_G_connection, \
                                                                false, \
                                                                c->window, \
                                                                atom, \
                                                                UINT_MAX), NULL); \
        setfunc(_G_L, c, xutil_get_text_property_from_reply(reply)); \
        if(no_reply) \
            p_delete(&reply); \
    } \
    static int \
    property_handle_##funcname(void *data, \
                               xcb_connection_t *connection, \
                               uint8_t state, \
                               xcb_window_t window, \
                               xcb_atom_t name, \
                               xcb_get_property_reply_t *reply) \
    { \
        client_t *c = client_getbywin(window); \
        if(c) \
            property_update_##funcname(c, reply); \
        return 0; \
    }


HANDLE_TEXT_PROPERTY(wm_name, WM_NAME, client_set_alt_name)
HANDLE_TEXT_PROPERTY(net_wm_name, _NET_WM_NAME, client_set_name)
HANDLE_TEXT_PROPERTY(wm_icon_name, WM_ICON_NAME, client_set_alt_icon_name)
HANDLE_TEXT_PROPERTY(net_wm_icon_name, _NET_WM_ICON_NAME, client_set_icon_name)
HANDLE_TEXT_PROPERTY(wm_client_machine, WM_CLIENT_MACHINE, client_set_machine)
HANDLE_TEXT_PROPERTY(wm_window_role, WM_WINDOW_ROLE, client_set_role)

#undef HANDLE_TEXT_PROPERTY

#define HANDLE_PROPERTY(name) \
    static int \
    property_handle_##name(void *data, \
                           xcb_connection_t *connection, \
                           uint8_t state, \
                           xcb_window_t window, \
                           xcb_atom_t name, \
                           xcb_get_property_reply_t *reply) \
    { \
        client_t *c = client_getbywin(window); \
        if(c) \
            property_update_##name(c, reply); \
        return 0; \
    }

HANDLE_PROPERTY(wm_protocols)
HANDLE_PROPERTY(wm_transient_for)
HANDLE_PROPERTY(wm_client_leader)
HANDLE_PROPERTY(wm_normal_hints)
HANDLE_PROPERTY(wm_hints)
HANDLE_PROPERTY(wm_class)
HANDLE_PROPERTY(net_wm_icon)
HANDLE_PROPERTY(net_wm_pid)

#undef HANDLE_PROPERTY

void
property_update_wm_transient_for(client_t *c, xcb_get_property_reply_t *reply)
{
    xcb_window_t trans;

    if(reply)
    {
        if(!xcb_get_wm_transient_for_from_reply(&trans, reply))
            return;
    }
    else
    {
        if(!xcb_get_wm_transient_for_reply(_G_connection,
                                            xcb_get_wm_transient_for_unchecked(_G_connection,
                                                                               c->window),
                                            &trans, NULL))
            return;
    }

    ewindow_set_type(_G_L, (ewindow_t *) c, EWINDOW_TYPE_DIALOG);
    ewindow_set_above(_G_L, (ewindow_t *) c, false);
    luaA_object_push(_G_L, client_getbywin(trans));
    client_set_transient_for(_G_L, c, -1);
    lua_pop(_G_L, 1);
}

/** Update leader hint of a client.
 * \param c The client.
 * \param reply (Optional) An existing reply.
 */
void
property_update_wm_client_leader(client_t *c, xcb_get_property_reply_t *reply)
{
    xcb_get_property_cookie_t client_leader_q;
    void *data;
    bool no_reply = !reply;

    if(no_reply)
    {
        client_leader_q = xcb_get_property_unchecked(_G_connection, false, c->window,
                                                     WM_CLIENT_LEADER, WINDOW, 0, 32);

        reply = xcb_get_property_reply(_G_connection, client_leader_q, NULL);
    }

    if(reply && reply->value_len && (data = xcb_get_property_value(reply)))
        c->leader_window = *(xcb_window_t *) data;

    /* Only free when we created a reply ourselves. */
    if(no_reply)
        p_delete(&reply);
}

/** Update the size hints of a client.
 * \param c The client.
 * \param reply (Optional) An existing reply.
 */
void
property_update_wm_normal_hints(client_t *c, xcb_get_property_reply_t *reply)
{
    if(reply)
    {
        if(!xcb_get_wm_size_hints_from_reply(&c->size_hints, reply))
            return;
    }
    else
    {
        if(!xcb_get_wm_normal_hints_reply(_G_connection,
                                          xcb_get_wm_normal_hints_unchecked(_G_connection,
                                                                            c->window),
                                          &c->size_hints, NULL))
            return;
    }

    c->resizable = !(c->size_hints.flags & XCB_SIZE_HINT_P_MAX_SIZE
                     && c->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE
                     && c->size_hints.max_width == c->size_hints.min_width
                     && c->size_hints.max_height == c->size_hints.min_height
                     && c->size_hints.max_width
                     && c->size_hints.max_height);
}

/** Update the WM hints of a client.
 * \param c The client.
 * \param reply (Optional) An existing reply.
 */
void
property_update_wm_hints(client_t *c, xcb_get_property_reply_t *reply)
{
    xcb_wm_hints_t wmh;

    if(reply)
    {
        if(!xcb_get_wm_hints_from_reply(&wmh, reply))
            return;
    }
    else
    {
        if(!xcb_get_wm_hints_reply(_G_connection,
                                  xcb_get_wm_hints_unchecked(_G_connection, c->window),
                                  &wmh, NULL))
            return;
    }

    client_set_urgent(_G_L, c, xcb_wm_hints_get_urgency(&wmh));

    if(wmh.flags & XCB_WM_HINT_INPUT)
        c->focusable = wmh.input;

    if(wmh.flags & XCB_WM_HINT_WINDOW_GROUP)
        client_set_group_window(_G_L, c, wmh.window_group);
}

/** Update WM_CLASS of a client.
 * \param c The client.
 * \param reply The reply to get property request, or NULL if none.
 */
void
property_update_wm_class(client_t *c, xcb_get_property_reply_t *reply)
{
    xcb_get_wm_class_reply_t hint;

    if(reply)
    {
        if(!xcb_get_wm_class_from_reply(&hint, reply))
            return;
    }
    else
    {
        if(!xcb_get_wm_class_reply(_G_connection,
                                   xcb_get_wm_class_unchecked(_G_connection, c->window),
                                   &hint, NULL))
            return;
    }

    client_set_class_instance(_G_L, c, hint.class_name, hint.instance_name);

    /* only delete reply if we get it ourselves */
    if(!reply)
        xcb_get_wm_class_reply_wipe(&hint);
}

static int
property_handle_net_wm_strut_partial(void *data,
                                     xcb_connection_t *connection,
                                     uint8_t state,
                                     xcb_window_t window,
                                     xcb_atom_t name,
                                     xcb_get_property_reply_t *reply)
{
    client_t *c = client_getbywin(window);

    if(c)
        ewmh_process_client_strut(c, reply);

    return 0;
}

void
property_update_net_wm_icon(client_t *c,
                            xcb_get_property_reply_t *reply)
{
    if(reply)
    {
        if(ewmh_window_icon_from_reply(reply))
            client_set_icon(_G_L, c, -1);
    }
    else if(ewmh_window_icon_get_reply(ewmh_window_icon_get_unchecked(c->window)))
        client_set_icon(_G_L, c, -1);
}

void
property_update_net_wm_pid(client_t *c,
                           xcb_get_property_reply_t *reply)
{
    bool no_reply = !reply;

    if(no_reply)
    {
        xcb_get_property_cookie_t prop_c =
            xcb_get_property_unchecked(_G_connection, false, c->window, _NET_WM_PID, CARDINAL, 0L, 1L);
        reply = xcb_get_property_reply(_G_connection, prop_c, NULL);
    }

    if(reply && reply->value_len)
    {
        uint32_t *rdata = xcb_get_property_value(reply);
        if(rdata)
            client_set_pid(_G_L, c, *rdata);
    }

    if(no_reply)
        p_delete(&reply);
}

/** Update the list of supported protocols for a client.
 * \param c The client.
 * \param reply The xcb property reply.
 */
void
property_update_wm_protocols(client_t *c, xcb_get_property_reply_t *reply)
{
    xcb_get_wm_protocols_reply_t protocols;

    if(reply)
    {
        if(!xcb_get_wm_protocols_from_reply(reply, &protocols))
            return;
    }
    else
    {
        /* If this fails for any reason, we still got the old value */
        if(!xcb_get_wm_protocols_reply(_G_connection,
                                      xcb_get_wm_protocols_unchecked(_G_connection,
                                                                     c->window, WM_PROTOCOLS),
                                      &protocols, NULL))
            return;
    }

    xcb_get_wm_protocols_reply_wipe(&c->protocols);
    memcpy(&c->protocols, &protocols, sizeof(protocols));
}

/** The property notify event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param state currently unused
 * \param window The window to obtain update the property with.
 * \param name The protocol atom, currently unused.
 * \param reply (Optional) An existing reply.
 */
static int
property_handle_xembed_info(void *data __attribute__ ((unused)),
                            xcb_connection_t *connection,
                            uint8_t state,
                            xcb_window_t window,
                            xcb_atom_t name,
                            xcb_get_property_reply_t *reply)
{
    xembed_window_t *emwin = xembed_getbywin(&_G_embedded, window);

    if(emwin)
        xembed_property_update(connection, emwin, reply);

    return 0;
}

static int
property_handle_xrootpmap_id(void *data __attribute__ ((unused)),
                             xcb_connection_t *connection,
                             uint8_t state,
                             xcb_window_t window,
                             xcb_atom_t name,
                             xcb_get_property_reply_t *reply)
{
    if(window != _G_root->window)
        return 0;

    xcb_pixmap_t pixmap;

    if(reply && reply->value_len)
        pixmap = *(xcb_pixmap_t *) xcb_get_property_value(reply);
    else
        pixmap = XCB_NONE;

    if(pixmap != _G_root->pixmap)
    {
        _G_root->pixmap = pixmap;
        window_emit_signal(_G_L, _G_root, "property::pixmap", 0);
    }

    return 0;
}

static int
property_handle_net_wm_opacity(void *data __attribute__ ((unused)),
                               xcb_connection_t *connection,
                               uint8_t state,
                               xcb_window_t window,
                               xcb_atom_t name,
                               xcb_get_property_reply_t *reply)
{
    ewindow_t *ewindow = ewindow_getbywin(window);

    if(ewindow)
        ewindow_set_opacity(_G_L, ewindow, xwindow_get_opacity_from_reply(reply));

    return 0;
}

void a_xcb_set_property_handlers(void)
{
    static xcb_property_handlers_t prophs;

    /* init */
    xcb_property_handlers_init(&prophs, &_G_evenths);

    /* Xembed stuff */
    xcb_property_set_handler(&prophs, _XEMBED_INFO, UINT_MAX,
                             property_handle_xembed_info, NULL);

    /* ICCCM stuff */
    xcb_property_set_handler(&prophs, WM_TRANSIENT_FOR, UINT_MAX,
                             property_handle_wm_transient_for, NULL);
    xcb_property_set_handler(&prophs, WM_CLIENT_LEADER, UINT_MAX,
                             property_handle_wm_client_leader, NULL);
    xcb_property_set_handler(&prophs, WM_NORMAL_HINTS, UINT_MAX,
                             property_handle_wm_normal_hints, NULL);
    xcb_property_set_handler(&prophs, WM_HINTS, UINT_MAX,
                             property_handle_wm_hints, NULL);
    xcb_property_set_handler(&prophs, WM_NAME, UINT_MAX,
                             property_handle_wm_name, NULL);
    xcb_property_set_handler(&prophs, WM_ICON_NAME, UINT_MAX,
                             property_handle_wm_icon_name, NULL);
    xcb_property_set_handler(&prophs, WM_CLASS, UINT_MAX,
                             property_handle_wm_class, NULL);
    xcb_property_set_handler(&prophs, WM_PROTOCOLS, UINT_MAX,
                             property_handle_wm_protocols, NULL);
    xcb_property_set_handler(&prophs, WM_CLIENT_MACHINE, UINT_MAX,
                             property_handle_wm_client_machine, NULL);
    xcb_property_set_handler(&prophs, WM_WINDOW_ROLE, UINT_MAX,
                             property_handle_wm_window_role, NULL);

    /* EWMH stuff */
    xcb_property_set_handler(&prophs, _NET_WM_NAME, UINT_MAX,
                             property_handle_net_wm_name, NULL);
    xcb_property_set_handler(&prophs, _NET_WM_ICON_NAME, UINT_MAX,
                             property_handle_net_wm_icon_name, NULL);
    xcb_property_set_handler(&prophs, _NET_WM_STRUT_PARTIAL, UINT_MAX,
                             property_handle_net_wm_strut_partial, NULL);
    xcb_property_set_handler(&prophs, _NET_WM_ICON, UINT_MAX,
                             property_handle_net_wm_icon, NULL);
    xcb_property_set_handler(&prophs, _NET_WM_PID, UINT_MAX,
                             property_handle_net_wm_pid, NULL);
    xcb_property_set_handler(&prophs, _NET_WM_WINDOW_OPACITY, 1,
                             property_handle_net_wm_opacity, NULL);

    /* background change */
    xcb_property_set_handler(&prophs, _XROOTPMAP_ID, 1,
                             property_handle_xrootpmap_id, NULL);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
