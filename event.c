/*
 * event.c - event handlers
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
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
#include <xcb/randr.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_event.h>

#include "event.h"
#include "xwindow.h"
#include "ewmh.h"
#include "keyresolv.h"
#include "systray.h"
#include "spawn.h"
#include "objects/screen.h"
#include "common/xutil.h"

static window_t *
window_getbywin(xcb_window_t window)
{
    if(_G_root->window == window)
        return _G_root;
    return (window_t *) ewindow_getbywin(window);
}

/** Push a modifier set to a Lua table.
 * \param L The Lua VM state.
 * \param modifiers The modifier.
 * \return The number of elements pushed on stack.
 */
static int
luaA_pushmodifiers(lua_State *L, uint16_t modifiers)
{
    lua_createtable(L, 8, 8);
    int i = 1;
    for(uint32_t mask = XCB_MOD_MASK_SHIFT; mask <= XCB_KEY_BUT_MASK_BUTTON_5; mask <<= 1)
    {
        const char *mod;
        size_t slen;
        xutil_key_mask_tostr(mask, &mod, &slen);
        if(mask & modifiers)
        {
            lua_pushlstring(L, mod, slen);
            lua_rawseti(L, -2, i++);
        }

        lua_pushlstring(L, mod, slen);
        lua_pushboolean(L, mask & modifiers);
        lua_rawset(L, -3);

    }
    return 1;
}

/** The button press event handler.
 * \param data The type of mouse event.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_button(void *data, xcb_connection_t *connection, xcb_button_press_event_t *ev)
{
    luaA_pushmodifiers(_G_L, ev->state);
    lua_pushinteger(_G_L, ev->detail);
    switch(ev->response_type)
    {
      case XCB_BUTTON_PRESS:
        window_emit_signal(_G_L, window_getbywin(ev->event), "button::press", 2);
        break;
      case XCB_BUTTON_RELEASE:
        window_emit_signal(_G_L, window_getbywin(ev->event), "button::release", 2);
        break;
      default: /* wtf? */
        lua_pop(_G_L, 2);
        break;
    }

    return 0;
}

