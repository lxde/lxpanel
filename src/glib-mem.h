/*
* C++ Interface: glib-mem
*
* Description: Compatibility macros for older versions of glib
*
* Author: Hong Jen Yee (PCMan) <pcman.tw (AT) gmail.com>, (C) 2006
*
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

#ifndef _GLIB_MEM_H_
#define _GLIB_MEM_H_

#include <glib.h>

#if ! GLIB_CHECK_VERSION(2, 10, 0)
/* older versions of glib don't provde g_slice API */
#define g_slice_alloc(size)         g_malloc(size)
#define g_slice_alloc0(size)        g_malloc0(size)
#define g_slice_new(type)           g_new(type, 1)
#define g_slice_new0(type)          g_new0(type, 1)
#define g_slice_free(type, mem)     g_free(mem)
#define g_slice_free1(size, mem)    g_free(mem)
#endif

#endif

