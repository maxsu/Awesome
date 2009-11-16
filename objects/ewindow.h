/*
 * ewindow.h - Extended window object header
 *
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
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

#ifndef AWESOME_OBJECTS_EWINDOW_H
#define AWESOME_OBJECTS_EWINDOW_H

#include "strut.h"
#include "objects/window.h"
#include "objects/button.h"
#include "common/luaclass.h"

/** ewindows type */
typedef enum
{
    EWINDOW_TYPE_NORMAL = 0,
    EWINDOW_TYPE_DESKTOP,
    EWINDOW_TYPE_DOCK,
    EWINDOW_TYPE_SPLASH,
    EWINDOW_TYPE_DIALOG,
    /* The ones below may have TRANSIENT_FOR, but are not plain dialogs.
     * They were purposefully placed below DIALOG.
     */
    EWINDOW_TYPE_MENU,
    EWINDOW_TYPE_TOOLBAR,
    EWINDOW_TYPE_UTILITY,
    /* This ones are usually set on override-redirect windows. */
    EWINDOW_TYPE_DROPDOWN_MENU,
    EWINDOW_TYPE_POPUP_MENU,
    EWINDOW_TYPE_TOOLTIP,
    EWINDOW_TYPE_NOTIFICATION,
    EWINDOW_TYPE_COMBO,
    EWINDOW_TYPE_DND
} ewindow_type_t;

#define EWINDOW_OBJECT_HEADER \
    WINDOW_OBJECT_HEADER \
    /** Opacity */ \
    double opacity; \
    /** Strut */ \
    strut_t strut; \
    /** Border color */ \
    xcolor_t border_color; \
    /** Border width */ \
    uint16_t border_width; \
    /** Window tags */ \
    tag_array_t tags; \
    /** True if the window is sticky */ \
    bool sticky; \
    /** True if the client is minimized */ \
    bool minimized; \
    /** True if the client is fullscreen */ \
    bool fullscreen; \
    /** True if the client is maximized horizontally */ \
    bool maximized_horizontal; \
    /** True if the client is maximized vertically */ \
    bool maximized_vertical; \
    /** True if the client is above others */ \
    bool above; \
    /** True if the client is below others */ \
    bool below; \
    /** True if the client is modal */ \
    bool modal; \
    /** True if the client is on top */ \
    bool ontop; \
    /** The window type */ \
    ewindow_type_t type;

typedef struct ewindow_t ewindow_t;
/** Window structure */
struct ewindow_t
{
    EWINDOW_OBJECT_HEADER
};

DO_ARRAY(ewindow_t *, ewindow, DO_NOTHING)
DO_BARRAY(ewindow_t *, ewindow_binary, DO_NOTHING, window_cmp)

/** All managed ewindows */
ewindow_binary_array_t _G_ewindows;

lua_interface_window_t ewindow_class;

void ewindow_class_setup(lua_State *);

bool ewindow_isvisible(ewindow_t *);
ewindow_t *ewindow_getbywin(xcb_window_t);

void ewindow_set_opacity(lua_State *, int, double);
void ewindow_set_border_width(lua_State *, int, int);
void ewindow_set_sticky(lua_State *, int, bool);
void ewindow_set_above(lua_State *, int, bool);
void ewindow_set_below(lua_State *, int, bool);
void ewindow_set_modal(lua_State *, int, bool);
void ewindow_set_ontop(lua_State *, int, bool);
void ewindow_set_fullscreen(lua_State *, int, bool);
void ewindow_set_maximized_horizontal(lua_State *, int, bool);
void ewindow_set_maximized_vertical(lua_State *, int, bool);
void ewindow_set_minimized(lua_State *, int, bool);
void ewindow_set_type(lua_State *, int, ewindow_type_t);

int luaA_ewindow_get_transient_for(lua_State *, ewindow_t *);
int luaA_ewindow_get_type(lua_State *, ewindow_t *);

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
