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

#include "location.h"
#include "weatherwidget.h"
#include "yahooutil.h"
#include "logutil.h"

#include <lxpanel/plugin.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <stdlib.h>

/* External button-press handler from plugin.c */
extern gboolean plugin_button_press_event(GtkWidget *widget, GdkEventButton *event, Plugin *plugin);

/* Need to maintain count for bookkeeping */
static gint g_iCount = 0;

typedef struct
{
  gint iMyId_;
  GtkWidget * pWeather_;
} WeatherPluginPrivate;

/**
 * Weather Plugin constructor
 *
 * @param pPlugin Pointer to the PluginClass wrapper instance.
 * @param pcFP Pointer to the configuration file position for this plugin.
 *
 * @return 1 (TRUE) on success, 0 (FALSE) on failure.
 */
static int
weather_constructor(Plugin * pPlugin, char ** pFP)
{
  WeatherPluginPrivate * pPriv = g_new0(WeatherPluginPrivate, 1);

  /* There is one more now... */
  ++g_iCount;

  pPriv->iMyId_ = g_iCount;

  if (g_iCount == 1)
    {
      initializeLogUtil("syslog");
      
      setMaxLogLevel(LXW_ERROR);

      initializeYahooUtil();
    }

  LXW_LOG(LXW_DEBUG, "weather_constructor()");
  
  GtkWidget * pWidg = gtk_weather_new(FALSE);

  pPriv->pWeather_ = pWidg;

  GtkWidget * pEventBox = gtk_event_box_new();

  /* Connect signals. */
  g_signal_connect(pEventBox, 
                   "button-press-event", 
                   G_CALLBACK(plugin_button_press_event),
                   pPlugin);

  gtk_container_add(GTK_CONTAINER(pEventBox), pWidg);

  pPlugin->priv = pPriv;
  pPlugin->pwid = pEventBox;

  gtk_widget_set_has_window(pPlugin->pwid,FALSE);

  gtk_widget_show_all(pPlugin->pwid);

  /* use config, see lxpanel_get_line, below */
  LocationInfo * pLocation = g_new0(LocationInfo, 1);

  line l;

  l.len = 256;
  
  if (pFP)
    {
      while (lxpanel_get_line(pFP, &l) != LINE_BLOCK_END)
        {
          if (l.type == LINE_VAR)
            {
              if (!g_ascii_strcasecmp(l.t[0], "alias"))
                {
                  pLocation->pcAlias_ = g_strndup(l.t[1], strlen(l.t[1]));
                }
              else if (!g_ascii_strcasecmp(l.t[0], "city"))
                {
                  pLocation->pcCity_ = g_strndup(l.t[1], strlen(l.t[1]));
                }
              else if (!g_ascii_strcasecmp(l.t[0], "state"))
                {
                  pLocation->pcState_ = g_strndup(l.t[1], strlen(l.t[1]));
                }
              else if (!g_ascii_strcasecmp(l.t[0], "country"))
                {
                  pLocation->pcCountry_ = g_strndup(l.t[1], strlen(l.t[1]));
                }
              else if (!g_ascii_strcasecmp(l.t[0], "woeid"))
                {
                  pLocation->pcWOEID_ = g_strndup(l.t[1], strlen(l.t[1]));
                }
              else if (!g_ascii_strcasecmp(l.t[0], "units"))
                {
                  pLocation->cUnits_ = *(l.t[1]);
                }
              else if (!g_ascii_strcasecmp(l.t[0], "interval"))
                {
                  pLocation->uiInterval_ = (guint)abs((gint)g_ascii_strtoll((l.t[1])?l.t[1]:"1", NULL, 10));
                }
              else if (!g_ascii_strcasecmp(l.t[0], "enabled"))
                {
                  pLocation->bEnabled_ = (gint)g_ascii_strtoll((l.t[1])?l.t[1]:"0", NULL, 10);
                }

            }
          else
            {
              LXW_LOG(LXW_ERROR, "Weather: illegal config line: %s", l.str);

              return 0;
            }

        }

    }
  
  if (pLocation->pcAlias_ && pLocation->pcWOEID_)
    {
      GValue locationValue = G_VALUE_INIT;

      g_value_init(&locationValue, G_TYPE_POINTER);
              
      /* location is copied by the widget */
      g_value_set_pointer(&locationValue, pLocation);
              
      g_object_set_property(G_OBJECT(pWidg),
                            "location",
                            &locationValue);
    }

  freeLocation(pLocation);

  return 1;
}