static void
event_handle_configurerequest_configure_window(xcb_configure_request_event_t *ev)
{
    uint16_t config_win_mask = 0;
    uint32_t config_win_vals[7];
    unsigned short i = 0;

    if(ev->value_mask & XCB_CONFIG_WINDOW_X)
    {
        config_win_mask |= XCB_CONFIG_WINDOW_X;
        config_win_vals[i++] = ev->x;
    }
    if(ev->value_mask & XCB_CONFIG_WINDOW_Y)
    {
        config_win_mask |= XCB_CONFIG_WINDOW_Y;
        config_win_vals[i++] = ev->y;
    }
    if(ev->value_mask & XCB_CONFIG_WINDOW_WIDTH)
    {
        config_win_mask |= XCB_CONFIG_WINDOW_WIDTH;
        config_win_vals[i++] = ev->width;
    }
    if(ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
    {
        config_win_mask |= XCB_CONFIG_WINDOW_HEIGHT;
        config_win_vals[i++] = ev->height;
    }
    if(ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
    {
        config_win_mask |= XCB_CONFIG_WINDOW_BORDER_WIDTH;
        config_win_vals[i++] = ev->border_width;
    }
    if(ev->value_mask & XCB_CONFIG_WINDOW_SIBLING)
    {
        config_win_mask |= XCB_CONFIG_WINDOW_SIBLING;
        config_win_vals[i++] = ev->sibling;
    }
    if(ev->value_mask & XCB_CONFIG_WINDOW_STACK_MODE)
    {
        config_win_mask |= XCB_CONFIG_WINDOW_STACK_MODE;
        config_win_vals[i++] = ev->stack_mode;
    }

    xcb_configure_window(_G_connection, ev->window, config_win_mask, config_win_vals);
}

/** The configure event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_configurerequest(void *data __attribute__ ((unused)),
                              xcb_connection_t *connection, xcb_configure_request_event_t *ev)
{
    client_t *c;

    if((c = client_getbywin(ev->window)))
    {
        area_t geometry = c->geometry;

        if(ev->value_mask & XCB_CONFIG_WINDOW_X)
            geometry.x = ev->x;
        if(ev->value_mask & XCB_CONFIG_WINDOW_Y)
            geometry.y = ev->y;
        if(ev->value_mask & XCB_CONFIG_WINDOW_WIDTH)
            geometry.width = ev->width;
        if(ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT)
            geometry.height = ev->height;

        if(ev->value_mask & XCB_CONFIG_WINDOW_BORDER_WIDTH)
            ewindow_set_border_width(_G_L, (ewindow_t *) c, ev->border_width);

        if(!window_set_geometry(_G_L, (window_t *) c, geometry))
            xwindow_configure(c->window, geometry, c->border_width);
    }
    else
        event_handle_configurerequest_configure_window(ev);

    return 0;
}

/** The configure notify event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_configurenotify(void *data __attribute__ ((unused)),
                             xcb_connection_t *connection, xcb_configure_notify_event_t *ev)
{
    if(ev->window == _G_root->window)
    {
        bool geometry_has_changed = false;

        if(_G_root->geometry.width != ev->width)
        {
            _G_root->geometry.width = ev->width;
            window_emit_signal(_G_L, _G_root, "property::width", 0);
            geometry_has_changed = true;
        }

        if(_G_root->geometry.height != ev->height)
        {
            _G_root->geometry.height = ev->height;
            window_emit_signal(_G_L, _G_root, "property:height", 0);
            geometry_has_changed = true;
        }

        if(geometry_has_changed)
            window_emit_signal(_G_L, _G_root, "property::geometry", 0);
    }

    return 0;
}

/** The destroy notify event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_destroynotify(void *data __attribute__ ((unused)),
                           xcb_connection_t *connection __attribute__ ((unused)),
                           xcb_destroy_notify_event_t *ev)
{
    client_t *c;

    if((c = client_getbywin(ev->window)))
        client_unmanage(c);
    else
        foreach(em, _G_embedded)
            if(em->window == ev->window)
            {
                xembed_window_array_remove(&_G_embedded, em);
                break;
            }

    return 0;
}

/** The motion notify event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_motionnotify(void *data __attribute__ ((unused)),
                          xcb_connection_t *connection,
                          xcb_motion_notify_event_t *ev)
{
    window_t *window;
    if(ev->child)
        window = window_getbywin(ev->child);
    else
        window = window_getbywin(ev->event);
    luaA_pushmodifiers(_G_L, ev->state);
    lua_pushinteger(_G_L, ev->event_x);
    lua_pushinteger(_G_L, ev->event_y);
    lua_pushinteger(_G_L, ev->root_x);
    lua_pushinteger(_G_L, ev->root_y);
    window_emit_signal(_G_L, window, "mouse::move", 5);
    return 0;
}

/** The enter and leave notify event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_enterleavenotify(void *data __attribute__ ((unused)),
                              xcb_connection_t *connection,
                              xcb_enter_notify_event_t *ev)
{
    if(ev->mode != XCB_NOTIFY_MODE_NORMAL)
        return 0;

    luaA_pushmodifiers(_G_L, ev->state);
    lua_pushinteger(_G_L, ev->event_x);
    lua_pushinteger(_G_L, ev->event_y);
    lua_pushinteger(_G_L, ev->root_x);
    lua_pushinteger(_G_L, ev->root_y);
    switch(ev->response_type)
    {
      case XCB_ENTER_NOTIFY:
        window_emit_signal(_G_L, window_getbywin(ev->event), "mouse::enter", 5);
        break;
      case XCB_LEAVE_NOTIFY:
        window_emit_signal(_G_L, window_getbywin(ev->event), "mouse::leave", 5);
        break;
      default: /* wtf */
        lua_pop(_G_L, 3);
        break;
    }

    return 0;
}

