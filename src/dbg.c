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

#include "dbg.h"

int log_level = LOG_WARN;
bool log_level_set_on_commandline = false;
int configured_log_level = LOG_WARN;



void _check_cairo_status(cairo_t* cr, char const* file, char const* func, int line)
{
    cairo_status_t status = cairo_status(cr);
    if (status != CAIRO_STATUS_SUCCESS)
        ERR("%s:%s:%-5d: cairo had error %d: %s\n", file, func, line, status,
                cairo_status_to_string(status));
}

void _check_cairo_surface_status(cairo_surface_t** surf, char const* file, char const* func, int line)
{
    cairo_status_t status = cairo_surface_status(*surf);
    if (status != CAIRO_STATUS_SUCCESS) {
        ERR("%s:%s:%-5d: cairo had error %d: %s\n", file, func, line, status,
                cairo_status_to_string(status));
        cairo_surface_destroy(*surf);
        *surf = NULL;
    }
}

/* vim: set sw=4 et sts=4 : */
