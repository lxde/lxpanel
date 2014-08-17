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

/* Provides http protocol utility functions */

#include "httputil.h"

#include <libxml/nanohttp.h>
#include <libxml/xmlmemory.h>

#include <string.h>

/**
 * Cleans up the nano HTTP state
 *
 * @param pContext HTTP Context
 * @param pContentType Content-type container
 */
static void
cleanup(void * pContext, char * pContentType)
{
  if (pContext)
    {
      xmlNanoHTTPClose(pContext);
    }

  if (pContentType)
    {
      xmlFree(pContentType);
    }

  xmlNanoHTTPCleanup();
}

/**
 * Returns the contents of the requested URL
 *
 * @param pczURL The URL to retrieve.
 * @param piRetCode The return code supplied with the response.
 * @param piDataSize The resulting data length [out].
 *
 * @return A pointer to a null-terminated buffer containing the textual 
 *         representation of the response. Must be freed by the caller.
 */
gpointer
getURL(const gchar * pczURL, gint * piRetCode, gint * piDataSize)
{
  /* nanohttp magic */
  gint iBufReadSize = 1024;
  gint iReadSize = 0;
  gint iCurrSize = 0;

  gpointer pInBuffer = NULL;
  gpointer pInBufferRef = NULL;

  gchar cReadBuffer[iBufReadSize];
  bzero(cReadBuffer, iBufReadSize);

  xmlNanoHTTPInit();

  char * pContentType = NULL;
  void * pHTTPContext = NULL;

  pHTTPContext = xmlNanoHTTPOpen(pczURL, &pContentType);

  if (!pHTTPContext)
    {
      // failure
      cleanup(pHTTPContext, pContentType);

      *piRetCode = -1;

      return pInBuffer; // it's NULL
    }

  *piRetCode = xmlNanoHTTPReturnCode(pHTTPContext);

  if (*piRetCode != HTTP_STATUS_OK)
    {
      // failure
      cleanup(pHTTPContext, pContentType);

      return pInBuffer; // it's NULL
    }

  while ((iReadSize = xmlNanoHTTPRead(pHTTPContext, cReadBuffer, iBufReadSize)) > 0)
    {
      // set return code
      *piRetCode = xmlNanoHTTPReturnCode(pHTTPContext);

      /* Maintain pointer to old location, free on failure */
      pInBufferRef = pInBuffer;

      pInBuffer = g_try_realloc(pInBuffer, iCurrSize + iReadSize);

      if (!pInBuffer || *piRetCode != HTTP_STATUS_OK)
        {
          // failure
          cleanup(pHTTPContext, pContentType);

          g_free(pInBufferRef);

          return pInBuffer; // it's NULL
        }

      memcpy(pInBuffer + iCurrSize, cReadBuffer, iReadSize);
      
      iCurrSize += iReadSize;

      // clear read buffer
      bzero(cReadBuffer, iBufReadSize);

      *piDataSize = iCurrSize;
    }

  if (iReadSize < 0)
    {
      // error
      g_free(pInBuffer);

      pInBuffer = NULL;
    }
  else
    {
      /* Maintain pointer to old location, free on failure */
      pInBufferRef = pInBuffer;

      // need to add '\0' at the end
      pInBuffer = g_try_realloc(pInBuffer, iCurrSize + 1);

      if (!pInBuffer)
        {
          // failure
          g_free(pInBufferRef);

          pInBuffer = NULL;
        }
      else
        {
          memcpy(pInBuffer + iCurrSize, "\0", 1);
        }
    }
  
  // finish up
  cleanup(pHTTPContext, pContentType);

  return pInBuffer;
}