/** The focus in event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_focusin(void *data __attribute__ ((unused)),
                     xcb_connection_t *connection,
                     xcb_focus_in_event_t *ev)
{
    /* Events that we are interested in: */
    switch(ev->detail)
    {
        /* These are events that jump between root windows.
        */
        case XCB_NOTIFY_DETAIL_ANCESTOR:
        case XCB_NOTIFY_DETAIL_INFERIOR:

        /* These are events that jump between clients.
         * Virtual events ensure we always get an event on our top-level window.
         */
        case XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL:
        case XCB_NOTIFY_DETAIL_NONLINEAR:
          {
            window_t *window = window_getbywin(ev->event);

            if(window)
                window_focus_update(window);
          }
        /* all other events are ignored */
        default:
            break;
    }
    return 0;
}

/** The focus out event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_focusout(void *data __attribute__ ((unused)),
                      xcb_connection_t *connection,
                      xcb_focus_in_event_t *ev)
{
    /* Events that we are interested in: */
    switch(ev->detail)
    {
        /* These are events that jump between root windows.
         */
        case XCB_NOTIFY_DETAIL_ANCESTOR:
        case XCB_NOTIFY_DETAIL_INFERIOR:

        /* These are events that jump between clients.
         * Virtual events ensure we always get an event on our top-level window.
         */
        case XCB_NOTIFY_DETAIL_NONLINEAR_VIRTUAL:
        case XCB_NOTIFY_DETAIL_NONLINEAR:
          {
            window_t *window = window_getbywin(ev->event);

            if(window)
                window_unfocus_update(window);
          }
        /* all other events are ignored */
        default:
            break;
    }
    return 0;
}

/** The expose event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_expose(void *data __attribute__ ((unused)),
                    xcb_connection_t *connection __attribute__ ((unused)),
                    xcb_expose_event_t *ev)
{
    wibox_t *wibox;

    /* If the wibox got need_update set, skip this because it will be repainted
     * soon anyway. Without this we could be painting garbage to the screen!
     */
    if((wibox = wibox_getbywin(ev->window)) && !wibox->need_update)
        wibox_refresh_pixmap_partial(wibox,
                                     ev->x, ev->y,
                                     ev->width, ev->height);

    return 0;
}

/** The key press event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_key(void *data __attribute__ ((unused)),
                 xcb_connection_t *connection __attribute__ ((unused)),
                 xcb_key_press_event_t *ev)
{
    /* get keysym ignoring all modifiers */
    xcb_keysym_t keysym = keyresolv_get_keysym(ev->detail, ev->state);

    /* Push modifiers */
    luaA_pushmodifiers(_G_L, ev->state);
    /* Push keycode */
    lua_pushinteger(_G_L, ev->detail);
    /* Push keysym */
    char buf[MAX(MB_LEN_MAX, 64)];
    if(keyresolv_keysym_to_string(keysym, buf, sizeof(buf)))
        lua_pushstring(_G_L, buf);
    else
        lua_pushnil(_G_L);
    lua_pushinteger(_G_L, ev->event_x);
    lua_pushinteger(_G_L, ev->event_y);
    lua_pushinteger(_G_L, ev->root_x);
    lua_pushinteger(_G_L, ev->root_y);

    switch(ev->response_type)
    {
      case XCB_KEY_PRESS:
        window_emit_signal(_G_L, window_getbywin(ev->event), "key::press", 7);
        break;
      case XCB_KEY_RELEASE:
        window_emit_signal(_G_L, window_getbywin(ev->event), "key::release", 7);
        break;
      default: /* wtf? */
        lua_pop(_G_L, 7);
        break;
    }

    return 0;
}

