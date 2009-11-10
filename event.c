/*
 * event.c - event handlers
 *
 * Copyright © 2007-2008 Julien Danjou <julien@danjou.info>
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

#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_event.h>

#include "awesome.h"
#include "event.h"
#include "property.h"
#include "objects/tag.h"
#include "xwindow.h"
#include "ewmh.h"
#include "objects/client.h"
#include "objects/widget.h"
#include "keyresolv.h"
#include "keygrabber.h"
#include "mousegrabber.h"
#include "luaa.h"
#include "systray.h"
#include "screen.h"
#include "spawn.h"
#include "common/atoms.h"
#include "common/xutil.h"

#define DO_EVENT_HOOK_CALLBACK(type, xcbtype, xcbeventprefix, arraytype, match) \
    static void \
    event_##xcbtype##_callback(xcb_##xcbtype##_press_event_t *ev, \
                               arraytype *arr, \
                               int nargs, \
                               void *data) \
    { \
        int item_matching = 0; \
        foreach(item, *arr) \
            if(match(ev, *item, data)) \
            { \
                luaA_object_push(globalconf.L, *item); \
                item_matching++; \
            } \
        for(; item_matching > 0; item_matching--) \
        { \
            switch(ev->response_type) \
            { \
              case xcbeventprefix##_PRESS: \
                for(int i = 0; i < nargs; i++) \
                    lua_pushvalue(globalconf.L, - nargs - item_matching); \
                luaA_object_emit_signal(globalconf.L, - nargs - 1, "press", nargs); \
                break; \
              case xcbeventprefix##_RELEASE: \
                for(int i = 0; i < nargs; i++) \
                    lua_pushvalue(globalconf.L, - nargs - item_matching); \
                luaA_object_emit_signal(globalconf.L, - nargs - 1, "release", nargs); \
                break; \
            } \
            lua_pop(globalconf.L, 1); \
        } \
        lua_pop(globalconf.L, nargs); \
    }

static bool
event_key_match(xcb_key_press_event_t *ev, keyb_t *k, void *data)
{
    assert(data);
    xcb_keysym_t keysym = *(xcb_keysym_t *) data;
    return (((k->keycode && ev->detail == k->keycode)
             || (k->keysym && keysym == k->keysym))
            && (k->modifiers == XCB_BUTTON_MASK_ANY || k->modifiers == ev->state));
}

static bool
event_button_match(xcb_button_press_event_t *ev, button_t *b, void *data)
{
    return ((!b->button || ev->detail == b->button)
            && (b->modifiers == XCB_BUTTON_MASK_ANY || b->modifiers == ev->state));
}

DO_EVENT_HOOK_CALLBACK(button_t, button, XCB_BUTTON, button_array_t, event_button_match)
DO_EVENT_HOOK_CALLBACK(keyb_t, key, XCB_KEY, key_array_t, event_key_match)

/** Handle an event with mouse grabber if needed
 * \param x The x coordinate.
 * \param y The y coordinate.
 * \param mask The mask buttons.
 * \return True if the event was handled.
 */
static bool
event_handle_mousegrabber(int x, int y, uint16_t mask)
{
    if(_G_mousegrabber)
    {
        luaA_object_push(globalconf.L, _G_mousegrabber);
        mousegrabber_handleevent(globalconf.L, x, y, mask);
        if(lua_pcall(globalconf.L, 1, 1, 0))
        {
            warn("error running function: %s", lua_tostring(globalconf.L, -1));
            luaA_mousegrabber_stop(globalconf.L);
        }
        else if(!lua_isboolean(globalconf.L, -1) || !lua_toboolean(globalconf.L, -1))
            luaA_mousegrabber_stop(globalconf.L);
        lua_pop(globalconf.L, 1);  /* pop returned value */
        return true;
    }
    return false;
}

/** The button press event handler.
 * \param ev The event.
 */
