/*
 * ewindow.c - Extended window object
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

#include "luaa.h"
#include "xwindow.h"
#include "ewmh.h"
#include "screen.h"
#include "objects/window.h"
#include "objects/tag.h"
#include "common/luaobject.h"
#include "common/xutil.h"

LUA_CLASS_FUNCS(ewindow, (lua_class_t *) &ewindow_class)

bool
ewindow_isvisible(ewindow_t *ewindow)
{
    if(ewindow->sticky)
        return true;

    foreach(tag, ewindow->screen->tags)
        if(tag_get_selected(*tag) && ewindow_is_tagged(ewindow, *tag))
            return true;

    return false;
}

/** Return ewindow struts (reserved space at the edge of the screen).
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_ewindow_struts(lua_State *L)
{
    ewindow_t *ewindow = luaA_checkudata(L, 1, (lua_class_t *) &ewindow_class);

    if(lua_gettop(L) == 2)
    {
        luaA_tostrut(L, 2, &ewindow->strut);
        ewmh_update_strut(ewindow->window, &ewindow->strut);
        luaA_object_emit_signal(L, 1, "property::struts", 0);
        if(ewindow->screen)
            screen_emit_signal(L, ewindow->screen, "property::workarea", 0);
    }

    return luaA_pushstrut(L, ewindow->strut);
}

/** Set an ewindow opacity.
 * \param L The Lua VM state.
 * \param idx The index of the ewindow on the stack.
 * \param opacity The opacity value.
 */
void
ewindow_set_opacity(lua_State *L, int idx, double opacity)
{
    ewindow_t *ewindow = luaA_checkudata(L, idx, (lua_class_t *) &ewindow_class);

    if(ewindow->opacity != opacity)
    {
        ewindow->opacity = opacity;
        xwindow_set_opacity(ewindow->window, opacity);
        luaA_object_emit_signal(L, idx, "property::opacity", 0);
    }
}

/** Set an ewindow opacity.
 * \param L The Lua VM state.
 * \param ewindow The ewindow object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_ewindow_set_opacity(lua_State *L, ewindow_t *ewindow)
{
    if(lua_isnil(L, -1))
        ewindow_set_opacity(L, -3, -1);
    else
    {
        double d = luaL_checknumber(L, -1);
        if(d >= 0 && d <= 1)
            ewindow_set_opacity(L, -3, d);
    }
    return 0;
}

/** Get the ewindow opacity.
 * \param L The Lua VM state.
 * \param ewindow The ewindow object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_ewindow_get_opacity(lua_State *L, ewindow_t *ewindow)
{
    if(ewindow->opacity >= 0)
    {
        lua_pushnumber(L, ewindow->opacity);
        return 1;
    }
    return 0;
}

/** Set the ewindow border color.
 * \param L The Lua VM state.
 * \param ewindow The ewindow object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_ewindow_set_border_color(lua_State *L, ewindow_t *ewindow)
{
    size_t len;
    const char *color_name = luaL_checklstring(L, -1, &len);

    if(color_name &&
       xcolor_init_reply(xcolor_init_unchecked(&ewindow->border_color, color_name, len)))
    {
        xwindow_set_border_color(ewindow->window, &ewindow->border_color);
        luaA_object_emit_signal(L, -3, "property::border_color", 0);
    }

    return 0;
}

/** Set an ewindow border width.
 * \param L The Lua VM state.
 * \param idx The ewindow index.
 * \param width The border width.
 */
void
ewindow_set_border_width(lua_State *L, int idx, int width)
{
    ewindow_t *ewindow = luaA_checkudata(L, idx, (lua_class_t *) &ewindow_class);

    if(width == ewindow->border_width || width < 0)
        return;

    xcb_configure_window(globalconf.connection, ewindow->window,
                         XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         (uint32_t[]) { width });

    ewindow->border_width = width;

    luaA_object_emit_signal(L, idx, "property::border_width", 0);
}

static int
luaA_ewindow_set_border_width(lua_State *L, ewindow_t *c)
{
    ewindow_set_border_width(L, -3, luaL_checknumber(L, -1));
    return 0;
}

/** Access or set the ewindow tags.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 * \lparam A table with tags to set, or none to get the current tags table.
 * \return The ewindow tag.
 */
static int
luaA_ewindow_tags(lua_State *L)
{
    ewindow_t *c = luaA_checkudata(L, 1, (lua_class_t *) (lua_class_t *) &ewindow_class);

    if(lua_gettop(L) == 2)
    {
        luaA_checktable(L, 2);
        foreach(tag, c->tags)
        {
            luaA_object_push_item(L, 1, *tag);
            untag_ewindow(L, 1, -1);
            /* remove tag */
            lua_pop(L, 1);
        }
        lua_pushnil(L);
        while(lua_next(L, 2))
        {
            tag_ewindow(L, 1, -1);
            /* remove value (tag) */
            lua_pop(L, 1);
        }
    }

    int i = 0;
    lua_createtable(L, c->tags.len, 0);
    foreach(tag, c->tags)
    {
        luaA_object_push_item(L, 1, *tag);
        lua_rawseti(L, -2, ++i);
    }

    return 1;
}

LUA_OBJECT_DO_SET_PROPERTY_FUNC(ewindow, (lua_class_t *) &ewindow_class, ewindow_t, sticky)

static int
luaA_ewindow_set_sticky(lua_State *L, ewindow_t *c)
{
    ewindow_set_sticky(L, -3, luaA_checkboolean(L, -1));
    return 0;
}

static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, border_color, luaA_pushxcolor)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, border_width, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, sticky, lua_pushboolean)

void
ewindow_class_setup(lua_State *L)
{
    static const struct luaL_reg ewindow_methods[] =
    {
        LUA_CLASS_METHODS(ewindow)
        { NULL, NULL }
    };

    static const struct luaL_reg ewindow_meta[] =
    {
        { "struts", luaA_ewindow_struts },
        { "tags", luaA_ewindow_tags },
        { NULL, NULL }
    };

    luaA_class_setup(L, (lua_class_t *) &ewindow_class, "ewindow", &window_class,
                     NULL, NULL, NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     ewindow_methods, ewindow_meta);

    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_OPACITY,
                            (lua_class_propfunc_t) luaA_ewindow_set_opacity,
                            (lua_class_propfunc_t) luaA_ewindow_get_opacity,
                            (lua_class_propfunc_t) luaA_ewindow_set_opacity);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_BORDER_COLOR,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_color,
                            (lua_class_propfunc_t) luaA_ewindow_get_border_color,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_color);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_BORDER_WIDTH,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_width,
                            (lua_class_propfunc_t) luaA_ewindow_get_border_width,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_width);
    luaA_class_add_property((lua_class_t *) &ewindow_class, A_TK_STICKY,
                            (lua_class_propfunc_t) luaA_ewindow_set_sticky,
                            (lua_class_propfunc_t) luaA_ewindow_get_sticky,
                            (lua_class_propfunc_t) luaA_ewindow_set_sticky);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
