/*
 * window.c - window object
 *
 * Copyright © 2009 Julien Danjou <julien@danjou.info>
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

#include <unistd.h>

#include <xcb/xcb_image.h>

#include "luaa.h"
#include "stack.h"
#include "awesome.h"
#include "xwindow.h"
#include "keyresolv.h"
#include "objects/window.h"
#include "objects/tag.h"
#include "objects/screen.h"
#include "common/luaobject.h"
#include "common/xutil.h"
#include "common/xcursor.h"

/** Focused window */
static window_t *window_focused;

static void
window_wipe(window_t *window)
{
    p_delete(&window->cursor);
}

bool
window_isvisible(lua_State *L, int idx)
{
    window_t *window = luaA_checkudata(L, idx, &window_class);
    lua_interface_window_t *interface = (lua_interface_window_t *) luaA_class_get_from_stack(L, idx);
    /* Go check for parent classes, but stop on window_class since higher
     * classes would not implement the isvisible method :-) */
    for(; interface && (lua_class_t *) interface != &window_class;
        interface = (lua_interface_window_t *) interface->parent)
        if(interface->isvisible)
            return interface->isvisible(window);
    return window->visible;
}

/** Prepare banning a window by running all needed Lua events.
 * \param window The window.
 */
void
window_ban_unfocus(window_t *window)
{
    /* Wait until the last moment to take away the focus from the window. */
    if(window_focused == window)
        /* Set focus on root window, so no events leak to the current window. */
        xcb_set_input_focus(_G_connection, XCB_INPUT_FOCUS_PARENT,
                            _G_root->window, XCB_CURRENT_TIME);
}

/** Ban window.
 * \param window The window.
 */
void
window_ban(window_t *window)
{
    if(!window->banned && window->window)
    {
        xcb_unmap_window(_G_connection, window->window);
        window->banned = true;
        window_ban_unfocus(window);
    }
}

/** Unban a window.
 * \param window The window.
 */
void
window_unban(window_t *window)
{
    if(window->banned && window->window)
    {
        xcb_map_window(_G_connection, window->window);
        window->banned = false;
    }
}

/** Record that a window got focus.
 * \param window The window that got focus.
 */
void
window_focus_update(window_t *window)
{
    window_focused = window;
    luaA_object_push(_G_L, window);
    luaA_object_emit_signal(_G_L, -1, "focus", 0);
    lua_pop(_G_L, 1);
}

/** Record that a window lost focus.
 * \param c Client being unfocused
 */
void
window_unfocus_update(window_t *window)
{
    window_focused = NULL;
    luaA_object_push(_G_L, window);
    luaA_object_emit_signal(_G_L, -1, "unfocus", 0);
    lua_pop(_G_L, 1);
}

/** Give focus to window.
 * \param L The Lua VM state.
 * \param idx The window index on the stack.
 */
void
window_focus(lua_State *L, int idx)
{
    window_t *window = luaA_checkudata(L, idx, &window_class);

    if(window->window)
    {
        /* If the window is banned but isvisible, unban it right now because you
         * can't set focus on unmapped window */
        if(window_isvisible(L, idx))
            window_unban(window);
        else
            return;

        /* Sets focus on window - using xcb_set_input_focus or WM_TAKE_FOCUS */
        if(window->focusable)
            xcb_set_input_focus(_G_connection, XCB_INPUT_FOCUS_PARENT,
                                window->window, XCB_CURRENT_TIME);
    }
}

/** Compute window geometry with respect to its geometry hints.
 * \param window The window.
 * \param geometry The geometry that the window might receive.
 * \return The geometry the window must take respecting its hints.
 */
