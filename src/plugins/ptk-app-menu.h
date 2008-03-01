/**
 * Copyright (c) 2006 LxDE Developers, see the file AUTHORS for details.
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

#ifndef PTK_APP_MENU_H
#define PTK_APP_MENU_H

#include <gtk/gtk.h>

extern GQuark  PTK_APP_MENU_ITEM_ID;

gboolean ptk_app_menu_item_has_data( GtkMenuItem* item );
void ptk_app_menu_insert_items( GtkMenu* menu, int position );
gboolean ptk_app_menu_need_reload();

#endif

