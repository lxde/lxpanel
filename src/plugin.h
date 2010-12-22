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

#ifndef PLUGIN_H
#define PLUGIN_H

#include <gmodule.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <stdio.h>
#include "panel.h"

struct _Plugin;

/* Support for external plugin versioning.
 * Plugins must invoke PLUGINCLASS_VERSIONING when they instantiate PluginClass. */
#define PLUGINCLASS_VERSION 1
#define PLUGINCLASS_VERSIONING \
    structure_size : sizeof(PluginClass), \
    structure_version : PLUGINCLASS_VERSION

/* Representative of an available plugin. */
typedef struct {

    /* Keep these first.  Do not make unnecessary changes in structure layout. */
    unsigned short structure_size;		/* Structure size, for versioning support */
    unsigned short structure_version;		/* Structure version, for versioning support */

    char * fname;				/* Plugin file pathname */
    int count;					/* Reference count */
    GModule * gmodule;				/* Associated GModule structure */

    int dynamic : 1;				/* True if dynamically loaded */
    int unused_invisible : 1;			/* Unused; reserved bit */
    int not_unloadable : 1;			/* Not unloadable due to GModule restriction */
    int one_per_system : 1;			/* Special: only one possible per system, such as system tray */
    int one_per_system_instantiated : 1;	/* True if one instance exists */
    int expand_available : 1;			/* True if "stretch" option is available */
    int expand_default : 1;			/* True if "stretch" option is default */

    /* These fields point within the plugin image. */
    char * type;				/* Internal name of plugin, to match external filename */
    char * name;				/* Display name of plugin for selection UI */
    char * version;				/* Version of plugin */
    char * description;				/* Brief textual description of plugin for selection UI */

    int (*constructor)(struct _Plugin * plugin, char ** fp);		/* Create an instance of the plugin */
    void (*destructor)(struct _Plugin * plugin);			/* Destroy an instance of the plugin */
    void (*config)(struct _Plugin * plugin, GtkWindow * parent);	/* Request the plugin to display its configuration dialog */
    void (*save)(struct _Plugin * plugin, FILE * fp);			/* Request the plugin to save its configuration to a file */
    void (*panel_configuration_changed)(struct _Plugin * plugin);	/* Request the plugin to do a full redraw after a panel configuration change */
} PluginClass;

/* Representative of a loaded and active plugin attached to a panel. */
typedef struct _Plugin {
    PluginClass * class;			/* Back pointer to PluginClass */
    Panel * panel;				/* Back pointer to Panel */
    GtkWidget * pwid;				/* Top level widget; plugin allocates, but plugin mechanism, not plugin itself, destroys this */
    int expand;					/* Expand ("stretch") setting for container */
    int padding;				/* Padding setting for container */
    int border;					/* Border setting for container */
    gpointer priv;				/* Private context for plugin; plugin frees this in its destructor */
} Plugin;

extern Plugin * plugin_load(char * type);		/* Create an instance of a plugin, loading it if necessary */
extern int plugin_start(Plugin * this, char ** fp);	/* Configure and start a plugin by calling its constructor */
extern void plugin_unload(Plugin * pl);			/* Delete an instance of a plugin if initialization fails */
extern void plugin_delete(Plugin * pl);			/* Delete an instance of a plugin */
extern GList * plugin_get_available_classes(void);	/* Get a list of all plugin classes; free with plugin_class_list_free */
extern void plugin_class_list_free(GList * list);	/* Free the list allocated by plugin_get_available_classes */
extern void plugin_widget_set_background(GtkWidget * w, Panel * p);
							/* Recursively set the background of all widgets on a panel background configuration change */
extern gboolean plugin_button_press_event(GtkWidget *widget, GdkEventButton *event, Plugin *plugin);
                                                        /* Handler for "button_press_event" signal with Plugin as parameter */
extern void plugin_popup_set_position_helper(Plugin * p, GtkWidget * near, GtkWidget * popup, GtkRequisition * popup_req, gint * px, gint * py);
							/* Helper for position-calculation callback for popup menus */
extern void plugin_adjust_popup_position(GtkWidget * popup, Plugin * plugin);
							/* Helper to move popup windows away from the panel */

/* FIXME: optional definitions */
#define STATIC_SEPARATOR
#define STATIC_LAUNCHBAR
#define STATIC_DCLOCK
#define STATIC_WINCMD
#define STATIC_DIRMENU
#define STATIC_TASKBAR
#define STATIC_PAGER
#define STATIC_TRAY
#define STATIC_MENU
#define STATIC_SPACE
#define STATIC_ICONS

#endif
