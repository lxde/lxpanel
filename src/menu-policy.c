/**
 * Copyright (c) 2009 LxDE Developers, see the file AUTHORS for details.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <menu-cache.h>
#include <glib.h>

#include "menu-policy.h"
#include "panel.h"

guint32 visibility_flags = 0;

/* Allocate a menu cache. */
MenuCache * panel_menu_cache_new(guint32* visibility_flags)
{
    MenuCache* cache;
    if (g_getenv("XDG_MENU_PREFIX") == NULL)
        g_setenv("XDG_MENU_PREFIX", "lxde-", TRUE);
    cache = menu_cache_lookup("applications.menu");
    if(visibility_flags)
    {
        if(is_in_lxde)
            *visibility_flags = SHOW_IN_LXDE;
        else
        {
            const char* de_name = g_getenv("XDG_CURRENT_DESKTOP");
            if(de_name)
                *visibility_flags = menu_cache_get_desktop_env_flag(cache, de_name);
            else
                *visibility_flags = SHOW_IN_LXDE|SHOW_IN_GNOME|SHOW_IN_KDE|SHOW_IN_XFCE;
        }
    }
    return cache;
}

/* Evaluate the visibility of a menu item. */
gboolean panel_menu_item_evaluate_visibility(MenuCacheItem * item, guint32 visibility_flags)
{
    g_debug("flags: %d\n", visibility_flags);
    return menu_cache_app_get_is_visible(MENU_CACHE_APP(item), visibility_flags);
}
