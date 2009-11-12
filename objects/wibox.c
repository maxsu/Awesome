/*
 * wibox.c - wibox functions
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

#include <xcb/shape.h>

#include "awesome.h"
#include "screen.h"
#include "wibox.h"
#include "objects/client.h"
#include "screen.h"
#include "xwindow.h"
#include "luaa.h"
#include "ewmh.h"
#include "bma.h"
#include "objects/tag.h"
#include "common/xcursor.h"
#include "common/xutil.h"

LUA_OBJECT_FUNCS((lua_class_t *) &wibox_class, wibox_t, wibox)

/** Destroy all X resources of a wibox.
 * \param w The wibox to wipe.
 */
static void
wibox_wipe(wibox_t *w)
{
    if(w->window)
    {
        DO_WITH_BMA(xcb_destroy_window(_G_connection, w->window));
        w->window = XCB_NONE;
    }
    draw_context_wipe(&w->ctx);
}

void
wibox_unref_simplified(wibox_t **item)
{
    luaA_object_unref(globalconf.L, *item);
}

static bool
wibox_check_have_shape(void)
{
    const xcb_query_extension_reply_t *reply = xcb_get_extension_data(_G_connection, &xcb_shape_id);

    /* We don't need a specific version of SHAPE, no version check required */
    if (!reply || !reply->present)
        return false;

    return true;
}

static void
shape_update(xcb_window_t win, xcb_shape_kind_t kind, image_t *image, int offset)
{
    xcb_pixmap_t shape;

    if(image)
        shape = image_to_1bit_pixmap(image, win);
    else
        /* Reset the shape */
        shape = XCB_NONE;

    xcb_shape_mask(_G_connection, XCB_SHAPE_SO_SET, kind,
                   win, offset, offset, shape);

    if (shape != XCB_NONE)
        xcb_free_pixmap(_G_connection, shape);
}

/** Update the window's shape.
 * \param wibox The simple window whose shape should be updated.
 */
static void
wibox_shape_update(wibox_t *wibox)
{
    if(wibox->window == XCB_NONE)
        return;

    if(!wibox_check_have_shape())
    {
        static bool warned = false;
        if(!warned)
            warn("The X server doesn't have the SHAPE extension; "
                    "can't change window's shape");
        warned = true;
        return;
    }

    shape_update(wibox->window, XCB_SHAPE_SK_CLIP, wibox->shape_clip, 0);
    shape_update(wibox->window, XCB_SHAPE_SK_BOUNDING, wibox->shape_bounding, - wibox->border_width);

    wibox->need_shape_update = false;
}

static void
wibox_draw_context_update(wibox_t *w)
{
    xcolor_t fg = w->ctx.fg, bg = w->ctx.bg;

    draw_context_wipe(&w->ctx);

    draw_context_init(&w->ctx,
                      w->geometry.width,
                      w->geometry.height,
                      &fg, &bg);
}

static int
luaA_wibox_draw_context_update(lua_State *L)
{
    wibox_draw_context_update(luaA_checkudata(L, 1, (lua_class_t *) &wibox_class));
    return 0;
}

/** Initialize a wibox.
 * \param w The wibox to initialize.
 */
static void
wibox_init(wibox_t *w)
{
    xcb_screen_t *s = globalconf.screen;

    w->window = xcb_generate_id(_G_connection);
    xcb_create_window(_G_connection, s->root_depth, w->window, s->root,
                      w->geometry.x, w->geometry.y,
                      w->geometry.width, w->geometry.height,
                      w->border_width, XCB_COPY_FROM_PARENT, s->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_BIT_GRAVITY
                      | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                      (const uint32_t [])
                      {
                          w->ctx.bg.pixel,
                          w->border_color.pixel,
                          XCB_GRAVITY_NORTH_WEST,
                          1,
                          WIBOX_SELECT_INPUT_EVENT_MASK
                      });

    /* Update draw context physical screen, important for Zaphod. */
    wibox_draw_context_update(w);

    luaA_object_push(globalconf.L, w);
    luaA_object_emit_signal(globalconf.L, -1, "property::window", 0);
    lua_pop(globalconf.L, 1);

    wibox_shape_update(w);

    xwindow_buttons_grab(w->window, &w->buttons);
}

/** Refresh the window content by copying its pixmap data to its window.
 * \param w The wibox to refresh.
 */
static inline void
wibox_refresh_pixmap(wibox_t *w)
{
    wibox_refresh_pixmap_partial(w, 0, 0, w->geometry.width, w->geometry.height);
}

