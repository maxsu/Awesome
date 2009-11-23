/*
 * common/xutil.c - X-related useful functions
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

/* XCB doesn't provide keysyms definition */
#include <X11/keysym.h>

#include "common/util.h"

#include <xcb/xcb.h>
#include <xcb/xcb_atom.h>
#include <xcb/xcb_icccm.h>

#include "common/xutil.h"
#include "common/tokenize.h"

/* Number of different errors */
#define ERRORS_NBR 256

uint16_t
xutil_key_mask_fromstr(const char *keyname, size_t len)
{
    switch(a_tokenize(keyname, len))
    {
      case A_TK_SHIFT:   return XCB_MOD_MASK_SHIFT;
      case A_TK_LOCK:    return XCB_MOD_MASK_LOCK;
      case A_TK_CTRL:
      case A_TK_CONTROL: return XCB_MOD_MASK_CONTROL;
      case A_TK_MOD1:    return XCB_MOD_MASK_1;
      case A_TK_MOD2:    return XCB_MOD_MASK_2;
      case A_TK_MOD3:    return XCB_MOD_MASK_3;
      case A_TK_MOD4:    return XCB_MOD_MASK_4;
      case A_TK_MOD5:    return XCB_MOD_MASK_5;
      case A_TK_ANY:     return XCB_MOD_MASK_ANY;
      default:           return XCB_NO_SYMBOL;
    }
}

void
xutil_key_mask_tostr(uint16_t mask, const char **name, size_t *len)
{
    switch(mask)
    {
#define SET_RESULT(res) \
        *name = #res; \
        *len = sizeof(#res) - 1; \
        return;
      case XCB_KEY_BUT_MASK_SHIFT:    SET_RESULT(Shift)
      case XCB_KEY_BUT_MASK_LOCK:     SET_RESULT(Lock)
      case XCB_KEY_BUT_MASK_CONTROL:  SET_RESULT(Control)
      case XCB_KEY_BUT_MASK_MOD_1:    SET_RESULT(Mod1)
      case XCB_KEY_BUT_MASK_MOD_2:    SET_RESULT(Mod2)
      case XCB_KEY_BUT_MASK_MOD_3:    SET_RESULT(Mod3)
      case XCB_KEY_BUT_MASK_MOD_4:    SET_RESULT(Mod4)
      case XCB_KEY_BUT_MASK_MOD_5:    SET_RESULT(Mod5)
      case XCB_KEY_BUT_MASK_BUTTON_1: SET_RESULT(Button1)
      case XCB_KEY_BUT_MASK_BUTTON_2: SET_RESULT(Button2)
      case XCB_KEY_BUT_MASK_BUTTON_3: SET_RESULT(Button3)
      case XCB_KEY_BUT_MASK_BUTTON_4: SET_RESULT(Button4)
      case XCB_KEY_BUT_MASK_BUTTON_5: SET_RESULT(Button5)
      default:                   SET_RESULT(Unknown)
#undef SET_RESULT
    }
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