static void
event_handle_button(xcb_button_press_event_t *ev)
{
    client_t *c;
    wibox_t *wibox;

    globalconf.timestamp = ev->time;

    if(event_handle_mousegrabber(ev->root_x, ev->root_y, 1 << (ev->detail - 1 + 8)))
        return;

    /* ev->state is
     * button status (8 bits) + modifiers status (8 bits)
     * we don't care for button status that we get, especially on release, so
     * drop them */
    ev->state &= 0x00ff;

    if((wibox = wibox_getbywin(ev->event))
       || (wibox = wibox_getbywin(ev->child)))
    {
        /* If the wibox is child, then x,y are
         * relative to root window */
        if(wibox->window == ev->child)
        {
            ev->event_x -= wibox->geometry.x;
            ev->event_y -= wibox->geometry.y;
        }

        /* Push the wibox */
        luaA_object_push(globalconf.L, wibox);
        /* Duplicate the wibox */
        lua_pushvalue(globalconf.L, -1);
        /* Handle the button event on it */
        event_button_callback(ev, &wibox->buttons, 1, NULL);

        /* then try to match a widget binding */
        widget_t *w = widget_getbycoords(&wibox->widgets,
                                         wibox->geometry.width,
                                         wibox->geometry.height,
                                         &ev->event_x, &ev->event_y);
        if(w)
        {
            /* Push widget (the wibox is already on stack) */
            luaA_object_push(globalconf.L, w);
            /* Move widget before wibox */
            lua_insert(globalconf.L, -2);
            event_button_callback(ev, &w->buttons, 2, NULL);
        }
        else
            /* Remove the wibox, we did not use it */
            lua_pop(globalconf.L, 1);

    }
    else if((c = client_getbyframewin(ev->event)))
    {
        luaA_object_push(globalconf.L, c);
        event_button_callback(ev, &c->buttons, 1, NULL);
        xcb_allow_events(_G_connection,
                         XCB_ALLOW_REPLAY_POINTER,
                         XCB_CURRENT_TIME);
    }
    else if(ev->child == XCB_NONE)
        if(globalconf.screen->root == ev->event)
        {
            luaA_object_push(globalconf.L, globalconf.screens.tab[0].root);
            event_button_callback(ev, &globalconf.screens.tab[0].root->buttons, 1, NULL);
            return;
        }
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
 * \param ev The event.
 */
static void
event_handle_configurerequest(xcb_configure_request_event_t *ev)
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
        {
            luaA_object_push(globalconf.L, c);
            ewindow_set_border_width(globalconf.L, -1, ev->border_width);
            lua_pop(globalconf.L, 1);
        }

        if(!client_resize(c, geometry, false))
            /* ICCCM 4.1.5 / 4.2.3, if nothing was changed, send an event saying so */
            xwindow_configure(c->window, geometry, c->border_width);
    }
    else
        event_handle_configurerequest_configure_window(ev);
}

/** The configure notify event handler.
 * \param ev The event.
 */
static void
event_handle_configurenotify(xcb_configure_notify_event_t *ev)
{
    const xcb_screen_t *screen = globalconf.screen;

    if(ev->window == screen->root
       && (ev->width != screen->width_in_pixels
           || ev->height != screen->height_in_pixels))
        /* it's not that we panic, but restart */
        awesome_restart();
}

/** The destroy notify event handler.
 * \param ev The event.
 */
static void
event_handle_destroynotify(xcb_destroy_notify_event_t *ev)
{
    client_t *c;

    if((c = client_getbywin(ev->window)))
        client_unmanage(c);
    else
        for(int i = 0; i < globalconf.embedded.len; i++)
            if(globalconf.embedded.tab[i].win == ev->window)
            {
                xembed_window_array_take(&globalconf.embedded, i);
                widget_invalidate_bytype(widget_systray);
            }
}

/** Handle a motion notify over widgets.
 * \param wibox The wibox.
 * \param mouse_over The pointer to the registered mouse over widget.
 * \param widget The new widget the mouse is over.
 */
