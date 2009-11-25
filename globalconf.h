/*
 * globalconf.h - basic globalconf.header
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

#ifndef AWESOME_GLOBALCONF_H
#define AWESOME_GLOBALCONF_H

#include <lua.h>

#include "color.h"
#include "common/array.h"

typedef struct tag tag_t;

ARRAY_TYPE(tag_t *, tag)

/** Main configuration structure */
typedef struct
{
    /** Lua VM state */
    lua_State *L;
    /** Default colors */
    struct
    {
        xcolor_t fg, bg;
    } colors;
    /** Wiboxes */
} awesome_t;

extern awesome_t globalconf;

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
