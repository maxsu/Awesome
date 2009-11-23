/*
 * wibox.h - wibox functions header
 *
 * Copyright © 2007-2009 Julien Danjou <julien@danjou.info>
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

#ifndef AWESOME_OBJECTS_WIBOX_H
#define AWESOME_OBJECTS_WIBOX_H

#include "draw.h"
#include "padding.h"
#include "objects/ewindow.h"
#include "objects/image.h"
#include "common/luaobject.h"

/** Wibox type */
struct wibox_t
{
    EWINDOW_OBJECT_HEADER
    /** Visible */
    bool visible;
    /** Need update */
    bool need_update;
    /** Need shape update */
    bool need_shape_update;
    /** Background image */
    image_t *bg_image;
    /** Draw context */
    draw_context_t ctx;
    /** The wibox text stuff */
    draw_text_context_t text_ctx;
    /** Text padding */
    padding_t text_padding;
    /** The window's content (shape) */
    image_t *shape_clip;
    /** The window's content and border (shape) */
    image_t *shape_bounding;
    /** Has wibox an attached systray **/
    bool has_systray;
};

BARRAY_FUNCS(wibox_t *, wibox, DO_NOTHING, window_cmp)

void wibox_refresh(void);

wibox_t * wibox_getbywin(xcb_window_t);

void wibox_refresh_pixmap_partial(wibox_t *, int16_t, int16_t, uint16_t, uint16_t);

void wibox_class_setup(lua_State *);

lua_interface_window_t wibox_class;

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