static void
event_handle_widget_motionnotify(wibox_t *wibox,
                                 widget_t *widget)
{
    if(widget != wibox->mouse_over)
    {
        if(wibox->mouse_over)
        {
            /* Emit mouse leave signal on old widget:
             * - Push wibox.*/
            luaA_object_push(globalconf.L, wibox);
            /* - Push the widget the mouse was on */
            luaA_object_push(globalconf.L, wibox->mouse_over);
            /* - Invert wibox/widget */
            lua_insert(globalconf.L, -2);
            /* - Emit the signal mouse::leave with the wibox as argument */
            luaA_object_emit_signal(globalconf.L, -2, "mouse::leave", 1);
            /* - Remove the widget */
            lua_pop(globalconf.L, 1);

            /* Re-push wibox */
            luaA_object_push(globalconf.L, wibox);
            luaA_object_unref_item(globalconf.L, -1, wibox->mouse_over);
            /* Remove the wibox */
            lua_pop(globalconf.L, 1);
        }
        if(widget)
        {
            /* Get a ref on this widget so that it can't be unref'd from
             * underneath us (-> invalid pointer dereference):
             * - Push the wibox */
            luaA_object_push(globalconf.L, wibox);
            /* - Push the widget */
            luaA_object_push(globalconf.L, widget);
            /* - Reference the widget into the wibox */
            luaA_object_ref_item(globalconf.L, -2, -1);

            /* Emit mouse::enter signal on new widget:
             * - Push the widget */
            luaA_object_push(globalconf.L, widget);
            /* - Move the widget before the wibox we pushed just above */
            lua_insert(globalconf.L, -2);
            /* - Emit the signal with the wibox as argument */

            luaA_object_emit_signal(globalconf.L, -2, "mouse::enter", 1);
            /* - Remove the widget from the stack */
            lua_pop(globalconf.L, 1);
        }

        /* Store that this widget was the one with the mouse over */
        wibox->mouse_over = widget;

    }
}

/** The motion notify event handler.
 * \param ev The event.
 */
static void
event_handle_motionnotify(xcb_motion_notify_event_t *ev)
{
    wibox_t *wibox;

    globalconf.timestamp = ev->time;

    if(event_handle_mousegrabber(ev->root_x, ev->root_y, ev->state))
        return;

    if((wibox = wibox_getbywin(ev->event)))
    {
        widget_t *w = widget_getbycoords(&wibox->widgets,
                                         wibox->geometry.width,
                                         wibox->geometry.height,
                                         &ev->event_x, &ev->event_y);

        event_handle_widget_motionnotify(wibox, w);
    }
}

/** The leave notify event handler.
 * \param ev The event.
 */
static void
event_handle_leavenotify(xcb_leave_notify_event_t *ev)
{
    wibox_t *wibox;
    client_t *c;

    globalconf.timestamp = ev->time;

    if(ev->mode != XCB_NOTIFY_MODE_NORMAL)
        return;

    if((c = client_getbyframewin(ev->event)))
    {
        luaA_object_push(globalconf.L, c);
        luaA_object_emit_signal(globalconf.L, -1, "mouse::leave", 0);
        lua_pop(globalconf.L, 1);
    }

    if((wibox = wibox_getbywin(ev->event)))
    {
        if(wibox->mouse_over)
        {
            /* Push the wibox */
            luaA_object_push(globalconf.L, wibox);
            /* Push the widget the mouse is over in this wibox */
            luaA_object_push(globalconf.L, wibox->mouse_over);
            /* Move the widget before the wibox */
            lua_insert(globalconf.L, -2);
            /* Emit mouse::leave signal on widget the mouse was over with
             * its wibox as argument */
            luaA_object_emit_signal(globalconf.L, -2, "mouse::leave", 1);
            /* Remove the widget from the stack */
            lua_pop(globalconf.L, 1);
            wibox_clear_mouse_over(wibox);
        }

        luaA_object_push(globalconf.L, wibox);
        luaA_object_emit_signal(globalconf.L, -1, "mouse::leave", 0);
        lua_pop(globalconf.L, 1);
    }
}

/** The enter notify event handler.
 * \param ev The event.
 */
static void
event_handle_enternotify(xcb_enter_notify_event_t *ev)
{
    client_t *c;
    wibox_t *wibox;

    globalconf.timestamp = ev->time;

    if(ev->mode != XCB_NOTIFY_MODE_NORMAL)
        return;

    if((wibox = wibox_getbywin(ev->event)))
    {
        widget_t *w = widget_getbycoords(&wibox->widgets,
                                         wibox->geometry.width,
                                         wibox->geometry.height,
                                         &ev->event_x, &ev->event_y);

        event_handle_widget_motionnotify(wibox, w);

        luaA_object_push(globalconf.L, wibox);
        luaA_object_emit_signal(globalconf.L, -1, "mouse::enter", 0);
        lua_pop(globalconf.L, 1);
    }

    if((c = client_getbyframewin(ev->event)))
    {
        luaA_object_push(globalconf.L, c);
        luaA_object_emit_signal(globalconf.L, -1, "mouse::enter", 0);
        lua_pop(globalconf.L, 1);
    }
}

