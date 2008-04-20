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

    int (*constructor)(struct _Plugin *this, char **fp);
    void (*destructor)(struct _Plugin *this);
    void (*config)(struct _Plugin *this, GtkWindow* parent); /* config UI */
    void (*save)(struct _Plugin *this, FILE* fp);
    void (*orientation)(struct _Plugin *this);
} PluginClass;

typedef struct _Plugin{
    PluginClass *class;
    Panel        *panel;
    GtkWidget    *pwid;
    int           expand;
    int           padding;
    int           border;
    gpointer      priv;
} Plugin;

/* if plugin is external it will load its dll */
Plugin * plugin_load(char *type);
void plugin_put(Plugin *this);
int plugin_start(Plugin *this, char **fp);
void plugin_stop(Plugin *this);

void plugin_class_unref( PluginClass* pc );

/*
   Get a list of all available plugin classes
   Return a newly allocated GList which should be freed with
   plugin_class_list_free( list );
*/
GList* plugin_get_available_classes();
void plugin_class_list_free( GList* classes );

void plugin_set_background( Plugin* pl, Panel* p );
void plugin_widget_set_background( GtkWidget* w, Panel* p );

/* FIXME: optional definitions */
#define STATIC_SEPARATOR
/* #define STATIC_IMAGE */
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

#if 0
/* Try to handle GTypePlugin problems, but in vain. :-( */
#define LX_TYPE_TYPE_PLUGIN           (lx_type_plugin_get_type ())
#define LX_TYPE_PLUGIN(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), LX_TYPE_TYPE_PLUGIN, LXTypePlugin))
#define LX_TYPE_PLUGIN_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST ((klass), LX_TYPE_TYPE_PLUGIN, LXTypePluginClass))
#define LX_IS_TYPE_PLUGIN(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), LX_TYPE_TYPE_PLUGIN))

typedef struct _LXTypePlugin LXTypePlugin;
typedef struct _LXTypePluginClass LXTypePluginClass;

struct _LXTypePlugin {
    GObject parent;
};

struct _LXTypePluginClass {
    GObjectClass parent;
};

GTypePlugin* lx_type_plugin_get( const char* plugin_name );
#endif

#endif
