/*
 * client.h - client management header
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

#ifndef AWESOME_OBJECTS_CLIENT_H
#define AWESOME_OBJECTS_CLIENT_H

#include "mouse.h"
#include "stack.h"
#include "draw.h"
#include "banning.h"
#include "objects/ewindow.h"
#include "common/luaobject.h"

typedef struct client_t client_t;
/** Client type */
struct client_t
{
    EWINDOW_OBJECT_HEADER
    /** Client name */
    char *name, *alt_name, *icon_name, *alt_icon_name;
    /** WM_CLASS stuff */
    char *class, *instance;
    /** Has urgency hint */
    bool urgent;
    /** true if the client must be skipped from task bar client list */
    bool skip_taskbar;
    /** Window of the group leader */
    xcb_window_t group_window;
    /** Window holding command needed to start it (session management related) */
    xcb_window_t leader_window;
    /** Client's WM_PROTOCOLS property */
    xcb_get_wm_protocols_reply_t protocols;
    /** Icon */
    image_t *icon;
    /** Machine the client is running on. */
    char *machine;
    /** Role of the client */
    char *role;
    /** Client pid */
    uint32_t pid;
    /** Client it is transient for */
    client_t *transient_for;
};

DO_BARRAY(client_t *, client, DO_NOTHING, window_cmp)

/** Client class */
lua_interface_window_t client_class;
LUA_OBJECT_FUNCS((lua_class_t *) &client_class, client_t, client)

/** Clients list */
client_array_t _G_clients;

client_t * client_getbywin(xcb_window_t);
void client_manage(xcb_window_t, xcb_get_geometry_reply_t *, bool);
void client_unmanage(client_t *);
void client_kill(client_t *);
void client_set_urgent(lua_State *, int, bool);
void client_set_pid(lua_State *, int, uint32_t);
void client_set_role(lua_State *, int, char *);
void client_set_machine(lua_State *, int, char *);
void client_set_icon_name(lua_State *, int, char *);
void client_set_alt_icon_name(lua_State *, int, char *);
void client_set_class_instance(lua_State *, int, const char *, const char *);
void client_set_name(lua_State *L, int, char *);
void client_set_alt_name(lua_State *L, int, char *);
void client_set_group_window(lua_State *, int, xcb_window_t);
void client_set_icon(lua_State *, int, int);
void client_set_skip_taskbar(lua_State *, int, bool);
void client_set_transient_for(lua_State *, int, int);
bool client_hasproto(client_t *, xcb_atom_t);
void client_class_setup(lua_State *);

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
