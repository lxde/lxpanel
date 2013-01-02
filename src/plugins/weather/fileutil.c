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

/* Defines the helper functions for configuration file handling 
  (reading and writing) */

#include "fileutil.h"
#include "location.h"
#include "logutil.h"

#include <gio/gio.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>
#include <stdarg.h>

static void     fillLocationList             (GList ** ppList, GKeyFile * pKeyFile);
static gboolean createConfigurationDirectory (const gchar * pczPath);
static gboolean fillKeyFile                  (GKeyFile ** ppKeyFile, GList * pList);

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
getLocationsFromConfiguration(const gchar * pczPath)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::getLocationsFromConfiguration(%s)", pczPath);

  GList *    pList = NULL;
  GError *   pError = NULL;

  GKeyFile * pKeyFile = g_key_file_new();

  if (g_key_file_load_from_file(pKeyFile, pczPath, G_KEY_FILE_NONE, &pError))
    {
      fillLocationList(&pList, pKeyFile);
    }
  else
    {
      /* An error occurred... */
      if (pError)
        {
          LXW_LOG(LXW_ERROR, "Failed to read configuration at %s: %s",
                  pczPath, pError->message);

          g_error_free(pError);
        }

    }

  g_key_file_free(pKeyFile);

  /* Reverse list, if filled */
  pList = g_list_reverse(pList);

  return pList;
}

/**
 * Creates and fills 'Location' sections based on passed-in LocationInfo
 * objects.
 *
 * @param pList Pointer to the list with LocationInfo objects.
 * @param pczPath Path to the file where to save the locations.
 */
void
saveLocationsToConfiguration(GList * pList, const gchar * pczPath)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::saveLocationsToConfiguration(%s)", pczPath);

  GKeyFile * pKeyFile = g_key_file_new();

  /* populate key file object */
  if (!fillKeyFile(&pKeyFile, pList))
    {
      /* No valid entries */
      return;
    }

  /* Check if directory exists, create if it doesn't */
  if (!createConfigurationDirectory(pczPath))
    {
      return;
    }

  GFile * pFile = g_file_new_for_path(pczPath);

  /* Get an output stream and write to it */
  GError * pError = NULL;

  GFileOutputStream * pOutputStream = g_file_replace(pFile,
                                                     NULL,
                                                     TRUE,
                                                     G_FILE_CREATE_PRIVATE,
                                                     NULL,
                                                     &pError);

  if (pOutputStream)
    {
      /* Everything is OK */
      gsize szDataLen = 0;

      gchar * pcData = g_key_file_to_data(pKeyFile, &szDataLen, &pError);

      if (pError)
        {
          LXW_LOG(LXW_ERROR, "Failed to convert key file to data: %s", pError->message);

          g_error_free(pError);
        }
      else
        {
          gsize szBytesWritten = 0;

          if (!g_output_stream_write_all((GOutputStream *)pOutputStream,
                                         pcData,
                                         szDataLen,
                                         &szBytesWritten,
                                         NULL,
                                         &pError))
            {
              /* Failed */
              LXW_LOG(LXW_ERROR, "Failed to write to output stream: %s", pError->message);

              g_error_free(pError);
            }

        }

      g_free(pcData);
      
      g_object_unref(pOutputStream);
    }
  else
    {
      LXW_LOG(LXW_ERROR, "Failed to create %s: %s", pczPath, pError->message);

      g_error_free(pError);
    }

  g_object_unref(pFile);

  g_key_file_free(pKeyFile);
}

/**
 * Creates the configuration directory specified at startup.
 *
 * @param pczPath Path to the configuration file to use.
 *
 * @return TRUE on success, FALSE on any error or failure.
 */
