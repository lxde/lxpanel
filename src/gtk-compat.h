/**
 * Copyright (c) 2014 LxDE Developers, see the file AUTHORS for details.
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

#ifndef __GTK_COMPAT_H__
#define __GTK_COMPAT_H__ 1

/* compatibility things */
#if !GTK_CHECK_VERSION(2, 21, 0)
#  define   GDK_KEY_Tab     GDK_Tab
#  define   GDK_KEY_space   GDK_space
#  define   GDK_KEY_Return  GDK_Return
#  define   GDK_KEY_KP_Enter GDK_KP_Enter
#endif

#endif
