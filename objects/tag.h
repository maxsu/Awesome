/*
 * tag.h - tag management header
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

#ifndef AWESOME_OBJECTS_TAG_H
#define AWESOME_OBJECTS_TAG_H

#include "objects/ewindow.h"

/** Tag type */
struct tag
{
    LUA_OBJECT_HEADER
    /** Tag name */
    char *name;
    /** true if selected */
    bool selected;
    /** Windows in this tag */
    ewindow_array_t windows;
};

int tags_get_first_selected_index(void);
void tag_ewindow(lua_State *, int, int);
void untag_ewindow(lua_State *, int, int);
bool ewindow_is_tagged(ewindow_t *, tag_t *);
void tag_view_only_byindex(lua_State *, int);

ARRAY_FUNCS(tag_t *, tag, DO_NOTHING)

void tag_class_setup(lua_State *);

bool tag_get_selected(tag_t *);
char *tag_get_name(tag_t *);

lua_class_t tag_class;

tag_array_t _G_tags;

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
