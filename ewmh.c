/*
 * ewmh.c - EWMH support functions
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

#include <sys/types.h>
#include <unistd.h>

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>

#include "awesome.h"
#include "ewmh.h"
#include "objects/tag.h"
#include "objects/client.h"
#include "objects/wibox.h"
#include "luaa.h"
#include "common/atoms.h"
#include "common/buffer.h"
#include "common/xutil.h"

#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD 1
#define _NET_WM_STATE_TOGGLE 2

/** Update client EWMH hints.
 * \param c The client.
 */
static int
ewmh_client_update_hints(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);
    xcb_atom_t state[10]; /* number of defined state atoms */
    int i = 0;

    if(c->modal)
        state[i++] = _NET_WM_STATE_MODAL;
    if(c->fullscreen)
        state[i++] = _NET_WM_STATE_FULLSCREEN;
    if(c->maximized_vertical)
        state[i++] = _NET_WM_STATE_MAXIMIZED_VERT;
    if(c->maximized_horizontal)
        state[i++] = _NET_WM_STATE_MAXIMIZED_HORZ;
    if(c->sticky)
        state[i++] = _NET_WM_STATE_STICKY;
    if(c->skip_taskbar)
        state[i++] = _NET_WM_STATE_SKIP_TASKBAR;
    if(c->above)
        state[i++] = _NET_WM_STATE_ABOVE;
    if(c->below)
        state[i++] = _NET_WM_STATE_BELOW;
    if(c->minimized)
        state[i++] = _NET_WM_STATE_HIDDEN;
    if(c->urgent)
        state[i++] = _NET_WM_STATE_DEMANDS_ATTENTION;

    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        c->window, _NET_WM_STATE, ATOM, 32, i, state);

    return 0;
}

/** Update the desktop geometry.
 */
static void
ewmh_update_desktop_geometry(void)
{
    area_t geom = screen_area_get(globalconf.screens.tab, false);
    uint32_t sizes[] = { geom.width, geom.height };

    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        _G_root->window,
                        _NET_DESKTOP_GEOMETRY, CARDINAL, 32, countof(sizes), sizes);
}

static int
ewmh_update_net_active_window(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);
    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        _G_root->window,
			_NET_ACTIVE_WINDOW, WINDOW, 32, 1, (xcb_window_t[]) { c->window });
    return 0;
}

static int
ewmh_reset_net_active_window(lua_State *L)
{
    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        _G_root->window,
			_NET_ACTIVE_WINDOW, WINDOW, 32, 1, (xcb_window_t[]) { XCB_NONE });
    return 0;
}

static int
ewmh_update_net_client_list(lua_State *L)
{
    xcb_window_t *wins = p_alloca(xcb_window_t, globalconf.clients.len);

    int n = 0;
    foreach(client, globalconf.clients)
        wins[n++] = (*client)->window;

    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        _G_root->window,
			_NET_CLIENT_LIST, WINDOW, 32, n, wins);

    return 0;
}

static int
ewmh_update_net_current_desktop(lua_State *L)
{
    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        _G_root->window,
                        _NET_CURRENT_DESKTOP, CARDINAL, 32, 1,
                        (uint32_t[]) { tags_get_first_selected_index() });
    return 0;
}

/** Update the client active desktop.
 * This is "wrong" since it can be on several tags, but EWMH has a strict view
 * of desktop system so just take the first tag.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
ewmh_client_update_desktop(lua_State *L)
{
    client_t *c = luaA_checkudata(L, 1, (lua_class_t *) &client_class);

    for(int i = 0; i < _G_tags.len; i++)
        if(ewindow_is_tagged((ewindow_t *) c, _G_tags.tab[i]))
        {
            xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                                c->window, _NET_WM_DESKTOP, CARDINAL, 32, 1, &i);
            break;
        }
    return 0;
}

static int
ewmh_client_reset_urgent(lua_State *L)
{
    /* EWMH indicates that the WM must reset urgent when client gets attention,
     * i.e. focus. */
    client_set_urgent(L, 1, false);
    return 0;
}