/** The map request event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_maprequest(void *data __attribute__ ((unused)),
                        xcb_connection_t *connection, xcb_map_request_event_t *ev)
{
    int ret = 0;
    client_t *c;
    xcb_get_window_attributes_cookie_t wa_c;
    xcb_get_window_attributes_reply_t *wa_r;
    xcb_get_geometry_cookie_t geom_c;
    xcb_get_geometry_reply_t *geom_r;

    wa_c = xcb_get_window_attributes_unchecked(connection, ev->window);

    if(!(wa_r = xcb_get_window_attributes_reply(connection, wa_c, NULL)))
        return -1;

    if(wa_r->override_redirect)
        goto bailout;

    if(xembed_getbywin(&_G_embedded, ev->window))
    {
        xcb_map_window(connection, ev->window);
        xembed_window_activate(connection, ev->window);
        goto bailout;
    }

    if((c = client_getbywin(ev->window)))
    {
        /* Check that it may be visible, but not asked to be hidden */
        if(ewindow_isvisible((ewindow_t *) c))
        {
            ewindow_set_minimized(_G_L, (ewindow_t *) c, false);
            /* it will be raised, so just update ourself */
            stack_window_raise(_G_L, (window_t *) c);
        }
    }
    else
    {
        if(systray_iskdedockapp(ev->window))
        {
            systray_request_handle(ev->window, NULL);
            goto bailout;
        }

        geom_c = xcb_get_geometry_unchecked(connection, ev->window);

        if(!(geom_r = xcb_get_geometry_reply(connection, geom_c, NULL)))
        {
            ret = -1;
            goto bailout;
        }

        client_manage(ev->window, geom_r, false);

        p_delete(&geom_r);
    }

bailout:
    p_delete(&wa_r);
    return ret;
}

/** The unmap notify event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_unmapnotify(void *data __attribute__ ((unused)),
                         xcb_connection_t *connection, xcb_unmap_notify_event_t *ev)
{
    client_t *c;

    if((c = client_getbywin(ev->window)))
    {
        if(ev->event == _G_root->window
           && XCB_EVENT_SENT(ev)
           && xwindow_get_state_reply(xwindow_get_state_unchecked(c->window)) == XCB_WM_STATE_NORMAL)
            client_unmanage(c);
    }
    else
        foreach(em, _G_embedded)
            if(em->window == ev->window)
            {
                xembed_window_array_remove(&_G_embedded, em);
                break;
            }

    return 0;
}

/** The randr screen change notify event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_randr_screen_change_notify(void *data __attribute__ ((unused)),
                                        xcb_connection_t *connection __attribute__ ((unused)),
                                        xcb_randr_screen_change_notify_event_t *ev)
{
    /* Code  of  XRRUpdateConfiguration Xlib  function  ported to  XCB
     * (only the code relevant  to RRScreenChangeNotify) as the latter
     * doesn't provide this kind of function */
    if(ev->rotation & (XCB_RANDR_ROTATION_ROTATE_90 | XCB_RANDR_ROTATION_ROTATE_270))
        xcb_randr_set_screen_size(connection, ev->root, ev->height, ev->width,
                                  ev->mheight, ev->mwidth);
    else
        xcb_randr_set_screen_size(connection, ev->root, ev->width, ev->height,
                                  ev->mwidth, ev->mheight);

    /* XRRUpdateConfiguration also executes the following instruction
     * but it's not useful because SubpixelOrder is not used at all at
     * the moment
     *
     * XRenderSetSubpixelOrder(dpy, snum, scevent->subpixel_order);
     */

    awesome_restart();

    return 0;
}

/** The client message event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_clientmessage(void *data __attribute__ ((unused)),
                           xcb_connection_t *connection,
                           xcb_client_message_event_t *ev)
{
    /* check for startup notification messages */
    if(sn_xcb_display_process_event(_G_sndisplay, (xcb_generic_event_t *) ev))
        return 0;

    if(ev->type == WM_CHANGE_STATE)
    {
        client_t *c;
        if((c = client_getbywin(ev->window))
           && ev->format == 32
           && ev->data.data32[0] == XCB_WM_STATE_ICONIC)
            ewindow_set_minimized(_G_L, (ewindow_t *) c, true);
    }
    else if(ev->type == _XEMBED)
        return xembed_process_client_message(ev);
    else if(ev->type == _NET_SYSTEM_TRAY_OPCODE)
        return systray_process_client_message(ev);
    return ewmh_process_client_message(ev);
}