/** Refresh the window content by copying its pixmap data to its window.
 * \param wibox The wibox to refresh.
 * \param x The copy starting point x component.
 * \param y The copy starting point y component.
 * \param w The copy width from the x component.
 * \param h The copy height from the y component.
 */
void
wibox_refresh_pixmap_partial(wibox_t *wibox,
                             int16_t x, int16_t y,
                             uint16_t w, uint16_t h)
{
    if(wibox->ctx.pixmap && wibox->window)
        xcb_copy_area(_G_connection, wibox->ctx.pixmap,
                      wibox->window, globalconf.gc,  x, y, x, y,
                      w, h);
}

static void
wibox_map(wibox_t *wibox)
{
    /* Map the wibox */
    DO_WITH_BMA(xcb_map_window(_G_connection, wibox->window));
    /* We must make sure the wibox does not display garbage */
    wibox->need_update = true;
}

/** Get a wibox by its window.
 * \param win The window id.
 * \return A wibox if found, NULL otherwise.
 */
wibox_t *
wibox_getbywin(xcb_window_t win)
{
    foreach(w, globalconf.wiboxes)
        if((*w)->window == win)
            return *w;
    return NULL;
}

/** Draw a wibox.
 * \param wibox The wibox to draw.
 */
static void
wibox_draw(wibox_t *wibox)
{
    if(wibox->visible)
    {
        wibox_refresh_pixmap(wibox);
        wibox->need_update = false;
    }
}

/** Refresh all wiboxes.
 */
void
wibox_refresh(void)
{
    foreach(w, globalconf.wiboxes)
    {
        if((*w)->need_shape_update)
            wibox_shape_update(*w);
        if((*w)->need_update)
            wibox_draw(*w);
    }
}

/** Set a wibox visible or not.
 * \param L The Lua VM state.
 * \param udx The wibox.
 * \param v The visible value.
 */
static void
wibox_set_visible(lua_State *L, int udx, bool v)
{
    wibox_t *wibox = luaA_checkudata(L, udx, (lua_class_t *) &wibox_class);
    if(v != wibox->visible)
    {
        wibox->visible = v;

        if(wibox->screen)
        {
            if(wibox->visible)
                wibox_map(wibox);
            else
                DO_WITH_BMA(xcb_unmap_window(_G_connection, wibox->window));
        }

        luaA_object_emit_signal(L, udx, "property::visible", 0);
    }
}

/** Remove a wibox from a screen.
 * \param L The Lua VM state.
 * \param udx Wibox to detach from screen.
 */
static void
wibox_detach(lua_State *L, int udx)
{
    wibox_t *wibox = luaA_checkudata(L, udx, (lua_class_t *) &wibox_class);
    if(wibox->screen)
    {
        bool v;

        /* save visible state */
        v = wibox->visible;
        wibox->visible = false;
        /* restore visibility */
        wibox->visible = v;

        wibox_wipe(wibox);

        /* XXX this may be done in wipe_resources, but since wipe_resources is
         * called via __gc, not sure it would work */
        luaA_object_emit_signal(globalconf.L, udx, "property::window", 0);

        foreach(item, globalconf.wiboxes)
            if(*item == wibox)
            {
                wibox_array_remove(&globalconf.wiboxes, item);
                break;
            }

        if(strut_has_value(&wibox->strut))
        {
            lua_pushlightuserdata(L, wibox->screen);
            luaA_object_emit_signal(L, -1, "property::workarea", 0);
            lua_pop(L, 1);
        }

        wibox->screen = NULL;
        wibox->parent = NULL;
        luaA_object_emit_signal(L, udx, "property::screen", 0);

        luaA_object_unref(globalconf.L, wibox);
    }
}

/** Attach a wibox that is on top of the stack.
 * \param L The Lua VM state.
 * \param udx The wibox to attach.
 * \param s The screen to attach the wibox to.
 */
static void
wibox_attach(lua_State *L, int udx, screen_t *s)
{
    /* duplicate wibox */
    lua_pushvalue(L, udx);
    /* ref it */
    wibox_t *wibox = luaA_object_ref_class(globalconf.L, -1, (lua_class_t *) &wibox_class);

    wibox_detach(L, udx);

    /* Set the wibox screen */
    wibox->screen = s;
    wibox->parent = globalconf.screens.tab[0].root;
    stack_ewindow_raise(globalconf.L, udx);

    /* Check that the wibox coordinates matches the screen. */
    screen_t *cscreen =
        screen_getbycoord(wibox->geometry.x, wibox->geometry.y);

    /* If it does not match, move it to the screen coordinates */
    if(cscreen != wibox->screen)
        window_set_geometry(L, udx, (area_t) { .x = s->geometry.x,
                                               .y = s->geometry.y,
                                               .width = wibox->geometry.width,
                                               .height = wibox->geometry.height });

    wibox_array_append(&globalconf.wiboxes, wibox);

    wibox_init(wibox);

    xwindow_set_cursor(wibox->window,
                       xcursor_new(_G_connection, xcursor_font_fromstr(wibox->cursor)));

    if(wibox->opacity != -1)
        xwindow_set_opacity(wibox->window, wibox->opacity);

    if(wibox->visible)
        wibox_map(wibox);
    else
        wibox->need_update = true;

    luaA_object_emit_signal(L, udx, "property::screen", 0);

    if(strut_has_value(&wibox->strut))
    {
        lua_pushlightuserdata(L, wibox->screen);
        luaA_object_emit_signal(L, -1, "property::workarea", 0);
        lua_pop(L, 1);
    }
}