/** The focus in event handler.
 * \param ev The event.
 */
static void
event_handle_focusin(xcb_focus_in_event_t *ev)
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
            client_t *c;

            if((c = client_getbywin(ev->event)))
                window_focus_update((window_t *) c);
          }
        /* all other events are ignored */
        default:
            break;
    }
}

/** The focus out event handler.
 * \param data currently unused.
 * \param connection The connection to the X server.
 * \param ev The event.
 */
static int
event_handle_focusout(xcb_focus_in_event_t *ev)
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
            client_t *c;

            if((c = client_getbywin(ev->event)))
                window_unfocus_update((window_t *) c);
          }
        /* all other events are ignored */
        default:
            break;
    }
    return 0;
}

/** The expose event handler.
 * \param ev The event.
 */
static void
event_handle_expose(xcb_expose_event_t *ev)
{
    wibox_t *wibox;

    /* If the wibox got need_update set, skip this because it will be repainted
     * soon anyway. Without this we could be painting garbage to the screen!
     */
    if((wibox = wibox_getbywin(ev->window)) && !wibox->need_update)
        wibox_refresh_pixmap_partial(wibox,
                                     ev->x, ev->y,
                                     ev->width, ev->height);
}

/** The key press event handler.
 * \param ev The event.
 */
static void
event_handle_key(xcb_key_press_event_t *ev)
{
    globalconf.timestamp = ev->time;

    if(_G_keygrabber)
    {
        luaA_object_push(globalconf.L, _G_keygrabber);
        if(keygrabber_handlekpress(globalconf.L, ev))
        {
            if(lua_pcall(globalconf.L, 3, 1, 0))
            {
                warn("error running function: %s", lua_tostring(globalconf.L, -1));
                luaA_keygrabber_stop(globalconf.L);
            }
            else if(!lua_isboolean(globalconf.L, -1) || !lua_toboolean(globalconf.L, -1))
                luaA_keygrabber_stop(globalconf.L);
        }
        lua_pop(globalconf.L, 1);  /* pop returned value or function if not called */
    }
    else
    {
        /* get keysym ignoring all modifiers */
        xcb_keysym_t keysym = keyresolv_get_keysym(ev->detail, 0);
        window_t *window = (window_t *) client_getbywin(ev->event);
        if(!window)
            window = (window_t *) wibox_getbywin(ev->event);
        if(window)
        {
            /* first emit key bindings */
            luaA_object_push(globalconf.L, window);
            event_key_callback(ev, &window->keys, 1, &keysym);
            /* transfer event (keycode + modifiers) to keysym and then convert
             * keysym to string */
            char buf[MAX(MB_LEN_MAX, 32)];
            if(keyresolv_keysym_to_string(keyresolv_get_keysym(ev->detail, ev->state), buf, countof(buf)))
            {
                luaA_object_push(globalconf.L, window);
                luaA_pushmodifiers(globalconf.L, ev->state);
                lua_pushstring(globalconf.L, buf);
                switch(ev->response_type)
                {
                  case XCB_KEY_PRESS:
                    luaA_object_emit_signal(globalconf.L, -3, "key::press", 2);
                    break;
                  case XCB_KEY_RELEASE:
                    luaA_object_emit_signal(globalconf.L, -3, "key::release", 2);
                    break;
                }
                lua_pop(globalconf.L, 1);
            }
        }
        else
            if(globalconf.screen->root == ev->event)
            {
                luaA_object_push(globalconf.L, globalconf.screens.tab[0].root);
                event_key_callback(ev, &globalconf.screens.tab[0].root->keys, 1, &keysym);
                return;
            }
    }
}

/** The map request event handler.
 * \param ev The event.
 */
