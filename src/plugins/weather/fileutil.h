/**
 * Copyright (c) 2012-2013 Piotr Sipika; see the AUTHORS file for more.
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
 * 
 * See the COPYRIGHT file for more information.
 */

/* Defines the helpers for configuration file handling */

#ifndef LXWEATHER_FILEUTIL_HEADER
#define LXWEATHER_FILEUTIL_HEADER

#include <glib.h>

/**
 * Reads configuration from the specified path and returns a list of 
 * LocationInfo pointers.
 *
 * @param pczPath Path to configuration file with key=value pairs.
 *
 * @return a list of LocationInfo pointers, or NULL on error
 *
 * @note The caller must free the returned list.
 */
GList *
getLocationsFromConfiguration(const gchar * pczPath);

/**
 * Creates and fills 'Location' sections based on passed-in LocationInfo
 * objects.
 *
 * @param pList Pointer to the list with LocationInfo objects.
 * @param pczPath Path to the file where to save the locations.
 */
void
saveLocationsToConfiguration(GList * pList, const gchar * pczPath);


#endif
