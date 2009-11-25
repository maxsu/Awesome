/*
 * keyresolv.h - Key resolv
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

#ifndef AWESOME_KEYRESOLV_H
#define AWESOME_KEYRESOLV_H

#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

xcb_keysym_t keyresolv_get_keysym(xcb_keycode_t, uint16_t);
bool keyresolv_keysym_to_string(xcb_keysym_t, char *, ssize_t);
xcb_keycode_t * keyresolv_string_to_keycode(const char *, ssize_t);
void keyresolv_lock_mask_refresh(xcb_connection_t *, xcb_get_modifier_mapping_cookie_t,
                                 xcb_key_symbols_t *);
/** Keys symbol table */
xcb_key_symbols_t *_G_keysyms;

#endif

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