static gboolean
createConfigurationDirectory(const gchar * pczPath)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::createConfigurationDirectory(%s)", pczPath);


  gboolean bRetVal = FALSE;

  gchar * pcPathDir = g_path_get_dirname(pczPath);

  struct stat pathStat;

  if (stat(pcPathDir, &pathStat) == -1)
    {
      /* Non-existence is not an error */
      if (errno == ENOENT)
        {
          if (mkdir(pcPathDir, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH) == 0)
            {
              bRetVal = TRUE;
            }
          else
            {
              /* Check error number, if the directory exists, we're OK */
              if (errno == EEXIST)
                {
                  bRetVal = TRUE;
                }
              else
                {
                  LXW_LOG(LXW_ERROR, "Failed to retrieve file information on %s: %s",
                          pcPathDir, strerror(errno));
                }

            }

        }
      else
        {
          LXW_LOG(LXW_ERROR, "Failed to retrieve file information on %s: %s",
                  pcPathDir, strerror(errno));
        }

    }
  else if (S_ISDIR(pathStat.st_mode))
    {
      bRetVal = TRUE;
    }

  g_free(pcPathDir);

  return bRetVal;
}

/**
 * Goes through all 'Location' sections and creates LocationInfo objects
 * based on key=value pairs
 *
 * @param ppList Pointer to the LocationInfo list pointer.
 * @param pKeyFile Pointer to the configuration file.
 */
static void
fillLocationList(GList ** ppList, GKeyFile * pKeyFile)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::fillLocationList()");

  gsize szGroupCount = 0;

  gchar ** pGroupNameList = g_key_file_get_groups(pKeyFile, &szGroupCount);

  gsize szGroupIndex = 0;

  for (; szGroupIndex < szGroupCount; ++szGroupIndex)
    {
      /* See if this group is 'Location N' */
      
      gchar ** pGroupNameTokens = g_strsplit(pGroupNameList[szGroupIndex], " ", 2);
                                             
      if (g_ascii_strncasecmp(pGroupNameTokens[0],
                              LOCATIONINFO_GROUP_NAME,
                              LOCATIONINFO_GROUP_NAME_LENGTH))
        {
          /* A match would produce a FALSE return value, 
           * so this group is not 'Location N' */
          LXW_LOG(LXW_ERROR, "Group: '%s' not handled", pGroupNameList[szGroupIndex]);
        }
      else
        {          
          gchar * pcWOEID = g_key_file_get_string(pKeyFile,
                                                  pGroupNameList[szGroupIndex],
                                                  LocationInfoFieldNames[WOEID],
                                                  NULL);
              
          gchar * pcAlias = g_key_file_get_string(pKeyFile,
                                                  pGroupNameList[szGroupIndex],
                                                  LocationInfoFieldNames[ALIAS],
                                                  NULL);

          LXW_LOG(LXW_DEBUG, "Group name: %s, Alias: %s, WOEID: %s",
                  pGroupNameList[szGroupIndex], pcAlias, pcWOEID);

          /* We MUST have WOEID and Alias */
          if (!pcWOEID || !strlen(pcWOEID) || !pcAlias || !strlen(pcAlias))
            {
              /* just in case they're emtpy strings */
              g_free(pcWOEID);
              g_free(pcAlias);

              continue;
            }

          gchar * pcCity = g_key_file_get_string(pKeyFile,
                                                 pGroupNameList[szGroupIndex],
                                                 LocationInfoFieldNames[CITY],
                                                 NULL);

          gchar * pcState = g_key_file_get_string(pKeyFile,
                                                  pGroupNameList[szGroupIndex],
                                                  LocationInfoFieldNames[STATE],
                                                  NULL);

          gchar * pcCountry = g_key_file_get_string(pKeyFile,
                                                    pGroupNameList[szGroupIndex],
                                                    LocationInfoFieldNames[COUNTRY],
                                                    NULL);

          gchar * pcUnits = g_key_file_get_string(pKeyFile,
                                                  pGroupNameList[szGroupIndex],
                                                  LocationInfoFieldNames[UNITS],
                                                  NULL);

          gint iInterval = g_key_file_get_integer(pKeyFile,
                                                  pGroupNameList[szGroupIndex],
                                                  LocationInfoFieldNames[INTERVAL],
                                                  NULL);

          gboolean bEnabled = g_key_file_get_boolean(pKeyFile,
                                                     pGroupNameList[szGroupIndex],
                                                     LocationInfoFieldNames[ENABLED],
                                                     NULL);

          LocationInfo * pLocation = g_try_new0(LocationInfo, 1);

          if (pLocation)
            {
              pLocation->pcAlias_ = g_strndup(pcAlias, strlen(pcAlias));
              pLocation->pcCity_ = (pcCity)?g_strndup(pcCity, strlen(pcCity)):NULL;
              pLocation->pcState_ = (pcState)?g_strndup(pcState, strlen(pcState)):NULL;
              pLocation->pcCountry_ = (pcCountry)?g_strndup(pcCountry, strlen(pcCountry)):NULL;
              pLocation->pcWOEID_ = g_strndup(pcWOEID, strlen(pcWOEID));
              pLocation->cUnits_ = pcUnits ? pcUnits[0] : 'f';
              pLocation->uiInterval_ = (iInterval > 0) ? iInterval : 1;
              pLocation->bEnabled_ = bEnabled;

              *ppList = g_list_prepend(*ppList, pLocation);
            }

          g_free(pcAlias);
          g_free(pcCity);
          g_free(pcState);
          g_free(pcCountry);
          g_free(pcWOEID);
          g_free(pcUnits);
        }

      /* Free the token list */
      g_strfreev(pGroupNameTokens);
    }

  g_strfreev(pGroupNameList);
}