/** Update the client struts.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
ewmh_update_strut(lua_State *L)
{
    ewindow_t *ewindow = luaA_checkudata(L, 1, (lua_class_t *) &ewindow_class);

    if(ewindow->window)
    {
        const uint32_t state[] =
        {
            ewindow->strut.left,
            ewindow->strut.right,
            ewindow->strut.top,
            ewindow->strut.bottom,
            ewindow->strut.left_start_y,
            ewindow->strut.left_end_y,
            ewindow->strut.right_start_y,
            ewindow->strut.right_end_y,
            ewindow->strut.top_start_x,
            ewindow->strut.top_end_x,
            ewindow->strut.bottom_start_x,
            ewindow->strut.bottom_end_x
        };

        xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                            ewindow->window, _NET_WM_STRUT_PARTIAL,
                            CARDINAL, 32, countof(state), state);
    }

    return 0;
}

static int
ewmh_update_net_desktop_names(lua_State *L)
{
    buffer_t buf;

    buffer_inita(&buf, BUFSIZ);

    foreach(tag, _G_tags)
    {
        buffer_adds(&buf, tag_get_name(*tag));
        buffer_addc(&buf, '\0');
    }

    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        _G_root->window,
			_NET_DESKTOP_NAMES, UTF8_STRING, 8, buf.len, buf.s);
    buffer_wipe(&buf);

    return 0;
}

static int
ewmh_update_net_numbers_of_desktop(lua_State *L)
{
    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        _G_root->window,
			_NET_NUMBER_OF_DESKTOPS, CARDINAL, 32, 1, &_G_tags.len);

    return 0;
}

/** Update the work area space for each physical screen and each desktop.
 */
static int
ewmh_update_workarea(lua_State *L)
{
    uint32_t *area = p_alloca(uint32_t, _G_tags.len * 4);
    area_t geom = screen_area_get(globalconf.screens.tab, true);

    for(int i = 0; i < _G_tags.len; i++)
    {
        area[4 * i + 0] = geom.x;
        area[4 * i + 1] = geom.y;
        area[4 * i + 2] = geom.width;
        area[4 * i + 3] = geom.height;
    }

    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        _G_root->window,
                        _NET_WORKAREA, CARDINAL, 32, _G_tags.len * 4, area);

    return 0;
}

void
ewmh_init(void)
{
    xcb_window_t father;
    xcb_screen_t *xscreen = xutil_screen_get(_G_connection, _G_default_screen);
    xcb_atom_t atom[] =
    {
        _NET_SUPPORTED,
        _NET_SUPPORTING_WM_CHECK,
        _NET_STARTUP_ID,
        _NET_CLIENT_LIST,
        _NET_NUMBER_OF_DESKTOPS,
        _NET_CURRENT_DESKTOP,
        _NET_DESKTOP_NAMES,
        _NET_ACTIVE_WINDOW,
        _NET_WORKAREA,
        _NET_DESKTOP_GEOMETRY,
        _NET_CLOSE_WINDOW,
        _NET_WM_NAME,
        _NET_WM_STRUT_PARTIAL,
        _NET_WM_ICON_NAME,
        _NET_WM_VISIBLE_ICON_NAME,
        _NET_WM_DESKTOP,
        _NET_WM_WINDOW_TYPE,
        _NET_WM_WINDOW_TYPE_DESKTOP,
        _NET_WM_WINDOW_TYPE_DOCK,
        _NET_WM_WINDOW_TYPE_TOOLBAR,
        _NET_WM_WINDOW_TYPE_MENU,
        _NET_WM_WINDOW_TYPE_UTILITY,
        _NET_WM_WINDOW_TYPE_SPLASH,
        _NET_WM_WINDOW_TYPE_DIALOG,
        _NET_WM_WINDOW_TYPE_DROPDOWN_MENU,
        _NET_WM_WINDOW_TYPE_POPUP_MENU,
        _NET_WM_WINDOW_TYPE_TOOLTIP,
        _NET_WM_WINDOW_TYPE_NOTIFICATION,
        _NET_WM_WINDOW_TYPE_COMBO,
        _NET_WM_WINDOW_TYPE_DND,
        _NET_WM_WINDOW_TYPE_NORMAL,
        _NET_WM_ICON,
        _NET_WM_PID,
        _NET_WM_STATE,
        _NET_WM_STATE_STICKY,
        _NET_WM_STATE_SKIP_TASKBAR,
        _NET_WM_STATE_FULLSCREEN,
        _NET_WM_STATE_MAXIMIZED_HORZ,
        _NET_WM_STATE_MAXIMIZED_VERT,
        _NET_WM_STATE_ABOVE,
        _NET_WM_STATE_BELOW,
        _NET_WM_STATE_MODAL,
        _NET_WM_STATE_HIDDEN,
        _NET_WM_STATE_DEMANDS_ATTENTION
    };

    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        xscreen->root, _NET_SUPPORTED, ATOM, 32,
                        countof(atom), atom);

    /* create our own window */
    father = xcb_generate_id(_G_connection);
    xcb_create_window(_G_connection, xscreen->root_depth,
                      father, xscreen->root, -1, -1, 1, 1, 0,
                      XCB_COPY_FROM_PARENT, xscreen->root_visual, 0, NULL);

    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        xscreen->root, _NET_SUPPORTING_WM_CHECK, WINDOW, 32,
                        1, &father);

    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        father, _NET_SUPPORTING_WM_CHECK, WINDOW, 32,
                        1, &father);

    /* set the window manager name */
    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        father, _NET_WM_NAME, UTF8_STRING, 8, 7, "awesome");

    /* set the window manager PID */
    int i = getpid();
    xcb_change_property(_G_connection, XCB_PROP_MODE_REPLACE,
                        father, _NET_WM_PID, CARDINAL, 32, 1, &i);

    ewmh_update_desktop_geometry();

    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "focus", ewmh_update_net_active_window);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "unfocus", ewmh_reset_net_active_window);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "manage", ewmh_update_net_client_list);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "unmanage", ewmh_update_net_client_list);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::modal" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::fullscreen" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::maximized_horizontal" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::maximized_vertical" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::sticky" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::skip_taskbar" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::above" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::below" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::minimized" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "property::urgent" , ewmh_client_update_hints);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "tagged", ewmh_client_update_desktop);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "untagged", ewmh_client_update_desktop);
    luaA_class_connect_signal(globalconf.L, &tag_class, "property::selected", ewmh_update_net_current_desktop);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "focus", ewmh_client_reset_urgent);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::struts", ewmh_update_strut);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::window", ewmh_update_strut);
    luaA_class_connect_signal(globalconf.L, &tag_class, "property::name", ewmh_update_net_desktop_names);
    luaA_class_connect_signal(globalconf.L, &tag_class, "property::attached", ewmh_update_net_desktop_names);
    luaA_class_connect_signal(globalconf.L, &tag_class, "property::attached", ewmh_update_net_numbers_of_desktop);
    luaA_class_connect_signal(globalconf.L, &tag_class, "property::attached", ewmh_update_workarea);

}

