/*
 * xwindow.c - X window handling functions
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

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

#include "awesome.h"
#include "globalconf.h"
#include "xwindow.h"
#include "common/atoms.h"

/** Set client state (WM_STATE) property.
 * \param win The window to set state.
 * \param state The state to set.
 */
void
xwindow_set_state(xcb_window_t win, uint32_t state)
{
    uint32_t data[] = { state, XCB_NONE };
    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE, win,
                        WM_STATE, WM_STATE, 32, 2, data);
}

/** Send request to get a window state (WM_STATE).
 * \param w A client window.
 * \return The cookie associated with the request.
 */
xcb_get_property_cookie_t
xwindow_get_state_unchecked(xcb_window_t w)
{
    return xcb_get_property_unchecked(_G_connection, false, w, WM_STATE,
                                      WM_STATE, 0L, 2L);
}

/** Get a window state (WM_STATE).
 * \param cookie The cookie.
 * \return The current state of the window, or 0 on error.
 */
#include <xcb/xcb_icccm.h>
uint32_t
xwindow_get_state_reply(xcb_get_property_cookie_t cookie)
{
    /* If no property is set, we just assume a sane default. */
    uint32_t result = XCB_WM_STATE_NORMAL;
    xcb_get_property_reply_t *prop_r;

    if((prop_r = xcb_get_property_reply(_G_connection, cookie, NULL)))
    {
        if(xcb_get_property_value_length(prop_r))
            result = *(uint32_t *) xcb_get_property_value(prop_r);

        p_delete(&prop_r);
    }

    return result;
}

/** Configure a window with its new geometry and border size.
 * \param win The X window id to configure.
 * \param geometry The new window geometry.
 * \param border The new border size.
 */
void
xwindow_configure(xcb_window_t win, area_t geometry, int border)
{
    xcb_configure_notify_event_t ce;

    ce.response_type = XCB_CONFIGURE_NOTIFY;
    ce.event = win;
    ce.window = win;
    ce.x = geometry.x;
    ce.y = geometry.y;
    ce.width = geometry.width;
    ce.height = geometry.height;
    ce.border_width = border;
    ce.above_sibling = XCB_NONE;
    ce.override_redirect = false;
    xcb_send_event(_G_connection, false, win, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (char *) &ce);
}

/** Grab key on a window.
 * \param win The window.
 * \param k The key.
 */
static void
xwindow_grabkey(xcb_window_t win, keyb_t *k)
{
    if(k->keycode)
        xcb_grab_key(_G_connection, true, win,
                     k->modifiers, k->keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    else if(k->keysym)
    {
        xcb_keycode_t *keycodes = xcb_key_symbols_get_keycode(globalconf.keysyms, k->keysym);
        if(keycodes)
        {
            for(xcb_keycode_t *kc = keycodes; *kc; kc++)
                xcb_grab_key(_G_connection, true, win,
                             k->modifiers, *kc, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
            p_delete(&keycodes);
        }
    }
}

void
xwindow_grabkeys(xcb_window_t win, key_array_t *keys)
{
    if(win)
    {
        /* Ungrab everything first */
        xcb_ungrab_key(_G_connection, XCB_GRAB_ANY, win, XCB_BUTTON_MASK_ANY);

        foreach(k, *keys)
            xwindow_grabkey(win, *k);
    }
}

/** Send a request for a window's opacity.
 * \param win The window
 * \return A cookie for xwindow_get_opacity_from_reply().
 */
xcb_get_property_cookie_t xwindow_get_opacity_unchecked(xcb_window_t win)
{
    return xcb_get_property_unchecked(_G_connection, false, win,
                                      _NET_WM_WINDOW_OPACITY, XCB_ATOM_CARDINAL, 0L, 1L);
}

/** Get the opacity of a window.
 * \param win The window.
 * \return The opacity, between 0 and 1 or -1 or no opacity set.
 */
double
xwindow_get_opacity(xcb_window_t win)
{
    xcb_get_property_cookie_t prop_c =
        xwindow_get_opacity_unchecked(win);
    return xwindow_get_opacity_from_cookie(prop_c);
}

/** Get the opacity of a window.
 * \param cookie A cookie for a reply to a get property request for _NET_WM_WINDOW_OPACITY.
 * \return The opacity, between 0 and 1.
 */
double
xwindow_get_opacity_from_cookie(xcb_get_property_cookie_t cookie)
{
    xcb_get_property_reply_t *prop_r =
        xcb_get_property_reply(_G_connection, cookie, NULL);

    if(prop_r && prop_r->value_len && prop_r->format == 32)
    {
        uint32_t value = *(uint32_t *) xcb_get_property_value(prop_r);
        p_delete(&prop_r);
        return (double) value / (double) 0xffffffff;
    }
    p_delete(&prop_r);

    return -1;
}

/** Set opacity of a window.
 * \param win The window.
 * \param opacity Opacity of the window, between 0 and 1.
 */
void
xwindow_set_opacity(xcb_window_t win, double opacity)
{
    if(win)
    {
        if(opacity >= 0 && opacity <= 1)
        {
            uint32_t real_opacity = opacity * 0xffffffff;
            xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE, win,
                                _NET_WM_WINDOW_OPACITY, XCB_ATOM_CARDINAL, 32, 1L, &real_opacity);
        }
        else
            xcb_delete_property(_G_connection, win, _NET_WM_WINDOW_OPACITY);
    }
}

/** Send WM_TAKE_FOCUS client message to window
 * \param win destination window
 */
void
xwindow_takefocus(xcb_window_t win)
{
    xcb_client_message_event_t ev;

    /* Initialize all of event's fields first */
    p_clear(&ev, 1);

    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = win;
    ev.format = 32;
    ev.data.data32[1] = globalconf.timestamp;
    ev.type = WM_PROTOCOLS;
    ev.data.data32[0] = WM_TAKE_FOCUS;

    xcb_send_event(_G_connection, false, win,
                   XCB_EVENT_MASK_NO_EVENT, (char *) &ev);
}

/** Set window cursor.
 * \param w The window.
 * \param c The cursor.
 */
void
xwindow_set_cursor(xcb_window_t w, xcb_cursor_t c)
{
    if(w)
        xcb_change_window_attributes(_G_connection, w, XCB_CW_CURSOR,
                                     (const uint32_t[]) { c });
}

/** Set a window border color.
 * \param w The window.
 * \param color The color.
 */
void
xwindow_set_border_color(xcb_window_t w, xcolor_t *color)
{
    if(w)
        xcb_change_window_attributes(_G_connection, w, XCB_CW_BORDER_PIXEL, &color->pixel);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
