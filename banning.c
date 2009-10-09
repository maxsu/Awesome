/*
 * banning.c - client banning management
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

#include "banning.h"
#include "objects/tag.h"
#include "screen.h"

/** Reban windows following current selected tags.
 */
static int
banning_need_update(lua_State *L)
{
    screen_t *screen = NULL;

    /** \todo add a common class for object with a screen? */
    client_t *client = luaA_toudata(L, 1, &client_class);
    if(client)
        screen = client->screen;
    else
    {
        tag_t *tag = luaA_toudata(L, 1, &tag_class);
        if(tag)
            screen = tag->screen;
        else
            return 0;
    }

    /* We update the complete banning only once per main loop to avoid
     * excessive updates...  */
    screen->need_lazy_banning = true;

    /* But if a client will be banned in our next update we unfocus it now. */
    foreach(c, globalconf.clients)
        /* we don't touch other screens windows */
        if(!client_isvisible(*c, screen) && (*c)->screen == screen)
            client_ban_unfocus(*c);

    return 0;
}

void
banning_init(void)
{
    luaA_class_connect_signal(globalconf.L, &client_class, "property::minimized", banning_need_update);
    luaA_class_connect_signal(globalconf.L, &client_class, "property::hidden", banning_need_update);
    luaA_class_connect_signal(globalconf.L, &client_class, "property::sticky", banning_need_update);
    luaA_class_connect_signal(globalconf.L, &client_class, "tagged", banning_need_update);
    luaA_class_connect_signal(globalconf.L, &client_class, "untagged", banning_need_update);
    luaA_class_connect_signal(globalconf.L, &tag_class, "property::selected", banning_need_update);
    luaA_class_connect_signal(globalconf.L, &tag_class, "property::screen", banning_need_update);
}

static void
reban(screen_t *screen)
{
    if (!screen->need_lazy_banning)
        return;

    screen->need_lazy_banning = false;

    client_ignore_enterleave_events();

    foreach(c, globalconf.clients)
        if(client_isvisible(*c, screen))
            client_unban(*c);

    /* Some people disliked the short flicker of background, so we first unban everything.
     * Afterwards we ban everything we don't want. This should avoid that. */
    foreach(c, globalconf.clients)
        /* we don't touch other screens windows */
        if(!client_isvisible(*c, screen) && (*c)->screen == screen)
            client_ban(*c);

    client_restore_enterleave_events();
}

/** Check all screens if they need to rebanned
 */
void
banning_refresh(void)
{
    foreach(screen, globalconf.screens)
        reban(screen);
}

// vim: filetype=c:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:encoding=utf-8:textwidth=80