static void
ewmh_process_state_atom(client_t *c, xcb_atom_t state, int set)
{
    luaA_object_push(globalconf.L, c);

    if(state == _NET_WM_STATE_STICKY)
    {
        if(set == _NET_WM_STATE_REMOVE)
            ewindow_set_sticky(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            ewindow_set_sticky(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            ewindow_set_sticky(globalconf.L, -1, !c->sticky);
    }
    else if(state == _NET_WM_STATE_SKIP_TASKBAR)
    {
        if(set == _NET_WM_STATE_REMOVE)
            client_set_skip_taskbar(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            client_set_skip_taskbar(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            client_set_skip_taskbar(globalconf.L, -1, !c->skip_taskbar);
    }
    else if(state == _NET_WM_STATE_FULLSCREEN)
    {
        if(set == _NET_WM_STATE_REMOVE)
            ewindow_set_fullscreen(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            ewindow_set_fullscreen(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            ewindow_set_fullscreen(globalconf.L, -1, !c->fullscreen);
    }
    else if(state == _NET_WM_STATE_MAXIMIZED_HORZ)
    {
        if(set == _NET_WM_STATE_REMOVE)
            ewindow_set_maximized_horizontal(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            ewindow_set_maximized_horizontal(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            ewindow_set_maximized_horizontal(globalconf.L, -1, !c->maximized_horizontal);
    }
    else if(state == _NET_WM_STATE_MAXIMIZED_VERT)
    {
        if(set == _NET_WM_STATE_REMOVE)
            ewindow_set_maximized_vertical(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            ewindow_set_maximized_vertical(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            ewindow_set_maximized_vertical(globalconf.L, -1, !c->maximized_vertical);
    }
    else if(state == _NET_WM_STATE_ABOVE)
    {
        if(set == _NET_WM_STATE_REMOVE)
            ewindow_set_above(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            ewindow_set_above(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            ewindow_set_above(globalconf.L, -1, !c->above);
    }
    else if(state == _NET_WM_STATE_BELOW)
    {
        if(set == _NET_WM_STATE_REMOVE)
            ewindow_set_below(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            ewindow_set_below(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            ewindow_set_below(globalconf.L, -1, !c->below);
    }
    else if(state == _NET_WM_STATE_MODAL)
    {
        if(set == _NET_WM_STATE_REMOVE)
            ewindow_set_modal(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            ewindow_set_modal(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            ewindow_set_modal(globalconf.L, -1, !c->modal);
    }
    else if(state == _NET_WM_STATE_HIDDEN)
    {
        if(set == _NET_WM_STATE_REMOVE)
            ewindow_set_minimized(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            ewindow_set_minimized(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            ewindow_set_minimized(globalconf.L, -1, !c->minimized);
    }
    else if(state == _NET_WM_STATE_DEMANDS_ATTENTION)
    {
        if(set == _NET_WM_STATE_REMOVE)
            client_set_urgent(globalconf.L, -1, false);
        else if(set == _NET_WM_STATE_ADD)
            client_set_urgent(globalconf.L, -1, true);
        else if(set == _NET_WM_STATE_TOGGLE)
            client_set_urgent(globalconf.L, -1, !c->urgent);
    }
}

int
ewmh_process_client_message(xcb_client_message_event_t *ev)
{
    client_t *c;

    if(ev->type == _NET_CURRENT_DESKTOP)
        tag_view_only_byindex(ev->data.data32[0]);
    else if(ev->type == _NET_CLOSE_WINDOW)
    {
        if((c = client_getbywin(ev->window)))
           client_kill(c);
    }
    else if(ev->type == _NET_WM_DESKTOP)
    {
        if((c = client_getbywin(ev->window)))
        {
            if(ev->data.data32[0] == 0xffffffff)
                c->sticky = true;
            else
                for(int i = 0; i < _G_tags.len; i++)
                {
                    luaA_object_push(globalconf.L, c);
                    luaA_object_push(globalconf.L, _G_tags.tab[i]);
                    if((int) ev->data.data32[0] == i)
                        tag_ewindow(globalconf.L, -2, -1);
                    else
                        untag_ewindow(globalconf.L, -2, -1);
                    lua_pop(globalconf.L, 2);
                }
        }
    }
    else if(ev->type == _NET_WM_STATE)
    {
        if((c = client_getbywin(ev->window)))
        {
            ewmh_process_state_atom(c, (xcb_atom_t) ev->data.data32[1], ev->data.data32[0]);
            if(ev->data.data32[2])
                ewmh_process_state_atom(c, (xcb_atom_t) ev->data.data32[2],
                                        ev->data.data32[0]);
        }
    }
    else if(ev->type == _NET_ACTIVE_WINDOW)
    {
        if((c = client_getbywin(ev->window)))
        {
            luaA_object_push(globalconf.L, c);
            window_focus(globalconf.L, -1);
            lua_pop(globalconf.L, 1);
        }
    }

    return 0;
}

void
ewmh_client_check_hints(client_t *c)
{
    xcb_atom_t *state;
    void *data = NULL;
    int desktop;
    xcb_get_property_cookie_t c0, c1, c2;
    xcb_get_property_reply_t *reply;

    /* Send the GetProperty requests which will be processed later */
    c0 = xcb_get_property_unchecked(_G_connection, false, c->window,
                                    _NET_WM_DESKTOP, XCB_GET_PROPERTY_TYPE_ANY, 0, 1);

    c1 = xcb_get_property_unchecked(_G_connection, false, c->window,
                                    _NET_WM_STATE, ATOM, 0, UINT32_MAX);

    c2 = xcb_get_property_unchecked(_G_connection, false, c->window,
                                    _NET_WM_WINDOW_TYPE, ATOM, 0, UINT32_MAX);

    reply = xcb_get_property_reply(_G_connection, c0, NULL);
    if(reply && reply->value_len && (data = xcb_get_property_value(reply)))
    {
        desktop = *(uint32_t *) data;
        if(desktop == -1)
            c->sticky = true;
        else
        {
            luaA_object_push(globalconf.L, c);
            for(int i = 0; i < _G_tags.len; i++)
            {
                luaA_object_push(globalconf.L, _G_tags.tab[i]);
                if(desktop == i)
                    tag_ewindow(globalconf.L, -2, -1);
                else
                    untag_ewindow(globalconf.L, -2, -1);
                lua_pop(globalconf.L, 1);
            }
            lua_pop(globalconf.L, 1);
        }
    }

    p_delete(&reply);

    reply = xcb_get_property_reply(_G_connection, c1, NULL);
    if(reply && (data = xcb_get_property_value(reply)))
    {
        state = (xcb_atom_t *) data;
        for(int i = 0; i < xcb_get_property_value_length(reply) / ssizeof(xcb_atom_t); i++)
            ewmh_process_state_atom(c, state[i], _NET_WM_STATE_ADD);
    }

    p_delete(&reply);

    reply = xcb_get_property_reply(_G_connection, c2, NULL);
    if(reply && (data = xcb_get_property_value(reply)))
    {
        state = (xcb_atom_t *) data;
        ewindow_type_t type = c->type;
        for(int i = 0; i < xcb_get_property_value_length(reply) / ssizeof(xcb_atom_t); i++)
            if(0) {}
#define HANDLE(wtype) \
            else if(state[i] == _NET_WM_WINDOW_##wtype) \
                type = MAX(c->type, EWINDOW_##wtype);
HANDLE(TYPE_DESKTOP)
HANDLE(TYPE_DOCK)
HANDLE(TYPE_TOOLBAR)
HANDLE(TYPE_MENU)
HANDLE(TYPE_UTILITY)
HANDLE(TYPE_SPLASH)
HANDLE(TYPE_DIALOG)
HANDLE(TYPE_DROPDOWN_MENU)
HANDLE(TYPE_POPUP_MENU)
HANDLE(TYPE_TOOLTIP)
HANDLE(TYPE_NOTIFICATION)
HANDLE(TYPE_COMBO)
HANDLE(TYPE_DND)
#undef HANDLE

        luaA_object_push(globalconf.L, c);
        ewindow_set_type(globalconf.L, -1, type);
        lua_pop(globalconf.L, 1);
    }

    p_delete(&reply);
}

/** Process the WM strut of a client.
 * \param c The client.
 * \param strut_r (Optional) An existing reply.
 */
void
ewmh_process_client_strut(client_t *c, xcb_get_property_reply_t *strut_r)
{
    void *data;
    xcb_get_property_reply_t *mstrut_r = NULL;

    if(!strut_r)
    {
        xcb_get_property_cookie_t strut_q = xcb_get_property_unchecked(_G_connection, false, c->window,
                                                                       _NET_WM_STRUT_PARTIAL, CARDINAL, 0, 12);
        strut_r = mstrut_r = xcb_get_property_reply(_G_connection, strut_q, NULL);
    }

    if(strut_r
       && strut_r->value_len
       && (data = xcb_get_property_value(strut_r)))
    {
        uint32_t *strut = data;

        if(c->strut.left != strut[0]
           || c->strut.right != strut[1]
           || c->strut.top != strut[2]
           || c->strut.bottom != strut[3]
           || c->strut.left_start_y != strut[4]
           || c->strut.left_end_y != strut[5]
           || c->strut.right_start_y != strut[6]
           || c->strut.right_end_y != strut[7]
           || c->strut.top_start_x != strut[8]
           || c->strut.top_end_x != strut[9]
           || c->strut.bottom_start_x != strut[10]
           || c->strut.bottom_end_x != strut[11])
        {
            c->strut.left = strut[0];
            c->strut.right = strut[1];
            c->strut.top = strut[2];
            c->strut.bottom = strut[3];
            c->strut.left_start_y = strut[4];
            c->strut.left_end_y = strut[5];
            c->strut.right_start_y = strut[6];
            c->strut.right_end_y = strut[7];
            c->strut.top_start_x = strut[8];
            c->strut.top_end_x = strut[9];
            c->strut.bottom_start_x = strut[10];
            c->strut.bottom_end_x = strut[11];

            luaA_object_push(globalconf.L, c);
            luaA_object_emit_signal(globalconf.L, -1, "property::struts", 0);
            lua_pop(globalconf.L, 1);
        }
    }

    p_delete(&mstrut_r);
}

/** Send request to get NET_WM_ICON (EWMH)
 * \param w The window.
 * \return The cookie associated with the request.
 */
xcb_get_property_cookie_t
ewmh_window_icon_get_unchecked(xcb_window_t w)
{
  return xcb_get_property_unchecked(_G_connection, false, w,
                                    _NET_WM_ICON, CARDINAL, 0, UINT32_MAX);
}

int
ewmh_window_icon_from_reply(xcb_get_property_reply_t *r)
{
    uint32_t *data;
    uint64_t len;

    if(!r || r->type != CARDINAL || r->format != 32 || r->length < 2)
        return 0;

    data = (uint32_t *) xcb_get_property_value(r);
    if (!data)
        return 0;

    /* Check that the property is as long as it should be, handling integer
     * overflow. <uint32_t> times <another uint32_t casted to uint64_t> always
     * fits into an uint64_t and thus this multiplication cannot overflow.
     */
    len = data[0] * (uint64_t) data[1];
    if (!data[0] || !data[1] || len > r->length - 2)
        return 0;

    return image_new_from_argb32(globalconf.L, data[0], data[1], data + 2);
}

/** Get NET_WM_ICON.
 * \param cookie The cookie.
 * \return The number of elements on stack.
 */
int
ewmh_window_icon_get_reply(xcb_get_property_cookie_t cookie)
{
    xcb_get_property_reply_t *r = xcb_get_property_reply(_G_connection, cookie, NULL);
    int ret = ewmh_window_icon_from_reply(r);
    p_delete(&r);
    return ret;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