static int
luaA_wibox_need_update(lua_State *L)
{
    wibox_t *wibox = luaA_checkudata(L, 1, (lua_class_t *) &wibox_class);
    wibox->need_update = true;
    return 0;
}

/** Create a new wibox.
 * \param L The Lua VM state.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wibox_new(lua_State *L)
{
    luaA_class_new(L, (lua_class_t *) &wibox_class);

    wibox_t *w = luaA_checkudata(L, -1, (lua_class_t *) &wibox_class);

    if(!w->ctx.fg.initialized)
        w->ctx.fg = globalconf.colors.fg;

    if(!w->ctx.bg.initialized)
        w->ctx.bg = globalconf.colors.bg;

    w->visible = true;

    if(!w->opacity)
        w->opacity = -1;

    if(!w->cursor)
        w->cursor = a_strdup("left_ptr");

    if(!w->geometry.width)
        w->geometry.width = 1;

    if(!w->geometry.height)
        w->geometry.height = 1;

    w->movable = w->resizable = true;

    return 1;
}

static LUA_OBJECT_EXPORT_PROPERTY(wibox, wibox_t, visible, lua_pushboolean)
static LUA_OBJECT_EXPORT_PROPERTY(wibox, wibox_t, bg_image, luaA_object_push)
static LUA_OBJECT_EXPORT_PROPERTY(wibox, wibox_t, shape_clip, luaA_object_push)
static LUA_OBJECT_EXPORT_PROPERTY(wibox, wibox_t, shape_bounding, luaA_object_push)

/** Set the wibox foreground color.
 * \param L The Lua VM state.
 * \param wibox The wibox object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wibox_set_fg(lua_State *L, wibox_t *wibox)
{
    size_t len;
    const char *buf = luaL_checklstring(L, -1, &len);
    if(xcolor_init_reply(xcolor_init_unchecked(&wibox->ctx.fg, buf, len)))
        wibox->need_update = true;
    luaA_object_emit_signal(L, -3, "property::fg", 0);
    return 0;
}

/** Get the wibox foreground color.
 * \param L The Lua VM state.
 * \param wibox The wibox object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wibox_get_fg(lua_State *L, wibox_t *wibox)
{
    return luaA_pushxcolor(L, wibox->ctx.fg);
}

/** Set the wibox background color.
 * \param L The Lua VM state.
 * \param wibox The wibox object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wibox_set_bg(lua_State *L, wibox_t *wibox)
{
    size_t len;
    const char *buf = luaL_checklstring(L, -1, &len);
    if(xcolor_init_reply(xcolor_init_unchecked(&wibox->ctx.bg, buf, len)))
    {
        uint32_t mask = XCB_CW_BACK_PIXEL;
        uint32_t values[] = { wibox->ctx.bg.pixel };

        wibox->need_update = true;

        if (wibox->window != XCB_NONE)
            xcb_change_window_attributes(_G_connection,
                                         wibox->window,
                                         mask,
                                         values);
    }
    luaA_object_emit_signal(L, -3, "property::bg", 0);
    return 0;
}

/** Get the wibox background color.
 * \param L The Lua VM state.
 * \param wibox The wibox object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wibox_get_bg(lua_State *L, wibox_t *wibox)
{
    return luaA_pushxcolor(L, wibox->ctx.bg);
}

/** Set the wibox background image.
 * \param L The Lua VM state.
 * \param wibox The wibox object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wibox_set_bg_image(lua_State *L, wibox_t *wibox)
{
    luaA_checkudata(L, -1, &image_class);
    luaA_object_unref_item(L, -3, wibox->bg_image);
    wibox->bg_image = luaA_object_ref_item(L, -3, -1);
    wibox->need_update = true;
    luaA_object_emit_signal(L, -2, "property::bg_image", 0);
    return 0;
}

/** Set the wibox screen.
 * \param L The Lua VM state.
 * \param wibox The wibox object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wibox_set_screen(lua_State *L, wibox_t *wibox)
{
    if(lua_isnil(L, -1))
        wibox_detach(L, -3);
    else
    {
        int screen = luaL_checknumber(L, -1) - 1;
        luaA_checkscreen(screen);
        if(!wibox->screen || screen != screen_array_indexof(&globalconf.screens, wibox->screen))
            wibox_attach(L, -3, &globalconf.screens.tab[screen]);
    }
    return 0;
}

/** Set the wibox visibility.
 * \param L The Lua VM state.
 * \param wibox The wibox object.
 * \return The number of elements pushed on stack.
 */
