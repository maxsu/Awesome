/*
 * ewindow.c - Extended window object
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

#include "awesome.h"
#include "luaa.h"
#include "xwindow.h"
#include "screen.h"
#include "objects/window.h"
#include "objects/tag.h"
#include "common/luaobject.h"
#include "common/xutil.h"

LUA_OBJECT_SIGNAL_FUNCS(ewindow, ewindow_t)

bool
ewindow_isvisible(ewindow_t *ewindow)
{
    if(ewindow->minimized || !ewindow->visible)
        return false;

    if(ewindow->sticky || ewindow->type == EWINDOW_TYPE_DESKTOP)
        return true;

    foreach(tag, _G_tags)
        if(tag_get_selected(*tag) && ewindow_is_tagged(ewindow, *tag))
            return true;

    return false;
}

ewindow_t *
ewindow_getbywin(xcb_window_t win)
{
    ewindow_t **w = ewindow_binary_array_lookup(&_G_ewindows, &(ewindow_t) { .window = win });
    return w ? *w : NULL;
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
        luaA_object_emit_signal(L, 1, "property::struts", 0);
        if(ewindow_isvisible(ewindow))
            screen_emit_signal(L, screen_getbycoord(ewindow->geometry.x, ewindow->geometry.y),
                               "property::workarea", 0);
    }

    return luaA_pushstrut(L, ewindow->strut);
}

/** Set a ewindow minimized, or not.
 * \param L The Lua VM state.
 * \param ewindow The ewindow.
 * \param s Set or not the ewindow minimized.
 */
void
ewindow_set_minimized(lua_State *L, ewindow_t *ewindow, bool s)
{
    if(ewindow->minimized != s)
    {
        ewindow->minimized = s;
        if(s)
            xwindow_set_state(ewindow->window, XCB_WM_STATE_ICONIC);
        else
            xwindow_set_state(ewindow->window, XCB_WM_STATE_NORMAL);
        if(strut_has_value(&ewindow->strut))
            screen_emit_signal(L, screen_getbycoord(ewindow->geometry.x, ewindow->geometry.y),
                               "property::workarea", 0);
        ewindow_emit_signal(L, ewindow, "property::minimized", 0);
    }
}

/** Set a ewindow fullscreen, or not.
 * \param L The Lua VM state.
 * \param ewindow The ewindow.
 * \param s Set or not the ewindow fullscreen.
 */
void
ewindow_set_fullscreen(lua_State *L, ewindow_t *ewindow, bool s)
{
    if(ewindow->fullscreen != s)
    {
        /* become fullscreen! */
        if(s)
        {
            /* remove any max state */
            ewindow_set_maximized_horizontal(L, ewindow, false);
            ewindow_set_maximized_vertical(L, ewindow, false);
            /* You can only be part of one of the special layers. */
            ewindow_set_below(L, ewindow, false);
            ewindow_set_above(L, ewindow, false);
            ewindow_set_ontop(L, ewindow, false);
        }
        lua_pushboolean(L, s);
        ewindow_emit_signal(L, ewindow, "request::fullscreen", 1);
        ewindow->fullscreen = s;
        ewindow_emit_signal(L, ewindow, "property::fullscreen", 0);
    }
}

/** Set a ewindow horizontally|vertically maximized.
 * \param L The Lua VM state.
 * \param ewindow The ewindow.
 * \param s The maximized status.
 */