area_t
window_geometry_hints(window_t *window, area_t geometry)
{
    int32_t basew, baseh, minw, minh;

    /* base size is substituted with min size if not specified */
    if(window->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
    {
        basew = window->size_hints.base_width;
        baseh = window->size_hints.base_height;
    }
    else if(window->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        basew = window->size_hints.min_width;
        baseh = window->size_hints.min_height;
    }
    else
        basew = baseh = 0;

    /* min size is substituted with base size if not specified */
    if(window->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        minw = window->size_hints.min_width;
        minh = window->size_hints.min_height;
    }
    else if(window->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
    {
        minw = window->size_hints.base_width;
        minh = window->size_hints.base_height;
    }
    else
        minw = minh = 0;

    if(window->size_hints.flags & XCB_SIZE_HINT_P_ASPECT
       && window->size_hints.min_aspect_num > 0
       && window->size_hints.min_aspect_den > 0
       && geometry.height - baseh > 0
       && geometry.width - basew > 0)
    {
        double dx = (double) (geometry.width - basew);
        double dy = (double) (geometry.height - baseh);
        double min = (double) window->size_hints.min_aspect_num / (double) window->size_hints.min_aspect_den;
        double max = (double) window->size_hints.max_aspect_num / (double) window->size_hints.min_aspect_den;
        double ratio = dx / dy;
        if(max > 0 && min > 0 && ratio > 0)
        {
            if(ratio < min)
            {
                dy = (dx * min + dy) / (min * min + 1);
                dx = dy * min;
                geometry.width = (int) dx + basew;
                geometry.height = (int) dy + baseh;
            }
            else if(ratio > max)
            {
                dy = (dx * min + dy) / (max * max + 1);
                dx = dy * min;
                geometry.width = (int) dx + basew;
                geometry.height = (int) dy + baseh;
            }
        }
    }

    if(minw)
        geometry.width = MAX(geometry.width, minw);
    if(minh)
        geometry.height = MAX(geometry.height, minh);

    if(window->size_hints.flags & XCB_SIZE_HINT_P_MAX_SIZE)
    {
        if(window->size_hints.max_width)
            geometry.width = MIN(geometry.width, window->size_hints.max_width);
        if(window->size_hints.max_height)
            geometry.height = MIN(geometry.height, window->size_hints.max_height);
    }

    if(window->size_hints.flags & (XCB_SIZE_HINT_P_RESIZE_INC | XCB_SIZE_HINT_BASE_SIZE)
       && window->size_hints.width_inc && window->size_hints.height_inc)
    {
        uint16_t t1 = geometry.width, t2 = geometry.height;
        unsigned_subtract(t1, basew);
        unsigned_subtract(t2, baseh);
        geometry.width -= t1 % window->size_hints.width_inc;
        geometry.height -= t2 % window->size_hints.height_inc;
    }

    return geometry;
}

static int
luaA_window_get_size_hints(lua_State *L, window_t *window)
{
    const char *u_or_p = NULL;

    lua_createtable(L, 0, 1);

    if(window->size_hints.flags & XCB_SIZE_HINT_US_POSITION)
        u_or_p = "user_position";
    else if(window->size_hints.flags & XCB_SIZE_HINT_P_POSITION)
        u_or_p = "program_position";

    if(u_or_p)
    {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, window->size_hints.x);
        lua_setfield(L, -2, "x");
        lua_pushnumber(L, window->size_hints.y);
        lua_setfield(L, -2, "y");
        lua_setfield(L, -2, u_or_p);
        u_or_p = NULL;
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_US_SIZE)
        u_or_p = "user_size";
    else if(window->size_hints.flags & XCB_SIZE_HINT_P_SIZE)
        u_or_p = "program_size";

    if(u_or_p)
    {
        lua_createtable(L, 0, 2);
        lua_pushnumber(L, window->size_hints.width);
        lua_setfield(L, -2, "width");
        lua_pushnumber(L, window->size_hints.height);
        lua_setfield(L, -2, "height");
        lua_setfield(L, -2, u_or_p);
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_MIN_SIZE)
    {
        lua_pushnumber(L, window->size_hints.min_width);
        lua_setfield(L, -2, "min_width");
        lua_pushnumber(L, window->size_hints.min_height);
        lua_setfield(L, -2, "min_height");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_MAX_SIZE)
    {
        lua_pushnumber(L, window->size_hints.max_width);
        lua_setfield(L, -2, "max_width");
        lua_pushnumber(L, window->size_hints.max_height);
        lua_setfield(L, -2, "max_height");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_RESIZE_INC)
    {
        lua_pushnumber(L, window->size_hints.width_inc);
        lua_setfield(L, -2, "width_inc");
        lua_pushnumber(L, window->size_hints.height_inc);
        lua_setfield(L, -2, "height_inc");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_ASPECT)
    {
        lua_pushnumber(L, window->size_hints.min_aspect_num);
        lua_setfield(L, -2, "min_aspect_num");
        lua_pushnumber(L, window->size_hints.min_aspect_den);
        lua_setfield(L, -2, "min_aspect_den");
        lua_pushnumber(L, window->size_hints.max_aspect_num);
        lua_setfield(L, -2, "max_aspect_num");
        lua_pushnumber(L, window->size_hints.max_aspect_den);
        lua_setfield(L, -2, "max_aspect_den");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_BASE_SIZE)
    {
        lua_pushnumber(L, window->size_hints.base_width);
        lua_setfield(L, -2, "base_width");
        lua_pushnumber(L, window->size_hints.base_height);
        lua_setfield(L, -2, "base_height");
    }

    if(window->size_hints.flags & XCB_SIZE_HINT_P_WIN_GRAVITY)
    {
        switch(window->size_hints.win_gravity)
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

/** Move and/or resize a window.
 * \param L The Lua VM state.
 * \param window The window.
 * \param geometry The new geometry.
 * \param Return true if the window has been resized or moved, false otherwise.
 */
bool
window_set_geometry(lua_State *L, window_t *window, area_t geometry)
{
    geometry = window_geometry_hints(window, geometry);

    int number_of_vals = 0;
    uint32_t set_geometry_win_vals[4], mask_vals = 0;

    if(window->movable)
    {
        if(window->geometry.x != geometry.x)
        {
            window->geometry.x = set_geometry_win_vals[number_of_vals++] = geometry.x;
            mask_vals |= XCB_CONFIG_WINDOW_X;
        }

        if(window->geometry.y != geometry.y)
        {
            window->geometry.y = set_geometry_win_vals[number_of_vals++] = geometry.y;
            mask_vals |= XCB_CONFIG_WINDOW_Y;
        }
    }

    if(window->resizable)
    {
        if(geometry.width > 0 && window->geometry.width != geometry.width)
        {
            window->geometry.width = set_geometry_win_vals[number_of_vals++] = geometry.width;
            mask_vals |= XCB_CONFIG_WINDOW_WIDTH;
        }

        if(geometry.height > 0 && window->geometry.height != geometry.height)
        {
            window->geometry.height = set_geometry_win_vals[number_of_vals++] = geometry.height;
            mask_vals |= XCB_CONFIG_WINDOW_HEIGHT;
        }
    }

    if(mask_vals)
    {
        if(window->window)
            xcb_configure_window(_G_connection, window->window, mask_vals, set_geometry_win_vals);

        if(mask_vals & XCB_CONFIG_WINDOW_X)
            window_emit_signal(L, window, "property::x", 0);
        if(mask_vals & XCB_CONFIG_WINDOW_Y)
            window_emit_signal(L, window, "property::y", 0);
        if(mask_vals & XCB_CONFIG_WINDOW_WIDTH)
            window_emit_signal(L, window, "property::width", 0);
        if(mask_vals & XCB_CONFIG_WINDOW_HEIGHT)
            window_emit_signal(L, window, "property::height", 0);

        window_emit_signal(L, window, "property::geometry", 0);

        return true;
    }

    return false;
}

/** Return client geometry.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \luastack
 * \lparam A table with new coordinates, or none.
 * \lreturn A table with client coordinates.
 */
static int
luaA_window_geometry(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);

    if(lua_gettop(L) == 2 && !lua_isnil(L, 2))
    {
        area_t geometry;

        luaA_checktable(L, 2);
        geometry.x = luaA_getopt_number(L, 2, "x", window->geometry.x);
        geometry.y = luaA_getopt_number(L, 2, "y", window->geometry.y);
        geometry.width = luaA_getopt_number(L, 2, "width", window->geometry.width);
        geometry.height = luaA_getopt_number(L, 2, "height", window->geometry.height);

        window_set_geometry(L, window, geometry);
    }

    return luaA_pusharea(L, window->geometry);
}

static int
luaA_window_set_x(lua_State *L, window_t *window)
{
    window_set_geometry(L, window, (area_t) { .x = luaL_checknumber(L, -1),
                                              .y = window->geometry.y,
                                              .width = window->geometry.width,
                                              .height = window->geometry.height });
    return 0;
}

static int
luaA_window_get_x(lua_State *L, window_t *window)
{
    lua_pushnumber(L, window->geometry.x);
    return 1;
}

static int
luaA_window_set_y(lua_State *L, window_t *window)
{
    window_set_geometry(L, window, (area_t) { .x = window->geometry.x,
                                              .y = luaL_checknumber(L, -1),
                                              .width = window->geometry.width,
                                              .height = window->geometry.height });
    return 0;
}

static int
luaA_window_get_y(lua_State *L, window_t *window)
{
    lua_pushnumber(L, window->geometry.y);
    return 1;
}

static int
luaA_window_set_width(lua_State *L, window_t *window)
{
    int width = luaL_checknumber(L, -1);
    if(width <= 0)
        luaL_error(L, "invalid width");
    window_set_geometry(L, window, (area_t) { .x = window->geometry.x,
                                              .y = window->geometry.y,
                                              .width = width,
                                              .height = window->geometry.height });
    return 0;
}

static int
luaA_window_get_width(lua_State *L, window_t *window)
{
    lua_pushnumber(L, window->geometry.width);
    return 1;
}

static int
luaA_window_set_height(lua_State *L, window_t *window)
{
    int height = luaL_checknumber(L, -1);
    if(height <= 0)
        luaL_error(L, "invalid height");
    window_set_geometry(L, window, (area_t) { .x = window->geometry.x,
                                              .y = window->geometry.y,
                                              .width = window->geometry.width,
                                              .height = height });
    return 0;
}

static int
luaA_window_get_height(lua_State *L, window_t *window)
{
    lua_pushnumber(L, window->geometry.height);
    return 1;
}

static int
luaA_window_get_content(lua_State *L, window_t *window)
{
    if(!window->window)
        return 1;

    xcb_image_t *ximage = xcb_image_get(_G_connection,
                                        window->window,
                                        0, 0,
                                        window->geometry.width,
                                        window->geometry.height,
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

/** Check if a window is visible.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_isvisible(lua_State *L)
{
    lua_pushboolean(L, window_isvisible(L, 1));
    return 1;
}

static LUA_OBJECT_DO_SET_PROPERTY_FUNC(window, &window_class, window_t, focusable)
static LUA_OBJECT_DO_SET_PROPERTY_FUNC(window, &window_class, window_t, layer)
LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(window, window_t, focusable, luaA_checkboolean)

static int
luaA_window_set_layer(lua_State *L, window_t *window)
{
    int layer = luaL_checknumber(L, 3);
    if(layer >= INT8_MIN && layer <= INT8_MAX)
        window_set_layer(L, window, layer);
    else
        luaL_error(L, "invalid layer, must be between %d and %d", INT8_MIN, INT8_MAX);
    return 0;
}

static int
luaA_window_set_cursor(lua_State *L, window_t *window)
{
    const char *buf = luaL_checkstring(L, -1);
    if(buf)
    {
        uint16_t cursor_font = xcursor_font_fromstr(buf);
        if(cursor_font)
        {
            xcb_cursor_t cursor = xcursor_new(_G_connection, cursor_font);
            p_delete(&window->cursor);
            window->cursor = a_strdup(buf);
            xwindow_set_cursor(window->window, cursor);
            luaA_object_emit_signal(L, -3, "property::cursor", 0);
        }
    }
    return 0;
}

/** Give the focus to a window.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_focus(lua_State *L)
{
    window_focus(L, 1);
    return 0;
}

static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, window, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, layer, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, cursor, lua_pushstring)
LUA_OBJECT_EXPORT_PROPERTY(window, window_t, parent, luaA_object_push)
static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, movable, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, resizable, lua_pushboolean)
LUA_OBJECT_EXPORT_PROPERTY(window, window_t, focusable, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(window, window_t, visible, lua_pushboolean)

/** Raise an window on top of others which are on the same layer.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_raise(lua_State *L)
{
    stack_window_raise(L, luaA_checkudata(L, 1, &window_class));
    return 0;
}

/** Lower an window on top of others which are on the same layer.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_lower(lua_State *L)
{
    stack_window_lower(L, luaA_checkudata(L, 1, &window_class));
    return 0;
}

/** Grab keyboard on window.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_grab_keyboard(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);
    xcb_grab_keyboard_reply_t *xgb_r = NULL;

    /** Try to grab keyboard */
    for(int i = 1000; i; i--)
    {
        xcb_grab_keyboard_cookie_t xgb_c = xcb_grab_keyboard(_G_connection, true,
                                                             window->window,
                                                             XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC,
                                                             XCB_GRAB_MODE_ASYNC);
        if((xgb_r = xcb_grab_keyboard_reply(_G_connection, xgb_c, NULL)))
            break;

        usleep(1000);
    }

    lua_pushboolean(L, xgb_r != NULL);
    p_delete(&xgb_r);

    return 1;
}

/** Stop grabbing the keyboard.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_ungrab_keyboard(lua_State *L)
{
    xcb_ungrab_keyboard(_G_connection, XCB_CURRENT_TIME);
    return 0;
}

/** Grab the mouse.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_grab_pointer(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);
    uint16_t cfont = xcursor_font_fromstr(luaL_optstring(L, 2, CURSOR_DEFAULT_NAME));
    xcb_grab_pointer_reply_t *grab_ptr_r = NULL;

    if(!cfont)
        luaL_error(L, "invalid cursor name");

    xcb_cursor_t cursor = xcursor_new(_G_connection, cfont);

    for(int i = 1000; i; i--)
    {
        xcb_grab_pointer_cookie_t grab_ptr_c =
            xcb_grab_pointer_unchecked(_G_connection, false, window->window,
                                       XCB_EVENT_MASK_BUTTON_PRESS
                                       | XCB_EVENT_MASK_BUTTON_RELEASE
                                       | XCB_EVENT_MASK_POINTER_MOTION,
                                       XCB_GRAB_MODE_ASYNC,
                                       XCB_GRAB_MODE_ASYNC,
                                       window->window, cursor, XCB_CURRENT_TIME);

        if((grab_ptr_r = xcb_grab_pointer_reply(_G_connection, grab_ptr_c, NULL)))
            break;

        usleep(1000);
    }

    lua_pushboolean(L, grab_ptr_r != NULL);
    p_delete(&grab_ptr_r);

    return 1;
}

/** Stop grabbing the mouse.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_window_ungrab_pointer(lua_State *L)
{
    xcb_ungrab_pointer(_G_connection, XCB_CURRENT_TIME);
    return 0;
}

/** Take a modifier table from the stack and return modifiers mask.
 * \param L The Lua VM state.
 * \param ud The index of the table.
 * \return The mask value.
 */
static uint16_t
luaA_tomodifiers(lua_State *L, int ud)
{
    luaA_checktable(L, ud);
    ssize_t len = lua_objlen(L, ud);
    uint16_t mod = XCB_NONE;
    for(int i = 1; i <= len; i++)
    {
        lua_rawgeti(L, ud, i);
        size_t blen;
        const char *key = luaL_checklstring(L, -1, &blen);
        mod |= xutil_key_mask_fromstr(key, blen);
        lua_pop(L, 1);
    }
    return mod;
}

static int
luaA_window_ungrab_button(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);
    xcb_ungrab_button(_G_connection, luaL_checknumber(L, 3), window->window, luaA_tomodifiers(L, 2));
    return 0;
}

/** Grab a button on a window.
 * \param L The Lua VM state.
 * \return The number of elements pushed on the stack.
 */
static int
luaA_window_grab_button(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);

    luaA_checktable(L, 2);

    /* Set modifiers */
    lua_getfield(L, 2, "modifiers");
    luaA_checktable(L, -1);
    uint16_t modifiers = luaA_tomodifiers(L, -1);
    /* Set button */
    lua_getfield(L, 2, "button");
    xcb_button_t button = luaL_checknumber(L, -1);

    /* Grab buttons */
    xcb_grab_button(_G_connection, false, window->window,
                    XCB_EVENT_MASK_BUTTON_PRESS
                    | XCB_EVENT_MASK_BUTTON_RELEASE
                    | XCB_EVENT_MASK_POINTER_MOTION,
                    XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_NONE, XCB_NONE,
                    button, modifiers);

    /* Remove button and modifiers field */
    lua_pop(L, 2);

    return 0;
}

static int
luaA_window_ungrab_key(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);
    uint16_t modifiers = luaA_tomodifiers(L, 2);
    size_t len;
    const char *keysym_name = luaL_checklstring(L, 3, &len);
    xcb_keycode_t *keycodes = keyresolv_string_to_keycode(keysym_name, len);

    if(keycodes)
    {
        for(xcb_keycode_t *k = keycodes; *k; k++)
            xcb_ungrab_key(_G_connection, *k, window->window, modifiers);
        p_delete(&keycodes);
    }

    return 0;
}