static int
luaA_wibox_set_visible(lua_State *L, wibox_t *wibox)
{
    wibox_set_visible(L, -3, luaA_checkboolean(L, -1));
    return 0;
}

static int
luaA_wibox_set_shape_bounding(lua_State *L, wibox_t *wibox)
{
    luaA_checkudata(L, -1, &image_class);
    luaA_object_unref_item(L, -3, wibox->shape_bounding);
    wibox->shape_bounding = luaA_object_ref_item(L, -3, -1);
    wibox->need_shape_update = true;
    luaA_object_emit_signal(L, -2, "property::shape_bounding", 0);
    return 0;
}

static int
luaA_wibox_set_shape_clip(lua_State *L, wibox_t *wibox)
{
    luaA_checkudata(L, -1, &image_class);
    luaA_object_unref_item(L, -3, wibox->shape_clip);
    wibox->shape_clip = luaA_object_ref_item(L, -3, -1);
    wibox->need_shape_update = true;
    luaA_object_emit_signal(L, -2, "property::shape_clip", 0);
    return 0;
}

void
wibox_class_setup(lua_State *L)
{
    static const struct luaL_reg wibox_methods[] =
    {
        LUA_CLASS_METHODS(wibox)
        { NULL, NULL }
    };

    static const struct luaL_reg wibox_module_meta[] =
    {
        { "__call", luaA_wibox_new },
        { NULL, NULL },
    };

    luaA_class_setup(L, (lua_class_t *) &wibox_class, "wibox", (lua_class_t *) &ewindow_class,
                     (lua_class_allocator_t) wibox_new,
                     (lua_class_collector_t) wibox_wipe,
                     NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     wibox_methods, wibox_module_meta, NULL);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_VISIBLE,
                            (lua_class_propfunc_t) luaA_wibox_set_visible,
                            (lua_class_propfunc_t) luaA_wibox_get_visible,
                            (lua_class_propfunc_t) luaA_wibox_set_visible);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_SCREEN,
                            NULL,
                            (lua_class_propfunc_t) luaA_window_get_screen,
                            (lua_class_propfunc_t) luaA_wibox_set_screen);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_FG,
                            (lua_class_propfunc_t) luaA_wibox_set_fg,
                            (lua_class_propfunc_t) luaA_wibox_get_fg,
                            (lua_class_propfunc_t) luaA_wibox_set_fg);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_BG,
                            (lua_class_propfunc_t) luaA_wibox_set_bg,
                            (lua_class_propfunc_t) luaA_wibox_get_bg,
                            (lua_class_propfunc_t) luaA_wibox_set_bg);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_BG_IMAGE,
                            (lua_class_propfunc_t) luaA_wibox_set_bg_image,
                            (lua_class_propfunc_t) luaA_wibox_get_bg_image,
                            (lua_class_propfunc_t) luaA_wibox_set_bg_image);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_SHAPE_BOUNDING,
                            (lua_class_propfunc_t) luaA_wibox_set_shape_bounding,
                            (lua_class_propfunc_t) luaA_wibox_get_shape_bounding,
                            (lua_class_propfunc_t) luaA_wibox_set_shape_bounding);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_SHAPE_CLIP,
                            (lua_class_propfunc_t) luaA_wibox_set_shape_clip,
                            (lua_class_propfunc_t) luaA_wibox_get_shape_clip,
                            (lua_class_propfunc_t) luaA_wibox_set_shape_clip);

    luaA_class_connect_signal(L, (lua_class_t *) &wibox_class, "property::border_width", luaA_wibox_need_update);
    luaA_class_connect_signal(L, (lua_class_t *) &wibox_class, "property::geometry", luaA_wibox_need_update);
    luaA_class_connect_signal(L, (lua_class_t *) &wibox_class, "property::width", luaA_wibox_draw_context_update);
    luaA_class_connect_signal(L, (lua_class_t *) &wibox_class, "property::height", luaA_wibox_draw_context_update);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