#define DO_FUNCTION_CLIENT_MAXIMIZED(type) \
    void \
    ewindow_set_maximized_##type(lua_State *L, ewindow_t *ewindow, bool s) \
    { \
        if(ewindow->maximized_##type != s) \
        { \
            if(s) \
                ewindow_set_fullscreen(L, ewindow, false); \
            lua_pushboolean(L, s); \
            ewindow_emit_signal(L, ewindow, "request::maximized_" #type, 1); \
            ewindow->maximized_##type = s; \
            ewindow_emit_signal(L, ewindow, "property::maximized_" #type, 0); \
        } \
    }
DO_FUNCTION_CLIENT_MAXIMIZED(vertical)
DO_FUNCTION_CLIENT_MAXIMIZED(horizontal)
#undef DO_FUNCTION_CLIENT_MAXIMIZED

/** Set a ewindow above, or not.
 * \param L The Lua VM state.
 * \param ewindow The ewindow.
 * \param s Set or not the ewindow above.
 */
void
ewindow_set_above(lua_State *L, ewindow_t *ewindow, bool s)
{
    if(ewindow->above != s)
    {
        /* You can only be part of one of the special layers. */
        if(s)
        {
            ewindow_set_below(L, ewindow, false);
            ewindow_set_ontop(L, ewindow, false);
            ewindow_set_fullscreen(L, ewindow, false);
        }
        ewindow->above = s;
        ewindow_emit_signal(L, ewindow, "property::above", 0);
    }
}

/** Set a ewindow below, or not.
 * \param L The Lua VM state.
 * \param ewindow The ewindow.
 * \param s Set or not the ewindow below.
 */
void
ewindow_set_below(lua_State *L, ewindow_t *ewindow, bool s)
{
    if(ewindow->below != s)
    {
        /* You can only be part of one of the special layers. */
        if(s)
        {
            ewindow_set_above(L, ewindow, false);
            ewindow_set_ontop(L, ewindow, false);
            ewindow_set_fullscreen(L, ewindow, false);
        }
        ewindow->below = s;
        ewindow_emit_signal(L, ewindow, "property::below", 0);
    }
}

/** Set a ewindow ontop, or not.
 * \param L The Lua VM state.
 * \param ewindow The ewindow.
 * \param s Set or not the ewindow ontop attribute.
 */
void
ewindow_set_ontop(lua_State *L, ewindow_t *ewindow, bool s)
{
    if(ewindow->ontop != s)
    {
        /* You can only be part of one of the special layers. */
        if(s)
        {
            ewindow_set_above(L, ewindow, false);
            ewindow_set_below(L, ewindow, false);
            ewindow_set_fullscreen(L, ewindow, false);
        }
        ewindow->ontop = s;
        ewindow_emit_signal(L, ewindow, "property::ontop", 0);
    }
}

/** Set an ewindow opacity.
 * \param L The Lua VM state.
 * \param ewindow The ewindow.
 * \param opacity The opacity value.
 */
void
ewindow_set_opacity(lua_State *L, ewindow_t *ewindow, double opacity)
{
    if(ewindow->opacity != opacity)
    {
        ewindow->opacity = opacity;
        xwindow_set_opacity(ewindow->window, opacity);
        ewindow_emit_signal(L, ewindow, "property::opacity", 0);
    }
}

int
luaA_ewindow_get_type(lua_State *L, ewindow_t *ewindow)
{
    switch(ewindow->type)
    {
      case EWINDOW_TYPE_DESKTOP:
        lua_pushliteral(L, "desktop");
        break;
      case EWINDOW_TYPE_DOCK:
        lua_pushliteral(L, "dock");
        break;
      case EWINDOW_TYPE_SPLASH:
        lua_pushliteral(L, "splash");
        break;
      case EWINDOW_TYPE_DIALOG:
        lua_pushliteral(L, "dialog");
        break;
      case EWINDOW_TYPE_MENU:
        lua_pushliteral(L, "menu");
        break;
      case EWINDOW_TYPE_TOOLBAR:
        lua_pushliteral(L, "toolbar");
        break;
      case EWINDOW_TYPE_UTILITY:
        lua_pushliteral(L, "utility");
        break;
      case EWINDOW_TYPE_DROPDOWN_MENU:
        lua_pushliteral(L, "dropdown_menu");
        break;
      case EWINDOW_TYPE_POPUP_MENU:
        lua_pushliteral(L, "popup_menu");
        break;
      case EWINDOW_TYPE_TOOLTIP:
        lua_pushliteral(L, "tooltip");
        break;
      case EWINDOW_TYPE_NOTIFICATION:
        lua_pushliteral(L, "notification");
        break;
      case EWINDOW_TYPE_COMBO:
        lua_pushliteral(L, "combo");
        break;
      case EWINDOW_TYPE_DND:
        lua_pushliteral(L, "dnd");
        break;
      case EWINDOW_TYPE_NORMAL:
        lua_pushliteral(L, "normal");
        break;
    }
    return 1;
}

static int
luaA_ewindow_set_type(lua_State *L, ewindow_t *ewindow)
{
    size_t len;
    const char *value = luaL_checklstring(L, -1, &len);

    switch(a_tokenize(value, len))
    {
      case A_TK_DESKTOP:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_DESKTOP);
        break;
      case A_TK_DOCK:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_DOCK);
        break;
      case A_TK_SPLASH:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_SPLASH);
        break;
      case A_TK_DIALOG:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_DIALOG);
        break;
      case A_TK_MENU:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_MENU);
        break;
      case A_TK_TOOLBAR:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_TOOLBAR);
        break;
      case A_TK_UTILITY:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_UTILITY);
        break;
      case A_TK_DROPDOWN_MENU:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_DROPDOWN_MENU);
        break;
      case A_TK_POPUP_MENU:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_POPUP_MENU);
        break;
      case A_TK_TOOLTIP:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_TOOLTIP);
        break;
      case A_TK_NOTIFICATION:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_NOTIFICATION);
        break;
      case A_TK_COMBO:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_COMBO);
        break;
      case A_TK_DND:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_DND);
        break;
      case A_TK_NORMAL:
        ewindow_set_type(L, ewindow, EWINDOW_TYPE_NORMAL);
        break;
      default:
        break;
    }

    return 0;
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
        ewindow_set_opacity(L, ewindow, -1);
    else
    {
        double d = luaL_checknumber(L, -1);
        if(d >= 0 && d <= 1)
            ewindow_set_opacity(L, ewindow, d);
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
 * \param ewindow The ewindow.
 * \param width The border width.
 */
void
ewindow_set_border_width(lua_State *L, ewindow_t *ewindow, int width)
{
    if(width == ewindow->border_width || width < 0)
        return;

    xcb_configure_window(_G_connection, ewindow->window,
                         XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         (uint32_t[]) { width });

    ewindow->border_width = width;

    ewindow_emit_signal(L, ewindow, "property::border_width", 0);
}

static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, border_width, luaL_checknumber)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, ontop, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, below, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, above, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, sticky, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, minimized, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, maximized_vertical, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, maximized_horizontal, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, modal, luaA_checkboolean)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(ewindow, ewindow_t, fullscreen, luaA_checkboolean)

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
            luaA_object_push(L, *tag);
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
        luaA_object_push(L, *tag);
        lua_rawseti(L, -2, ++i);
    }

    return 1;
}