static void
event_handle_maprequest(xcb_map_request_event_t *ev)
{
    client_t *c;
    xcb_get_window_attributes_cookie_t wa_c;
    xcb_get_window_attributes_reply_t *wa_r;
    xcb_get_geometry_cookie_t geom_c;
    xcb_get_geometry_reply_t *geom_r;

    wa_c = xcb_get_window_attributes_unchecked(_G_connection, ev->window);

    if(!(wa_r = xcb_get_window_attributes_reply(_G_connection, wa_c, NULL)))
        return;

    if(wa_r->override_redirect)
        goto bailout;

    if(xembed_getbywin(&globalconf.embedded, ev->window))
    {
        xcb_map_window(_G_connection, ev->window);
        xembed_window_activate(_G_connection, ev->window);
    }
    else if((c = client_getbywin(ev->window)))
    {
        /* Check that it may be visible, but not asked to be hidden */
        if(client_maybevisible(c) && !c->hidden)
        {
            luaA_object_push(globalconf.L, c);
            ewindow_set_minimized(globalconf.L, -1, false);
            /* it will be raised, so just update ourself */
            stack_ewindow_raise(globalconf.L, -1);
            lua_pop(globalconf.L, 1);
        }
    }
    else
    {
        geom_c = xcb_get_geometry_unchecked(_G_connection, ev->window);

        if(!(geom_r = xcb_get_geometry_reply(_G_connection, geom_c, NULL)))
        {
            goto bailout;
        }

        client_manage(ev->window, geom_r, false);

        p_delete(&geom_r);
    }

bailout:
    p_delete(&wa_r);
}

/** The unmap notify event handler.
 * \param ev The event.
 */
static void
event_handle_unmapnotify(xcb_unmap_notify_event_t *ev)
{
    client_t *c;

    if((c = client_getbywin(ev->window)))
    {
        client_unmanage(c);
    }
    else
        for(int i = 0; i < globalconf.embedded.len; i++)
            if(globalconf.embedded.tab[i].win == ev->window)
            {
                xembed_window_array_take(&globalconf.embedded, i);
                widget_invalidate_bytype(widget_systray);
                xcb_change_save_set(_G_connection, XCB_SET_MODE_DELETE, ev->window);
            }
}

/** The randr screen change notify event handler.
 * \param ev The event.
 */
static void
event_handle_randr_screen_change_notify(xcb_randr_screen_change_notify_event_t *ev)
{
    /* Code  of  XRRUpdateConfiguration Xlib  function  ported to  XCB
     * (only the code relevant  to RRScreenChangeNotify) as the latter
     * doesn't provide this kind of function */
    if(ev->rotation & (XCB_RANDR_ROTATION_ROTATE_90 | XCB_RANDR_ROTATION_ROTATE_270))
        xcb_randr_set_screen_size(_G_connection, ev->root, ev->height, ev->width,
                                  ev->mheight, ev->mwidth);
    else
        xcb_randr_set_screen_size(_G_connection, ev->root, ev->width, ev->height,
                                  ev->mwidth, ev->mheight);

    /* XRRUpdateConfiguration also executes the following instruction
     * but it's not useful because SubpixelOrder is not used at all at
     * the moment
     *
     * XRenderSetSubpixelOrder(dpy, snum, scevent->subpixel_order);
     */

    awesome_restart();
}

/** The client message event handler.
 * \param ev The event.
 */
static void
event_handle_clientmessage(xcb_client_message_event_t *ev)
{
    /* check for startup notification messages */
    if(sn_xcb_display_process_event(_G_sndisplay, (xcb_generic_event_t *) ev))
        return;

    if(ev->type == WM_CHANGE_STATE)
    {
        client_t *c;
        if((c = client_getbywin(ev->window))
           && ev->format == 32
           && ev->data.data32[0] == XCB_WM_STATE_ICONIC)
        {
            luaA_object_push(globalconf.L, c);
            ewindow_set_minimized(globalconf.L, -1, true);
            lua_pop(globalconf.L, 1);
        }
    }
    else if(ev->type == _XEMBED)
        xembed_process_client_message(ev);
    else if(ev->type == _NET_SYSTEM_TRAY_OPCODE)
        systray_process_client_message(ev);
    else
        ewmh_process_client_message(ev);
}

/** The keymap change notify event handler.
 * \param ev The event.
 */
