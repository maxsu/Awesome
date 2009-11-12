/*
 * bma.c - Bob Marley Algorithm
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

#include "globalconf.h"
#include "luaa.h"
#include "bma.h"
#include "awesome.h"
#include "objects/wibox.h"
#include "objects/client.h"

/** Ignore enter and leave window
 * in certain cases, like map/unmap or move, so we don't get spurious events.
 */
void
bma_enable(void)
{
    foreach(c, globalconf.clients)
        xcb_change_window_attributes(_G_connection,
                                     (*c)->window,
                                     XCB_CW_EVENT_MASK,
                                     (const uint32_t []) { CLIENT_SELECT_INPUT_EVENT_MASK & ~(XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW) });

    foreach(w, globalconf.wiboxes)
        xcb_change_window_attributes(_G_connection,
                                     (*w)->window,
                                     XCB_CW_EVENT_MASK,
                                     (const uint32_t []) { WIBOX_SELECT_INPUT_EVENT_MASK & ~(XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_LEAVE_WINDOW) });
}

void
bma_disable(void)
{
    foreach(c, globalconf.clients)
        xcb_change_window_attributes(_G_connection,
                                     (*c)->window,
                                     XCB_CW_EVENT_MASK,
                                     (const uint32_t []) { CLIENT_SELECT_INPUT_EVENT_MASK });

    foreach(w, globalconf.wiboxes)
        xcb_change_window_attributes(_G_connection,
                                     (*w)->window,
                                     XCB_CW_EVENT_MASK,
                                     (const uint32_t []) { WIBOX_SELECT_INPUT_EVENT_MASK });
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