/**
 * Fills the supplied key file with data from the list.
 *
 * @param ppKeyFile Pointer to the key file to fill in
 * @param pList     Pointer to the list to use data from
 *
 * @return TRUE if there was at least one location to save, FALSE otherwise.
 */
static gboolean
fillKeyFile(GKeyFile ** ppKeyFile, GList * pList)
{
  LXW_LOG(LXW_DEBUG, "fileUtil::fillKeyFile()");

  gboolean bRetVal = FALSE;

  gint iSize = g_list_length(pList);

  int i = 0;

  for (; i < iSize; ++i)
    {
      LocationInfo * pLocation = (LocationInfo *)g_list_nth_data(pList, i);

      if (pLocation)
        {
          gchar * pcGroupName = g_strdup_printf("Location %d", i + 1);

          g_key_file_set_string(*ppKeyFile, pcGroupName, 
                                LocationInfoFieldNames[ALIAS], pLocation->pcAlias_);
          
          if (pLocation->pcCity_)
            {
              g_key_file_set_string(*ppKeyFile, pcGroupName, 
                                    LocationInfoFieldNames[CITY], pLocation->pcCity_);
            }

          if (pLocation->pcState_)
            {
              g_key_file_set_string(*ppKeyFile, pcGroupName, 
                                    LocationInfoFieldNames[STATE], pLocation->pcState_);
            }

          if (pLocation->pcCountry_)
            {
              g_key_file_set_string(*ppKeyFile, pcGroupName, 
                                    LocationInfoFieldNames[COUNTRY], pLocation->pcCountry_);
            }

          g_key_file_set_string(*ppKeyFile, pcGroupName, 
                                LocationInfoFieldNames[WOEID], pLocation->pcWOEID_);
          g_key_file_set_string(*ppKeyFile, pcGroupName, 
                                LocationInfoFieldNames[UNITS], &pLocation->cUnits_);
          g_key_file_set_integer(*ppKeyFile, pcGroupName, 
                                 LocationInfoFieldNames[INTERVAL], (gint)pLocation->uiInterval_);
          g_key_file_set_boolean(*ppKeyFile, pcGroupName, 
                                 LocationInfoFieldNames[ENABLED], pLocation->bEnabled_);

          g_free(pcGroupName);

          bRetVal = TRUE;
        }

    }

  return bRetVal;
}
