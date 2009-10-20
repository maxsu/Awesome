/*
 * stack.c - ewindow stack management
 *
 * Copyright © 2008-2009 Julien Danjou <julien@danjou.info>
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

#include "ewmh.h"
#include "screen.h"
#include "stack.h"
#include "objects/client.h"
#include "objects/wibox.h"
#include "screen.h"

static void
stack_ewindow_remove(ewindow_t *ewindow)
{
    foreach(w, ewindow->parent->stack)
        if(*w == ewindow)
        {
            ewindow_array_remove(&ewindow->parent->stack, w);
            break;
        }
}

/** Push the ewindow at the beginning of the ewindow stack.
 * \param ewindow The ewindow to push.
 */
static void
stack_ewindow_push(ewindow_t *ewindow)
{
    stack_ewindow_remove(ewindow);
    ewindow_array_push(&ewindow->parent->stack, ewindow);
}

/** Push the ewindow at the end of the ewindow stack.
 * \param ewindow The ewindow to push.
 */
static void
stack_ewindow_append(ewindow_t *ewindow)
{
    stack_ewindow_remove(ewindow);
    ewindow_array_append(&ewindow->parent->stack, ewindow);
}

/** Put ewindow on bottom of the stack.
 * \param L The Lua VM state.
 * \param idx The index of the ewindow to raise.
 */
void
stack_ewindow_lower(lua_State *L, int idx)
{
    ewindow_t *ewindow = luaA_checkudata(L, idx, (lua_class_t *) &ewindow_class);
    stack_ewindow_push(ewindow);
    luaA_object_emit_signal(L, idx, "lower", 0);
}

/** Put ewindow on top of the stack.
 * \param L The Lua VM state.
 * \param idx The index of the ewindow to raise.
 */
void
stack_ewindow_raise(lua_State *L, int idx)
{
    ewindow_t *ewindow = luaA_checkudata(L, idx, (lua_class_t *) &ewindow_class);
    /* Push ewindow on top of the stack. */
    stack_ewindow_append(ewindow);
    luaA_object_emit_signal(L, idx, "raise", 0);
}

/** Stack a window above another window, without causing errors.
 * \param w The window.
 * \param previous The window which should be below this window.
 */
static void
stack_window_above(xcb_window_t w, xcb_window_t previous)
{
    if (previous == XCB_NONE)
        /* This would cause an error from the X server. Also, if we really
         * changed the stacking order of all windows, they'd all have to redraw
         * themselves. Doing it like this is better. */
        return;

    xcb_configure_window(globalconf.connection, w,
                         XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE,
                         (uint32_t[]) { previous, XCB_STACK_MODE_ABOVE });
}

/** Stack a ewindow above.
 * \param ewindow The ewindow.
 * \param previous The previous ewindow on the stack.
 * \return The next-previous!
 */
static xcb_window_t
stack_ewindow_above(ewindow_t *ewindow, xcb_window_t previous)
{
    stack_window_above(ewindow->frame_window, previous);

    previous = ewindow->frame_window;

    /* stack transient window on top of their parents */
    if(ewindow->parent)
        foreach(node, ewindow->parent->stack)
            if((*node)->transient_for == ewindow)
                previous = stack_ewindow_above(*node, previous);

    return previous;
}

/** Stacking layout layers */
typedef enum
{
    /** This one is a special layer */
    WINDOW_LAYER_IGNORE,
    WINDOW_LAYER_DESKTOP,
    WINDOW_LAYER_BELOW,
    WINDOW_LAYER_NORMAL,
    WINDOW_LAYER_ABOVE,
    WINDOW_LAYER_FULLSCREEN,
    WINDOW_LAYER_ONTOP,
    /** This one only used for counting and is not a real layer */
    WINDOW_LAYER_COUNT
} ewindow_layer_t;

/** Get the real layer of a client according to its attribute (fullscreen, …)
 * \param window The window.
 * \return The real layer.
 */
extern window_t *window_focused;
static ewindow_layer_t
ewindow_layer_translator(ewindow_t *ewindow)
{
    /* first deal with user set attributes */
    if(ewindow->ontop)
        return WINDOW_LAYER_ONTOP;
    /* Fullscreen windows only get their own layer when they have the focus */
    else if(ewindow->fullscreen && window_focused == (window_t *) ewindow)
        return WINDOW_LAYER_FULLSCREEN;
    else if(ewindow->above)
        return WINDOW_LAYER_ABOVE;
    else if(ewindow->below)
        return WINDOW_LAYER_BELOW;
    /* check for transient attr */
    else if(ewindow->transient_for)
        return WINDOW_LAYER_IGNORE;

    /* then deal with windows type */
    switch(ewindow->type)
    {
      case EWINDOW_TYPE_DESKTOP:
        return WINDOW_LAYER_DESKTOP;
      default:
        break;
    }

    return WINDOW_LAYER_NORMAL;
}

/** Restack windows.
 * \todo It might be worth stopping to restack everyone and only stack `c'
 * relatively to the first matching in the list.
 */
static int
stack_refresh(lua_State *L)
{
    ewindow_t *window = luaA_checkudata(L, 1, (lua_class_t *) &ewindow_class);
    xcb_window_t next = XCB_NONE;

    if(window->parent)
        for(ewindow_layer_t layer = WINDOW_LAYER_DESKTOP; layer < WINDOW_LAYER_COUNT; layer++)
            foreach(node, window->parent->stack)
                if(ewindow_layer_translator(*node) == layer)
                    next = stack_ewindow_above(*node, next);

    return 0;
}

static int
luaA_stack_ewindow_remove(lua_State *L)
{
    stack_ewindow_remove(luaA_checkudata(L, 1, (lua_class_t *) &ewindow_class));
    return 0;
}

void
stack_init(void)
{
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::fullscreen", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::maximized_vertical", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::maximized_horizontal", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::above", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::below", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::modal", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::ontop", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::ontop", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::visible", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "property::screen", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "raise", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &ewindow_class, "lower", stack_refresh);
    luaA_class_connect_signal(globalconf.L, (lua_class_t *) &client_class, "unmanage", luaA_stack_ewindow_remove);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
