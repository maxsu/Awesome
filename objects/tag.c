/*
 * tag.c - tag management
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

#include "screen.h"
#include "tag.h"
#include "objects/ewindow.h"
#include "luaa.h"

LUA_OBJECT_FUNCS(&tag_class, tag_t, tag)

static void
tag_wipe(tag_t *tag)
{
    ewindow_array_wipe(&tag->windows);
    p_delete(&tag->name);
}

OBJECT_EXPORT_PROPERTY(tag, tag_t, selected)
OBJECT_EXPORT_PROPERTY(tag, tag_t, name)
static LUA_OBJECT_EXPORT_PROPERTY(tag, tag_t, name, lua_pushstring)
static LUA_OBJECT_EXPORT_PROPERTY(tag, tag_t, selected, lua_pushboolean)
static LUA_OBJECT_DO_SET_PROPERTY_FUNC(tag, &tag_class, tag_t, selected)
static LUA_OBJECT_DO_LUA_SET_PROPERTY_FUNC(tag, tag_t, selected, luaA_checkboolean)

static int
luaA_tag_get_attached(lua_State *L, tag_t *tag)
{
    lua_pushboolean(L, tag_array_find(&_G_tags, tag) != NULL);
    return 1;
}

static int
luaA_tag_set_attached(lua_State *L, tag_t *tag)
{
    bool attach = luaA_checkboolean(L, 3);
    tag_t **tag_index = tag_array_find(&_G_tags, tag);

    if(attach)
    {
        /* Tag not already attached? */
        if(!tag_index)
        {
            tag_array_append(&_G_tags, luaA_object_ref(L, 1));
            luaA_object_emit_signal(L, 1, "property::attached", 0);
        }
    }
    else if(tag_index)
    {
        tag_array_remove(&_G_tags, tag_index);
        luaA_object_emit_signal(L, 1, "property::attached", 0);
    }

    return 0;
}

static void
tag_ewindow_emit_signal(lua_State *L, int tidx, int widx, const char *signame)
{
    /* transform indexes in absolute value */
    tidx = luaA_absindex(L, tidx);
    widx = luaA_absindex(L, widx);

    /* emit signal on window, with new tag as argument */
    lua_pushvalue(L, tidx);
    luaA_object_emit_signal(L, widx, signame, 1);

    /* now do the opposite! */
    lua_pushvalue(L, widx);
    luaA_object_emit_signal(L, tidx, signame, 1);
}

/** Tag an ewindow.
 * \param L The Lua VM state.
 * \param widx The ewindow index on the stack.
 * \param tidx The tag index on the stack.
 */
void
tag_ewindow(lua_State *L, int widx, int tidx)
{
    tag_t *tag = luaA_checkudata(L, tidx, &tag_class);
    ewindow_t *ewindow = luaA_checkudata(L, widx, (lua_class_t *) &ewindow_class);

    /* don't tag twice */
    if(ewindow_is_tagged(ewindow, tag))
        return;

    /* Reference the ewindow in the tag */
    lua_pushvalue(L, widx);
    ewindow_array_append(&tag->windows, luaA_object_ref_item(L, tidx, -1));
    /* Reference the tag in the window */
    lua_pushvalue(L, tidx);
    tag_array_append(&ewindow->tags, luaA_object_ref_item(L, widx, -1));

    tag_ewindow_emit_signal(L, tidx, widx, "tagged");
}

/** Untag a window with specified tag.
 * \param L The Lua VM state.
 * \param widx The window index on the stack.
 * \param tidx The tag index on the stack.
 */
void
untag_ewindow(lua_State *L, int widx, int tidx)
{
    tag_t *tag = luaA_checkudata(L, tidx, &tag_class);
    ewindow_t *ewindow = luaA_checkudata(L, widx, (lua_class_t *) &ewindow_class);

    ewindow_array_find_and_remove(&tag->windows, ewindow);
    tag_array_find_and_remove(&ewindow->tags, tag);
    /* Unref ewindow from tag */
    luaA_object_unref_item(L, tidx, ewindow);
    /* Unref tag from ewindow */
    luaA_object_unref_item(L, widx, tag);
    tag_ewindow_emit_signal(L, tidx, widx, "untagged");
}