/** The keymap change notify event handler.
 * \param data Unused data.
 * \param connection The connection to the X server.
 * \param ev The event.
 * \return Status code, 0 if everything's fine.
 */
static int
event_handle_mappingnotify(void *data,
                           xcb_connection_t *connection,
                           xcb_mapping_notify_event_t *ev)
{
    if(ev->request == XCB_MAPPING_MODIFIER
       || ev->request == XCB_MAPPING_KEYBOARD)
    {
        xcb_get_modifier_mapping_cookie_t xmapping_cookie =
            xcb_get_modifier_mapping_unchecked(_G_connection);

        /* Free and then allocate the key symbols */
        xcb_key_symbols_free(_G_keysyms);
        _G_keysyms = xcb_key_symbols_alloc(_G_connection);
        keyresolv_lock_mask_refresh(_G_connection, xmapping_cookie, _G_keysyms);
    }

    return 0;
}

static int
event_handle_reparentnotify(void *data,
                           xcb_connection_t *connection,
                           xcb_reparent_notify_event_t *ev)
{
    client_t *c;

    if((c = client_getbywin(ev->window)))
        client_unmanage(c);

    return 0;
}

void a_xcb_set_event_handlers(void)
{
    const xcb_query_extension_reply_t *randr_query;

    xcb_event_set_button_press_handler(&_G_evenths, event_handle_button, NULL);
    xcb_event_set_button_release_handler(&_G_evenths, event_handle_button, NULL);
    xcb_event_set_configure_request_handler(&_G_evenths, event_handle_configurerequest, NULL);
    xcb_event_set_configure_notify_handler(&_G_evenths, event_handle_configurenotify, NULL);
    xcb_event_set_destroy_notify_handler(&_G_evenths, event_handle_destroynotify, NULL);
    xcb_event_set_enter_notify_handler(&_G_evenths, event_handle_enterleavenotify, NULL);
    xcb_event_set_leave_notify_handler(&_G_evenths, event_handle_enterleavenotify, NULL);
    xcb_event_set_focus_in_handler(&_G_evenths, event_handle_focusin, NULL);
    xcb_event_set_focus_out_handler(&_G_evenths, event_handle_focusout, NULL);
    xcb_event_set_motion_notify_handler(&_G_evenths, event_handle_motionnotify, NULL);
    xcb_event_set_expose_handler(&_G_evenths, event_handle_expose, NULL);
    xcb_event_set_key_press_handler(&_G_evenths, event_handle_key, NULL);
    xcb_event_set_key_release_handler(&_G_evenths, event_handle_key, NULL);
    xcb_event_set_map_request_handler(&_G_evenths, event_handle_maprequest, NULL);
    xcb_event_set_unmap_notify_handler(&_G_evenths, event_handle_unmapnotify, NULL);
    xcb_event_set_client_message_handler(&_G_evenths, event_handle_clientmessage, NULL);
    xcb_event_set_mapping_notify_handler(&_G_evenths, event_handle_mappingnotify, NULL);
    xcb_event_set_reparent_notify_handler(&_G_evenths, event_handle_reparentnotify, NULL);

    /* check for randr extension */
    randr_query = xcb_get_extension_data(_G_connection, &xcb_randr_id);
    if(randr_query->present)
        xcb_event_set_handler(&_G_evenths,
                              randr_query->first_event + XCB_RANDR_SCREEN_CHANGE_NOTIFY,
                              (xcb_generic_event_handler_t) event_handle_randr_screen_change_notify,
                              NULL);

}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
