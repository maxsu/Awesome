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
                      wibox->window, _G_gc, x, y, x, y,
                      w, h);
}

static void
wibox_unmap(wibox_t *wibox)
{
    /* Map the wibox */
    xcb_unmap_window(_G_connection, wibox->window);
    if(strut_has_value(&wibox->strut))
    {
        lua_pushlightuserdata(globalconf.L, screen_getbycoord(wibox->geometry.x, wibox->geometry.y));
        luaA_object_emit_signal(globalconf.L, -1, "property::workarea", 0);
        lua_pop(globalconf.L, 1);
    }
}

static void
wibox_map(wibox_t *wibox)
{
    /* Map the wibox */
    xcb_map_window(_G_connection, wibox->window);
    /* We must make sure the wibox does not display garbage */
    wibox->need_update = true;
    if(strut_has_value(&wibox->strut))
    {
        lua_pushlightuserdata(globalconf.L, screen_getbycoord(wibox->geometry.x, wibox->geometry.y));
        luaA_object_emit_signal(globalconf.L, -1, "property::workarea", 0);
        lua_pop(globalconf.L, 1);
    }
}

/** Get a wibox by its window.
 * \param win The window id.
 * \return A wibox if found, NULL otherwise.
 */
wibox_t *
wibox_getbywin(xcb_window_t win)
{
    wibox_t **w = wibox_array_lookup(&globalconf.wiboxes, &(wibox_t) { .window = win });
    return w ? *w : NULL;
}

/** Render a wibox content.
 * \param wibox The wibox.
 */
static void
wibox_render(wibox_t *wibox)
{
    if(wibox->ctx.bg.alpha != 0xffff)
    {
        int x = wibox->geometry.x + wibox->border_width,
            y = wibox->geometry.y + wibox->border_width;
        xcb_get_property_reply_t *prop_r;
        char *data;
        xcb_pixmap_t rootpix;
        xcb_get_property_cookie_t prop_c;
        xcb_screen_t *s = xutil_screen_get(_G_connection, _G_default_screen);
        prop_c = xcb_get_property_unchecked(_G_connection, false, s->root, _XROOTPMAP_ID,
                                            PIXMAP, 0, 1);
        if((prop_r = xcb_get_property_reply(_G_connection, prop_c, NULL)))
        {
            if(prop_r->value_len
               && (data = xcb_get_property_value(prop_r))
               && (rootpix = *(xcb_pixmap_t *) data))
                xcb_copy_area(_G_connection, rootpix,
                              wibox->ctx.pixmap, _G_gc,
                              x, y,
                              0, 0,
                              wibox->geometry.width, wibox->geometry.height);
            p_delete(&prop_r);
        }
    }

    /* draw background image, only if the background color is not opaque */
    if(wibox->bg_image && wibox->ctx.bg.alpha != 0xffff)
        draw_image(&wibox->ctx, 0, 0, 1.0, wibox->bg_image);

    /* draw background color */
    color_t col;
    xcolor_to_color(&wibox->ctx.bg, &col);
    draw_rectangle(&wibox->ctx, (area_t) { .x = 0, .y = 0,
                                           .width = wibox->geometry.width,
                                           .height = wibox->geometry.height },
                   1.0, true, &col);

    /* Compute where to draw text, using padding */
    area_t geometry =  { .x = wibox->text_padding.left,
                         .y = wibox->text_padding.top,
                         .width = wibox->geometry.width,
                         .height = wibox->geometry.height };

    /* Check that padding is not too superior to size. If so, just ignore it and
     * set size to 0 */
    int padding_width = wibox->text_padding.left + wibox->text_padding.right;
    if(padding_width <= geometry.width)
        geometry.width -= wibox->text_padding.left + wibox->text_padding.right;
    else
        geometry.width = 0;

    int padding_height = wibox->text_padding.top + wibox->text_padding.bottom;
    if(padding_height <= geometry.height)
        geometry.height -= wibox->text_padding.top + wibox->text_padding.bottom;
    else
        geometry.height = 0;

    draw_text(&wibox->ctx, &wibox->text_ctx, geometry);
}

/** Draw a wibox.
 * \param wibox The wibox to draw.
 */