/** Check if an ewindow is tagged with the specified tag.
 * \param ewindow The ewindow.
 * \param tag The tag.
 * \return True if the ewindow is tagged with the tag, false otherwise.
 */
bool
ewindow_is_tagged(ewindow_t *ewindow, tag_t *tag)
{
    foreach(w, tag->windows)
        if(*w == ewindow)
            return true;

    return false;
}

/** Get the index of the first selected tag.
 * \return Its index.
 */
int
tags_get_first_selected_index(void)
{
    foreach(tag, _G_tags)
        if((*tag)->selected)
            return tag_array_indexof(&_G_tags, tag);
    return 0;
}

/** Set a tag to be the only one viewed.
 * \param L The Lua VM state.
 * \param target the tag to see
 */
static void
tag_view_only(lua_State *L, tag_t *target)
{
    if(target)
        foreach(tag, _G_tags)
        {
            luaA_object_push(L, *tag);
            tag_set_selected(L, -1, *tag == target);
            lua_pop(L, 1);
        }
}

/** View only a tag, selected by its index.
 * \param L The Lua VM state.
 * \param dindex The index.
 */
void
tag_view_only_byindex(lua_State *L, int dindex)
{
    if(dindex < 0 || dindex >= _G_tags.len)
        return;
    tag_view_only(L, _G_tags.tab[dindex]);
}

/** Create a new tag.
 * \param L The Lua VM state.
 * \luastack
 * \lparam A name.
 * \lreturn A new tag object.
 */
static int
luaA_tag_new(lua_State *L)
{
    return luaA_class_new(L, &tag_class);
}

/** Get or set the windows attached to this tag.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_tag_windows(lua_State *L)
{
    tag_t *tag = luaA_checkudata(L, 1, &tag_class);

    if(lua_gettop(L) == 2)
    {
        luaA_checktable(L, 2);
        foreach(window, tag->windows)
        {
            luaA_object_push(L, *window);
            untag_ewindow(L, -1, 1);
            /* remove pushed windows */
            lua_pop(L, 1);
        }
        lua_pushnil(L);
        while(lua_next(L, 2))
        {
            tag_ewindow(L, -1, 1);
            lua_pop(L, 1);
        }
    }

    lua_createtable(L, tag->windows.len, 0);
    for(int i = 0; i < tag->windows.len; i++)
    {
        luaA_object_push(L, tag->windows.tab[i]);
        lua_rawseti(L, -2, i + 1);
    }

    return 1;
}

/** Set the tag name.
 * \param L The Lua VM state.
 * \param tag The tag to name.
 * \return The number of elements pushed on stack.
 */
static int
luaA_tag_set_name(lua_State *L, tag_t *tag)
{
    size_t len;
    const char *buf = luaL_checklstring(L, -1, &len);
    p_delete(&tag->name);
    a_iso2utf8(buf, len, &tag->name, NULL);
    luaA_object_emit_signal(L, -3, "property::name", 0);
    return 0;
}

void
tag_class_setup(lua_State *L)
{
    static const struct luaL_reg tag_methods[] =
    {
        LUA_CLASS_METHODS(tag)
        { "windows", luaA_tag_windows },
        { NULL, NULL }
    };

    static const struct luaL_reg tag_module_meta[] =
    {
        { "__call", luaA_tag_new },
        { NULL, NULL },
    };

    luaA_class_setup(L, &tag_class, "tag", NULL, sizeof(tag_t), NULL,
                     (lua_class_collector_t) tag_wipe,
                     NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     tag_methods, tag_module_meta, NULL);
    luaA_class_add_property(&tag_class, A_TK_NAME,
                            (lua_class_propfunc_t) luaA_tag_set_name,
                            (lua_class_propfunc_t) luaA_tag_get_name,
                            (lua_class_propfunc_t) luaA_tag_set_name);
    luaA_class_add_property(&tag_class, A_TK_SELECTED,
                            (lua_class_propfunc_t) luaA_tag_set_selected,
                            (lua_class_propfunc_t) luaA_tag_get_selected,
                            (lua_class_propfunc_t) luaA_tag_set_selected);
    luaA_class_add_property(&tag_class, A_TK_ATTACHED,
                            (lua_class_propfunc_t) luaA_tag_set_attached,
                            (lua_class_propfunc_t) luaA_tag_get_attached,
                            (lua_class_propfunc_t) luaA_tag_set_attached);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