LUA_OBJECT_DO_SET_PROPERTY_FUNC(ewindow, ewindow_t, sticky)
LUA_OBJECT_DO_SET_PROPERTY_FUNC(ewindow, ewindow_t, modal)
LUA_OBJECT_DO_SET_PROPERTY_FUNC(ewindow, ewindow_t, type)

static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, border_color, luaA_pushxcolor)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, border_width, lua_pushnumber)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, sticky, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, minimized, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, fullscreen, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, modal, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, ontop, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, above, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, below, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, maximized_horizontal, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(ewindow, ewindow_t, maximized_vertical, lua_pushboolean)

static void
ewindow_init(lua_State *L, ewindow_t *ewindow)
{
    ewindow->opacity = -1;
}

LUA_CLASS_FUNCS(ewindow, (lua_class_t *) &ewindow_class)

void
ewindow_class_setup(lua_State *L)
{
    static const struct luaL_reg ewindow_methods[] =
    {
        LUA_CLASS_METHODS(ewindow)
        { "struts", luaA_ewindow_struts },
        { "tags", luaA_ewindow_tags },
        { NULL, NULL }
    };

    luaA_class_setup(L, (lua_class_t *) &ewindow_class, "ewindow", &window_class,
                     sizeof(ewindow_t), (lua_class_initializer_t) ewindow_init,
                     NULL, NULL,
                     ewindow_methods, NULL, NULL);

    luaA_class_add_property((lua_class_t *) &ewindow_class, "opacity",
                            (lua_class_propfunc_t) luaA_ewindow_set_opacity,
                            (lua_class_propfunc_t) luaA_ewindow_get_opacity,
                            (lua_class_propfunc_t) luaA_ewindow_set_opacity);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "border_color",
                            (lua_class_propfunc_t) luaA_ewindow_set_border_color,
                            (lua_class_propfunc_t) luaA_ewindow_get_border_color,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_color);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "border_width",
                            (lua_class_propfunc_t) luaA_ewindow_set_border_width,
                            (lua_class_propfunc_t) luaA_ewindow_get_border_width,
                            (lua_class_propfunc_t) luaA_ewindow_set_border_width);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "sticky",
                            (lua_class_propfunc_t) luaA_ewindow_set_sticky,
                            (lua_class_propfunc_t) luaA_ewindow_get_sticky,
                            (lua_class_propfunc_t) luaA_ewindow_set_sticky);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "ontop",
                            (lua_class_propfunc_t) luaA_ewindow_set_ontop,
                            (lua_class_propfunc_t) luaA_ewindow_get_ontop,
                            (lua_class_propfunc_t) luaA_ewindow_set_ontop);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "above",
                            (lua_class_propfunc_t) luaA_ewindow_set_above,
                            (lua_class_propfunc_t) luaA_ewindow_get_above,
                            (lua_class_propfunc_t) luaA_ewindow_set_above);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "below",
                            (lua_class_propfunc_t) luaA_ewindow_set_below,
                            (lua_class_propfunc_t) luaA_ewindow_get_below,
                            (lua_class_propfunc_t) luaA_ewindow_set_below);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "minimized",
                            (lua_class_propfunc_t) luaA_ewindow_set_minimized,
                            (lua_class_propfunc_t) luaA_ewindow_get_minimized,
                            (lua_class_propfunc_t) luaA_ewindow_set_minimized);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "fullscreen",
                            (lua_class_propfunc_t) luaA_ewindow_set_fullscreen,
                            (lua_class_propfunc_t) luaA_ewindow_get_fullscreen,
                            (lua_class_propfunc_t) luaA_ewindow_set_fullscreen);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "modal",
                            (lua_class_propfunc_t) luaA_ewindow_set_modal,
                            (lua_class_propfunc_t) luaA_ewindow_get_modal,
                            (lua_class_propfunc_t) luaA_ewindow_set_modal);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "maximized_horizontal",
                            (lua_class_propfunc_t) luaA_ewindow_set_maximized_horizontal,
                            (lua_class_propfunc_t) luaA_ewindow_get_maximized_horizontal,
                            (lua_class_propfunc_t) luaA_ewindow_set_maximized_horizontal);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "maximized_vertical",
                            (lua_class_propfunc_t) luaA_ewindow_set_maximized_vertical,
                            (lua_class_propfunc_t) luaA_ewindow_get_maximized_vertical,
                            (lua_class_propfunc_t) luaA_ewindow_set_maximized_vertical);
    luaA_class_add_property((lua_class_t *) &ewindow_class, "type",
                            (lua_class_propfunc_t) luaA_ewindow_set_type,
                            (lua_class_propfunc_t) luaA_ewindow_get_type,
                            (lua_class_propfunc_t) luaA_ewindow_set_type);

    ewindow_class.isvisible = (lua_interface_window_isvisible_t) ewindow_isvisible;
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
