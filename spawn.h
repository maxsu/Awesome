/*
 * spawn.h - Lua configuration management header
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

#ifndef AWESOME_SPAWN_H
#define AWESOME_SPAWN_H

#define SN_API_NOT_YET_FROZEN
#include <libsn/sn.h>

#include "globalconf.h"

void spawn_init(void);
void spawn_start_notify(client_t *, const char *);
int luaA_spawn(lua_State *);

/** The startup notification display struct */
SnDisplay *_G_sndisplay;

#endif
// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
