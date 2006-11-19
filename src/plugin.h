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

struct _plugin *stam;

typedef struct {
    /* common */
    char *fname;
    int count;
    GModule *gmodule;

    int dynamic : 1;
    int invisible : 1;
    /* these fields are pointers to the data within loaded dll */
    char *type;
    char *name;
    char *version;
    char *description;

    int (*constructor)(struct _plugin *this, char **fp);
    void (*destructor)(struct _plugin *this);
    void (*config)(struct _plugin *this, GtkWindow* parent); /* config UI */
    void (*save)(struct _plugin *this, FILE* fp);
    void (*orientation)(struct _plugin *this);
} plugin_class;

typedef struct _plugin{
    plugin_class *class;
    panel        *panel;
    GtkWidget    *pwid;
    int           expand;
    int           padding;
    int           border;
    gpointer      priv;
} plugin;

/* if plugin is external it will load its dll */
plugin * plugin_load(char *type);
void plugin_put(plugin *this);
int plugin_start(plugin *this, char **fp);
void plugin_stop(plugin *this);

void plugin_class_unref( plugin_class* pc );

/*
   Get a list of all available plugin classes
   Return a newly allocated GList which should be freed with following code:
   g_list_foreach( list, plugin_class_unref, NULL );
   g_list_free( list );
*/
GList* plugin_get_available_classes();

/* FIXME: optional definitions */
#define STATIC_SEPARATOR
#define STATIC_IMAGE
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
