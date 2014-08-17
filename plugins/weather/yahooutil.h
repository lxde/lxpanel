/**
 * Copyright (c) 2012-2014 Piotr Sipika; see the AUTHORS file for more.
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

/* Provides utilities to use Yahoo's weather services */

#ifndef LXWEATHER_YAHOOUTIL_HEADER
#define LXWEATHER_YAHOOUTIL_HEADER

#include <glib.h>

/**
 * Retrieves the details for the specified location
 *
 * @param pczLocation The string containing the name/code of the location
 *
 * @return A pointer to a list of LocationInfo entries, possibly empty, 
 *         if no details were found. Caller is responsible for freeing the list.
 */
GList *
getLocationInfo(const gchar * pczLocation);

/**
 * Retrieves the forecast for the specified location WOEID
 *
 * @param pczWOEID The string containing the WOEID of the location
 * @param czUnits The character containing the units for the forecast (c|f)
 * @param pForecast The pointer to the forecast to be filled. If set to NULL,
 *                  a new one will be allocated.
 *
 */
void
getForecastInfo(const gchar * pczWOEID, const gchar czUnits, gpointer pForecast);

/**
 * Initializes the internals: XML and HTTP
 *
 */
void
initializeYahooUtil(void);

/**
 * Cleans up the internals: XML and HTTP
 *
 */
void
cleanupYahooUtil(void);

#endif