static void
wibox_draw(wibox_t *wibox)
{
    if(wibox->visible)
    {
        wibox_render(wibox);
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

        if(wibox->visible)
            wibox_map(wibox);
        else
            wibox_unmap(wibox);

        luaA_object_emit_signal(L, udx, "property::visible", 0);
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
    return luaA_class_new(L, (lua_class_t *) &wibox_class);
}

static int
luaA_wibox_set_parent(lua_State *L, wibox_t *wibox)
{
    /* If wibox already has a window, we need to destroy it */
    if(wibox->window)
    {
        xcb_destroy_window(_G_connection, wibox->window);
        wibox->window = XCB_NONE;

        /* Remove it right away */
        wibox_array_lookup_and_remove(&globalconf.wiboxes, &(wibox_t) { .window = wibox->window });
        ewindow_binary_array_lookup_and_remove(&_G_ewindows, &(ewindow_t) { .window = wibox->window });

        luaA_object_emit_signal(L, -3, "property::window", 0);

        if(strut_has_value(&wibox->strut))
        {
            lua_pushlightuserdata(globalconf.L, screen_getbycoord(wibox->geometry.x, wibox->geometry.y));
            luaA_object_emit_signal(L, -1, "property::workarea", 0);
            lua_pop(L, 1);
        }

        luaA_object_unref_item(L, -3, wibox->parent);
        wibox->parent = NULL;
        luaA_object_emit_signal(L, -3, "property::screen", 0);
        luaA_object_unref(L, wibox);
    }

    if(!lua_isnil(L, -1))
    {
        /* \todo check that parent is not a child or on a different tree (other
         * pscreen) */
        luaA_checkudata(L, -1, &window_class);
        wibox->parent = luaA_object_ref_item(L, -3, -1);

        xcb_screen_t *s = xutil_screen_get(_G_connection, _G_default_screen);

        /* Create window */
        wibox->window = xcb_generate_id(_G_connection);
        xcb_create_window(_G_connection, s->root_depth, wibox->window, wibox->parent->window,
                          wibox->geometry.x, wibox->geometry.y,
                          wibox->geometry.width, wibox->geometry.height,
                          wibox->border_width, XCB_COPY_FROM_PARENT, s->root_visual,
                          XCB_CW_BACK_PIXEL | XCB_CW_BORDER_PIXEL | XCB_CW_BIT_GRAVITY
                          | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK,
                          (const uint32_t [])
                          {
                              wibox->ctx.bg.pixel,
                              wibox->border_color.pixel,
                              XCB_GRAVITY_NORTH_WEST,
                              true,
                              WIBOX_SELECT_INPUT_EVENT_MASK
                          });
        luaA_object_emit_signal(globalconf.L, 1, "property::window", 0);

        /* Raise window */
        stack_window_raise(L, -2);

        /* ref it */
        luaA_object_ref_class(L, -2, (lua_class_t *) &wibox_class);
        /* Now we can insert because we have a window id */
        wibox_array_insert(&globalconf.wiboxes, wibox);
        ewindow_binary_array_insert(&_G_ewindows, (ewindow_t *) wibox);

        wibox_draw_context_update(wibox);

        wibox_shape_update(wibox);

        xwindow_buttons_grab(wibox->window, &wibox->buttons);

        xwindow_set_cursor(wibox->window, xcursor_new(_G_connection, xcursor_font_fromstr(wibox->cursor)));

        xwindow_set_opacity(wibox->window, wibox->opacity);

        if(wibox->visible)
            wibox_map(wibox);
    }

    return 0;
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
    luaA_checkudataornil(L, -1, &image_class);
    luaA_object_unref_item(L, -3, wibox->bg_image);
    wibox->bg_image = luaA_object_ref_item(L, -3, -1);
    wibox->need_update = true;
    luaA_object_emit_signal(L, -2, "property::bg_image", 0);
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

/** Create a new wibox and init its default values.
 * The wibox is left on the stack.
 * \param L The Lua VM state.
 * \return A pointer to a wibox object, initialized.
 */
static wibox_t *
wibox_new_init(lua_State *L)
{
    wibox_t *wibox = wibox_new(L);
    wibox->visible = wibox->movable = wibox->resizable = true;
    wibox->ctx.fg = globalconf.colors.fg;
    wibox->ctx.bg = globalconf.colors.bg;
    wibox->opacity = -1;
    wibox->cursor = a_strdup("left_ptr");
    wibox->geometry.width = wibox->geometry.height = 1;
    wibox->text_ctx.valign = AlignTop;
    wibox->need_update = true;
    return wibox;
}

static int
luaA_wibox_set_text(lua_State *L, wibox_t *wibox)
{
    size_t len;
    const char *buf;

    if(lua_isnil(L, -1))
    {
        /* delete */
        draw_text_context_wipe(&wibox->text_ctx);
        p_clear(&wibox->text_ctx, 1);
    }
    else if((buf = luaL_checklstring(L, -1, &len)))
    {
        char *text;
        ssize_t tlen;
        /* if text has been converted to UTF-8 */
        if(draw_iso2utf8(buf, len, &text, &tlen))
        {
            draw_text_context_init(&wibox->text_ctx, text, tlen);
            p_delete(&text);
        }
        else
            draw_text_context_init(&wibox->text_ctx, buf, len);
    }
    return 0;
}

static int
luaA_wibox_get_text(lua_State *L, wibox_t *wibox)
{
    lua_pushlstring(L, wibox->text_ctx.text, wibox->text_ctx.len);
    return 1;
}

static int
luaA_wibox_text_padding(lua_State *L)
{
    wibox_t *wibox = luaA_checkudata(L, 1, (lua_class_t *) &wibox_class);

    if(lua_gettop(L) == 2)
        wibox->text_padding = luaA_getopt_padding(L, 2, &wibox->text_padding);

    return luaA_pushpadding(L, &wibox->text_padding);
}

static int
luaA_wibox_get_ellipsize(lua_State *L, wibox_t *wibox)
{
    switch(wibox->text_ctx.ellip)
    {
      case PANGO_ELLIPSIZE_START:
        lua_pushliteral(L, "start");
        break;
      case PANGO_ELLIPSIZE_MIDDLE:
        lua_pushliteral(L, "middle");
        break;
      case PANGO_ELLIPSIZE_END:
        lua_pushliteral(L, "end");
        break;
      case PANGO_ELLIPSIZE_NONE:
        lua_pushliteral(L, "none");
        break;
    }

    return 1;
}


static int
luaA_wibox_set_ellipsize(lua_State *L, wibox_t *wibox)
{
    size_t len;
    const char *buf;

    if((buf = luaL_checklstring(L, 3, &len)))
        switch(a_tokenize(buf, len))
        {
          case A_TK_START:
            wibox->text_ctx.ellip = PANGO_ELLIPSIZE_START;
            break;
          case A_TK_MIDDLE:
            wibox->text_ctx.ellip = PANGO_ELLIPSIZE_MIDDLE;
            break;
          case A_TK_END:
            wibox->text_ctx.ellip = PANGO_ELLIPSIZE_END;
            break;
          case A_TK_NONE:
            wibox->text_ctx.ellip = PANGO_ELLIPSIZE_NONE;
            break;
          default:
            return 0;
        }

    wibox->need_update = true;

    return 0;
}

static int
luaA_wibox_get_wrap(lua_State *L, wibox_t *wibox)
{
    switch(wibox->text_ctx.wrap)
    {
      case PANGO_WRAP_WORD:
        lua_pushliteral(L, "word");
        break;
      case PANGO_WRAP_CHAR:
        lua_pushliteral(L, "char");
        break;
      case PANGO_WRAP_WORD_CHAR:
        lua_pushliteral(L, "word_char");
        break;
    }

    return 1;
}

static int
luaA_wibox_set_wrap(lua_State *L, wibox_t *wibox)
{
    size_t len;
    const char *buf;

    if((buf = luaL_checklstring(L, 3, &len)))
        switch(a_tokenize(buf, len))
        {
          case A_TK_WORD:
            wibox->text_ctx.wrap = PANGO_WRAP_WORD;
            break;
          case A_TK_CHAR:
            wibox->text_ctx.wrap = PANGO_WRAP_CHAR;
            break;
          case A_TK_WORD_CHAR:
            wibox->text_ctx.wrap = PANGO_WRAP_WORD_CHAR;
            break;
          default:
            return 0;
        }

    wibox->need_update = true;

    return 0;
}

#define DO_WIBOX_ALIGN_FUNC(field) \
    static int \
    luaA_wibox_set_##field(lua_State *L, wibox_t *wibox) \
    { \
        size_t len; \
        const char *buf = luaL_checklstring(L, -1, &len); \
        wibox->text_ctx.field = draw_##field##_fromstr(buf, len); \
        wibox->need_update = true; \
        return 0; \
    } \
    static int \
    luaA_wibox_get_##field(lua_State *L, wibox_t *wibox) \
    { \
        lua_pushstring(L, draw_##field##_tostr(wibox->text_ctx.field)); \
        return 1; \
    }
DO_WIBOX_ALIGN_FUNC(align)
DO_WIBOX_ALIGN_FUNC(valign)
#undef DO_WIBOX_ALIGN_FUNC

void
wibox_class_setup(lua_State *L)
{
    static const struct luaL_reg wibox_methods[] =
    {
        LUA_CLASS_METHODS(wibox)
        { "text_padding", luaA_wibox_text_padding },
        { NULL, NULL }
    };

    static const struct luaL_reg wibox_module_meta[] =
    {
        { "__call", luaA_wibox_new },
        { NULL, NULL },
    };

    luaA_class_setup(L, (lua_class_t *) &wibox_class, "wibox", (lua_class_t *) &ewindow_class,
                     (lua_class_allocator_t) wibox_new_init,
                     (lua_class_collector_t) wibox_wipe,
                     NULL,
                     luaA_class_index_miss_property, luaA_class_newindex_miss_property,
                     wibox_methods, wibox_module_meta, NULL);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_VISIBLE,
                            (lua_class_propfunc_t) luaA_wibox_set_visible,
                            (lua_class_propfunc_t) luaA_wibox_get_visible,
                            (lua_class_propfunc_t) luaA_wibox_set_visible);
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
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_TEXT,
                            (lua_class_propfunc_t) luaA_wibox_set_text,
                            (lua_class_propfunc_t) luaA_wibox_get_text,
                            (lua_class_propfunc_t) luaA_wibox_set_text);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_ELLIPSIZE,
                            (lua_class_propfunc_t) luaA_wibox_set_ellipsize,
                            (lua_class_propfunc_t) luaA_wibox_get_ellipsize,
                            (lua_class_propfunc_t) luaA_wibox_set_ellipsize);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_WRAP,
                            (lua_class_propfunc_t) luaA_wibox_set_wrap,
                            (lua_class_propfunc_t) luaA_wibox_get_wrap,
                            (lua_class_propfunc_t) luaA_wibox_set_wrap);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_ALIGN,
                            (lua_class_propfunc_t) luaA_wibox_set_align,
                            (lua_class_propfunc_t) luaA_wibox_get_align,
                            (lua_class_propfunc_t) luaA_wibox_set_align);
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_VALIGN,
                            (lua_class_propfunc_t) luaA_wibox_set_valign,
                            (lua_class_propfunc_t) luaA_wibox_get_valign,
                            (lua_class_propfunc_t) luaA_wibox_set_valign);
    /* Properties overwritten */
    /* Parent can be set on wiboxes */
    luaA_class_add_property((lua_class_t *) &wibox_class, A_TK_PARENT,
                            (lua_class_propfunc_t) luaA_wibox_set_parent,
                            (lua_class_propfunc_t) luaA_window_get_parent,
                            (lua_class_propfunc_t) luaA_wibox_set_parent);

    luaA_class_connect_signal(L, (lua_class_t *) &wibox_class, "property::border_width", luaA_wibox_need_update);
    luaA_class_connect_signal(L, (lua_class_t *) &wibox_class, "property::geometry", luaA_wibox_need_update);
    luaA_class_connect_signal(L, (lua_class_t *) &wibox_class, "property::width", luaA_wibox_draw_context_update);
    luaA_class_connect_signal(L, (lua_class_t *) &wibox_class, "property::height", luaA_wibox_draw_context_update);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