static void
event_handle_mappingnotify(xcb_mapping_notify_event_t *ev)
{
    if(ev->request == XCB_MAPPING_MODIFIER
       || ev->request == XCB_MAPPING_KEYBOARD)
    {
        xcb_get_modifier_mapping_cookie_t xmapping_cookie =
            xcb_get_modifier_mapping_unchecked(_G_connection);

        /* Free and then allocate the key symbols */
        xcb_key_symbols_free(globalconf.keysyms);
        globalconf.keysyms = xcb_key_symbols_alloc(_G_connection);

        keyresolv_lock_mask_refresh(_G_connection, xmapping_cookie, globalconf.keysyms);

        /* regrab everything */
        xcb_screen_t *s = globalconf.screen;
        xwindow_grabkeys(s->root, &globalconf.keys);

        foreach(c, globalconf.clients)
            xwindow_grabkeys((*c)->window, &(*c)->keys);
    }
}

static void
event_handle_reparentnotify(xcb_reparent_notify_event_t *ev)
{
    client_t *c;

    if((c = client_getbywin(ev->window)) && c->frame_window != ev->parent)
    {
        /* Ignore reparents to the root window, they *might* be caused by
         * ourselves if a client quickly unmaps and maps itself again. */
        if (ev->parent != globalconf.screen->root)
            client_unmanage(c);
    }
}

/** \brief awesome xerror function.
 * There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's).
 * \param e The error event.
 */
static void
xerror(xcb_generic_error_t *e)
{
    /* ignore this */
    if(e->error_code == XCB_EVENT_ERROR_BAD_WINDOW
       || (e->error_code == XCB_EVENT_ERROR_BAD_MATCH
           && e->major_code == XCB_SET_INPUT_FOCUS)
       || (e->error_code == XCB_EVENT_ERROR_BAD_VALUE
           && e->major_code == XCB_KILL_CLIENT)
       || (e->major_code == XCB_CONFIGURE_WINDOW
           && e->error_code == XCB_EVENT_ERROR_BAD_MATCH))
        return;

    warn("X error: request=%s, error=%s",
         xcb_event_get_request_label(e->major_code),
         xcb_event_get_error_label(e->error_code));

    return;
}

void event_handle(xcb_generic_event_t *event)
{
    uint8_t response_type = XCB_EVENT_RESPONSE_TYPE(event);

    if(response_type == 0)
    {
        /* This is an error, not a event */
        xerror((xcb_generic_error_t *) event);
        return;
    }

    switch(response_type)
    {
#define EVENT(type, callback) case type: callback((void *) event); return
        EVENT(XCB_BUTTON_PRESS, event_handle_button);
        EVENT(XCB_BUTTON_RELEASE, event_handle_button);
        EVENT(XCB_CONFIGURE_REQUEST, event_handle_configurerequest);
        EVENT(XCB_CONFIGURE_NOTIFY, event_handle_configurenotify);
        EVENT(XCB_DESTROY_NOTIFY, event_handle_destroynotify);
        EVENT(XCB_ENTER_NOTIFY, event_handle_enternotify);
        EVENT(XCB_CLIENT_MESSAGE, event_handle_clientmessage);
        EVENT(XCB_EXPOSE, event_handle_expose);
        EVENT(XCB_FOCUS_IN, event_handle_focusin);
        EVENT(XCB_FOCUS_OUT, event_handle_focusout);
        EVENT(XCB_KEY_PRESS, event_handle_key);
        EVENT(XCB_KEY_RELEASE, event_handle_key);
        EVENT(XCB_LEAVE_NOTIFY, event_handle_leavenotify);
        EVENT(XCB_MAPPING_NOTIFY, event_handle_mappingnotify);
        EVENT(XCB_MAP_REQUEST, event_handle_maprequest);
        EVENT(XCB_MOTION_NOTIFY, event_handle_motionnotify);
        EVENT(XCB_PROPERTY_NOTIFY, property_handle_propertynotify);
        EVENT(XCB_REPARENT_NOTIFY, event_handle_reparentnotify);
        EVENT(XCB_UNMAP_NOTIFY, event_handle_unmapnotify);
#undef EVENT
    }

    static uint8_t randr_screen_change_notify = 0;

    if(randr_screen_change_notify == 0)
    {
        /* check for randr extension */
        const xcb_query_extension_reply_t *randr_query;
        randr_query = xcb_get_extension_data(_G_connection, &xcb_randr_id);
        if(randr_query->present)
            randr_screen_change_notify = randr_query->first_event + XCB_RANDR_SCREEN_CHANGE_NOTIFY;
    }

    if (response_type == randr_screen_change_notify)
        event_handle_randr_screen_change_notify((void *) event);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