/**
 * Weather Plugin destructor.
 *
 * @param pPlugin Pointer to the PluginClass wrapper instance.
 */
static void
weather_destructor(Plugin * pPlugin)
{
  WeatherPluginPrivate * pPriv = (WeatherPluginPrivate *) pPlugin->priv;

  LXW_LOG(LXW_DEBUG, "weather_destructor(%d): %d", pPriv->iMyId_, g_iCount);

  g_free(pPriv);

  --g_iCount;

  if (g_iCount == 0)
    {
      cleanupYahooUtil();

      cleanupLogUtil();
    }
}

/**
 * Weather Plugin configuration change callback.
 *
 * @param pPlugin Pointer to the PluginClass wrapper instance.
 */
static void
weather_configuration_changed(Plugin * pPlugin)
{
  LXW_LOG(LXW_DEBUG, "weather_configuration_changed()");

  if (pPlugin && pPlugin->panel)
    {
      LXW_LOG(LXW_DEBUG, 
             "   orientation: %s, width: %d, height: %d, icon size: %d\n", 
             (pPlugin->panel->orientation == ORIENT_HORIZ)?"HORIZONTAL":
             (pPlugin->panel->orientation == ORIENT_VERT)?"VERTICAL":"NONE",
             pPlugin->panel->width,
             pPlugin->panel->height,
             pPlugin->panel->icon_size);
    }

}

/**
 * Weather Plugin configuration dialog callback.
 *
 * @param pPlugin Pointer to the PluginClass wrapper instance.
 * @param pParent Pointer to the GtkWindow parent.
 */
static void
weather_configure(Plugin * pPlugin, GtkWindow * pParent G_GNUC_UNUSED)
{
  LXW_LOG(LXW_DEBUG, "weather_configure()");

  WeatherPluginPrivate * pPriv = (WeatherPluginPrivate *) pPlugin->priv;

  gtk_weather_run_preferences_dialog(GTK_WIDGET(pPriv->pWeather_));
}

/**
 * Weather Plugin callback to save configuration
 *
 * @param pPlugin Pointer to the PluginClass wrapper instance.
 * @param pFile Pointer to the FILE object
 */
static void
weather_save_configuration(Plugin * pPlugin, FILE *pFile)
{
  WeatherPluginPrivate * pPriv = (WeatherPluginPrivate *) pPlugin->priv;

  LXW_LOG(LXW_DEBUG, "weather_save_configuration(%d)", pPriv->iMyId_);

  GValue location = G_VALUE_INIT;

  g_value_init(&location, G_TYPE_POINTER);

  /* pwid is the WeatherWidget */
  g_object_get_property(G_OBJECT(pPriv->pWeather_),
                        "location",
                        &location);

  LocationInfo * pLocation = g_value_get_pointer(&location);

  if (pLocation)
    {
      lxpanel_put_str(pFile, "alias", pLocation->pcAlias_);
      lxpanel_put_str(pFile, "city", pLocation->pcCity_);
      lxpanel_put_str(pFile, "state", pLocation->pcState_);
      lxpanel_put_str(pFile, "country", pLocation->pcCountry_);
      lxpanel_put_str(pFile, "woeid", pLocation->pcWOEID_);
      lxpanel_put_line(pFile, "units=%c", pLocation->cUnits_);
      lxpanel_put_int(pFile, "interval", pLocation->uiInterval_);
      lxpanel_put_bool(pFile, "enabled", pLocation->bEnabled_);
    }

}

/**
 * Definition of the weather plugin class
 *
 *
 */
PluginClass weather_plugin_class =
  {
    // version info
    PLUGINCLASS_VERSIONING,

    // general info
    type : "weather",
    name : N_("Weather Plugin"),
    version : "0.0.1",
    description : N_("Show weather conditions for a location."),

    // system settings
    one_per_system : 0,
    expand_available : 0,

    // API functions
    constructor : weather_constructor,
    destructor : weather_destructor,
    config : weather_configure,
    save : weather_save_configuration,
    panel_configuration_changed : weather_configuration_changed
  };