static int
luaA_window_grab_key(lua_State *L)
{
    window_t *window = luaA_checkudata(L, 1, &window_class);
    uint16_t modifiers = luaA_tomodifiers(L, 2);
    size_t len;
    const char *keysym_name = luaL_checklstring(L, 3, &len);
    xcb_keycode_t *keycodes = keyresolv_string_to_keycode(keysym_name, len);

    if(keycodes)
    {
        for(xcb_keycode_t *k = keycodes; *k; k++)
            xcb_grab_key(_G_connection, false, window->window, modifiers, *k, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        p_delete(&keycodes);
    }

    return 0;
}

static void
window_init(lua_State *L, window_t *window)
{
    window->cursor = a_strdup(CURSOR_DEFAULT_NAME);
}

LUA_CLASS_FUNCS(window, &window_class)

void
window_class_setup(lua_State *L)
{
    static const struct luaL_reg window_methods[] =
    {
        LUA_CLASS_METHODS(window)
        { "focus", luaA_window_focus },
        { "geometry", luaA_window_geometry },
        { "isvisible", luaA_window_isvisible },
        { "raise", luaA_window_raise },
        { "lower", luaA_window_lower },
        { "grab_keyboard", luaA_window_grab_keyboard },
        { "ungrab_keyboard", luaA_window_ungrab_keyboard },
        { "grab_pointer", luaA_window_grab_pointer },
        { "ungrab_pointer", luaA_window_ungrab_pointer },
        { "grab_button", luaA_window_grab_button },
        { "ungrab_button", luaA_window_ungrab_button },
        { "grab_key", luaA_window_grab_key },
        { "ungrab_key", luaA_window_ungrab_key },
        { NULL, NULL }
    };

    luaA_class_setup(L, &window_class, "window", NULL, sizeof(window_t),
                     (lua_class_initializer_t) window_init,
                     (lua_class_collector_t) window_wipe, NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     window_methods, NULL, NULL);

    luaA_class_add_property(&window_class, "window",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_window,
                            NULL);
    luaA_class_add_property(&window_class, "focusable",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_focusable,
                            NULL);
    luaA_class_add_property(&window_class, "cursor",
                            (lua_class_propfunc_t) luaA_window_set_cursor,
                            (lua_class_propfunc_t) luaA_window_get_cursor,
                            (lua_class_propfunc_t) luaA_window_set_cursor);
    luaA_class_add_property(&window_class, "parent",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_parent,
                            NULL);
    luaA_class_add_property(&window_class, "size_hints",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_size_hints,
                            NULL);
    luaA_class_add_property(&window_class, "x",
                            (lua_class_propfunc_t) luaA_window_set_x,
                            (lua_class_propfunc_t) luaA_window_get_x,
                            (lua_class_propfunc_t) luaA_window_set_x);
    luaA_class_add_property(&window_class, "y",
                            (lua_class_propfunc_t) luaA_window_set_y,
                            (lua_class_propfunc_t) luaA_window_get_y,
                            (lua_class_propfunc_t) luaA_window_set_y);
    luaA_class_add_property(&window_class, "width",
                            (lua_class_propfunc_t) luaA_window_set_width,
                            (lua_class_propfunc_t) luaA_window_get_width,
                            (lua_class_propfunc_t) luaA_window_set_width);
    luaA_class_add_property(&window_class, "height",
                            (lua_class_propfunc_t) luaA_window_set_height,
                            (lua_class_propfunc_t) luaA_window_get_height,
                            (lua_class_propfunc_t) luaA_window_set_height);
    luaA_class_add_property(&window_class, "content",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_content,
                            NULL);
    luaA_class_add_property(&window_class, "movable",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_movable,
                            NULL);
    luaA_class_add_property(&window_class, "resizable",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_resizable,
                            NULL);
    luaA_class_add_property(&window_class, "visible",
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_visible,
                            NULL);
    luaA_class_add_property(&window_class, "layer",
                            (lua_class_propfunc_t) luaA_window_set_layer,
                            (lua_class_propfunc_t) luaA_window_get_layer,
                            (lua_class_propfunc_t) luaA_window_set_layer);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
