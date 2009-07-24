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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "plugin.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>
#include <gdk/gdk.h>
#include <string.h>
#include <stdlib.h>

#include "misc.h"
#include "bg.h"

#include <glib-object.h>

//#define DEBUG
#include "dbg.h"

static GList * pcl = NULL;			/* List of PluginClass structures */

static void register_plugin_class(PluginClass * pc, gboolean dynamic);
static void init_plugin_class_list(void);
static PluginClass * plugin_find_class(const char * type);
static PluginClass * plugin_load_dynamic(const char * type, const gchar * path);
static void plugin_class_unref(PluginClass * pc);

/* Dynamic parameter for static (built-in) plugins must be FALSE so we will not try to unload them */
#define REGISTER_STATIC_PLUGIN_CLASS(pc) \
do {\
    extern PluginClass pc;\
    register_plugin_class(&pc, FALSE);\
} while (0)

/* Register a PluginClass. */
static void register_plugin_class(PluginClass * pc, gboolean dynamic)
{
    pcl = g_list_append(pcl, pc);
    pc->dynamic = dynamic;
}

/* Initialize the static plugins. */
static void init_plugin_class_list(void)
{
#ifdef STATIC_SEPARATOR
    REGISTER_STATIC_PLUGIN_CLASS(separator_plugin_class);
#endif

#ifdef STATIC_LAUNCHBAR
    REGISTER_STATIC_PLUGIN_CLASS(launchbar_plugin_class);
#endif

#ifdef STATIC_DCLOCK
    REGISTER_STATIC_PLUGIN_CLASS(dclock_plugin_class);
#endif

#ifdef STATIC_WINCMD
    REGISTER_STATIC_PLUGIN_CLASS(wincmd_plugin_class);
#endif

#ifdef STATIC_DIRMENU
    REGISTER_STATIC_PLUGIN_CLASS(dirmenu_plugin_class);
#endif

#ifdef STATIC_TASKBAR
    REGISTER_STATIC_PLUGIN_CLASS(taskbar_plugin_class);
#endif

#ifdef STATIC_PAGER
    REGISTER_STATIC_PLUGIN_CLASS(pager_plugin_class);
#endif

#ifdef STATIC_TRAY
    REGISTER_STATIC_PLUGIN_CLASS(tray_plugin_class);
#endif

#ifndef DISABLE_MENU
#ifdef STATIC_MENU
    REGISTER_STATIC_PLUGIN_CLASS(menu_plugin_class);
#endif
#endif

#ifdef STATIC_SPACE
    REGISTER_STATIC_PLUGIN_CLASS(space_plugin_class);
#endif
}

/* Look up a plugin class by name. */
static PluginClass * plugin_find_class(const char * type)
{
    GList * tmp;
    for (tmp = pcl; tmp != NULL; tmp = g_list_next(tmp))
    {
        PluginClass * pc = (PluginClass *) tmp->data;
        if (g_ascii_strcasecmp(type, pc->type) == 0)
            return pc;
    }
    return NULL;
}

/* Load a dynamic plugin. */
static PluginClass * plugin_load_dynamic(const char * type, const gchar * path)
{
    PluginClass * pc = NULL;

    /* Load the external module. */
    GModule * m = g_module_open(path, G_MODULE_BIND_LAZY);
    if (m != NULL)
    {
        /* Formulate the name of the expected external variable of type PluginClass. */
        char class_name[128];
        g_snprintf(class_name, sizeof(class_name), "%s_plugin_class", type);

        /* Validate that the external variable is of type PluginClass. */
        gpointer tmpsym;
        if (( ! g_module_symbol(m, class_name, &tmpsym))	/* Ensure symbol is present */
        || ((pc = tmpsym) == NULL)
        || (pc->structure_size != sizeof(PluginClass))		/* Then check versioning information */
        || (pc->structure_version != PLUGINCLASS_VERSION)
        || (strcmp(type, pc->type) != 0))			/* Then and only then access other fields; check name */
        {
            g_module_close(m);
            ERR("%s.so is not a lxpanel plugin\n", type);
            return NULL;
        }

        /* Register the newly loaded and valid plugin. */
        pc->gmodule = m;
        register_plugin_class(pc, TRUE);
    }
    return pc;
}

/* Create an instance of a plugin with a specified name, loading it if external. */
Plugin * plugin_load(char * type)
{
    /* Initialize static plugins on first call. */
    if (pcl == NULL)
        init_plugin_class_list();

    /* Look up the PluginClass. */
    PluginClass * pc = plugin_find_class(type);

#ifndef DISABLE_PLUGINS_LOADING
    /* If not found and dynamic loading is available, try to locate an external plugin. */
    if ((pc == NULL) && (g_module_supported()))
    {
        gchar path[PATH_MAX];
        g_snprintf(path, sizeof(path), PACKAGE_LIB_DIR "/lxpanel/plugins/%s.so", type);
        pc = plugin_load_dynamic(type, path);
    }
#endif  /* DISABLE_PLUGINS_LOADING */

    /* If not found, return failure. */
    if (pc == NULL)
        return NULL;

    /* Instantiate the plugin */
    Plugin * plug = g_new0(Plugin, 1);
    plug->class = pc;
    pc->count += 1;
    return plug;
}

/* Configure and start a plugin by calling its constructor. */
int plugin_start(Plugin * pl, char ** fp)
{
    /* Call the constructor.
     * It is responsible for parsing the parameters, and setting "pwid" to the top level widget. */
    if ( ! pl->class->constructor(pl, fp))
        return 0;

    /* If this plugin can only be instantiated once, count the instantiation.
     * This causes the configuration system to avoid displaying the plugin as one that can be added. */
    if (pl->class->one_per_system)
        pl->class->one_per_system_instantiated = TRUE;

    /* If the plugin has a top level widget, add it to the panel's container. */
    if (pl->pwid != NULL)
    {
        gtk_widget_set_name(pl->pwid, pl->class->type);
        gtk_box_pack_start(GTK_BOX(pl->panel->box), pl->pwid, pl->expand, TRUE, pl->padding);
        gtk_container_set_border_width(GTK_CONTAINER(pl->pwid), pl->border);
        gtk_widget_show(pl->pwid);
    }
    return 1;
}

/* Unload a plugin if initialization fails. */
void plugin_unload(Plugin * pl)
{
    plugin_class_unref(pl->class);
    g_free(pl);
}

/* Delete a plugin. */
void plugin_delete(Plugin * pl)
{
    Panel * p = pl->panel;
    PluginClass * pc = pl->class;

    /* If a plugin configuration dialog is open, close it. */
    if (p->plugin_pref_dialog != NULL)
    {
        gtk_widget_destroy(p->plugin_pref_dialog);
        p->plugin_pref_dialog = NULL;
    }

    /* Run the destructor and then destroy the top level widget.
     * This prevents problems with the plugin destroying child widgets. */
    pc->destructor(pl);
    if (pl->pwid != NULL)
        gtk_widget_destroy(pl->pwid);

    /* Data structure bookkeeping. */
    pc->one_per_system_instantiated = FALSE;
    plugin_class_unref(pc);

    /* Free the Plugin structure. */
    g_free(pl);
}

/* Unreference a dynamic plugin. */
static void plugin_class_unref(PluginClass * pc)
{
    pc->count -= 1;

    /* If the reference count drops to zero, unload the plugin if it is dynamic and has declared itself unloadable. */
    if ((pc->count == 0)
    && (pc->dynamic)
    && ( ! pc->not_unloadable))
    {
        pcl = g_list_remove(pcl, pc);
        g_module_close(pc->gmodule);
    }
}

/* Get a list of all available plugin classes.
 * Returns a newly allocated GList which should be freed with plugin_class_list_free(list). */
GList * plugin_get_available_classes(void)
{
    /* Initialize static plugins on first call. */
    if (pcl == NULL)
        init_plugin_class_list();

    /* Loop over all classes to formulate the result.
     * Increase the reference count; it will be decreased in plugin_class_list_free. */
    GList * classes = NULL;
    GList * l;
    for (l = pcl; l != NULL; l = l->next)
    {
        PluginClass * pc = (PluginClass *) l->data;
        classes = g_list_prepend(classes, pc);
        pc->count += 1;
    }

#ifndef DISABLE_PLUGINS_LOADING
    GDir * dir = g_dir_open(PACKAGE_LIB_DIR "/lxpanel/plugins", 0, NULL);
    if (dir != NULL)
    {
        const char * file;
        while ((file = g_dir_read_name(dir)) != NULL)
        {
            if (g_str_has_suffix(file, ".so"))
            {
                char * type = g_strndup(file, strlen(file) - 3);
                if (plugin_find_class(type) == NULL)
                {
                    /* If it has not been loaded, do it.  If successful, add it to the result. */
                    char * path = g_build_filename(PACKAGE_LIB_DIR "/lxpanel/plugins", file, NULL );
                    PluginClass * pc = plugin_load_dynamic(type, path);
                    if (pc != NULL)
                    {
                        pc->count += 1;
                        classes = g_list_prepend(classes, pc);
                    }
                    g_free(path);
                }
                g_free(type);
            }
        }
        g_dir_close(dir);
    }
#endif
    return classes;
}

/* Free the list allocated by plugin_get_available_classes. */
void plugin_class_list_free(GList * list)
{
   g_list_foreach(list, (GFunc) plugin_class_unref, NULL);
   g_list_free(list);
}

/* Recursively set the background of all widgets on a panel background configuration change. */
void plugin_widget_set_background(GtkWidget * w, Panel * p)
{
    if (w != NULL)
    {
        if ( ! GTK_WIDGET_NO_WINDOW(w))
        {
            if ((p->background) || (p->transparent))
            {
                if (GTK_WIDGET_REALIZED(w))
                {
                    panel_determine_background_pixmap(p, w, w->window);
                    gdk_window_invalidate_rect(w->window, NULL, TRUE);
                }
            }
            else
            {
                /* Set background according to the current GTK style. */
                gtk_widget_set_app_paintable(w, FALSE);
                if (GTK_WIDGET_REALIZED(w))
                {
                    gdk_window_set_back_pixmap(w->window, NULL, TRUE);
                    gtk_style_set_background(w->style, w->window, GTK_STATE_NORMAL);
                }
            }
        }

        /* Special handling to get tray icons redrawn. */
        if (GTK_IS_SOCKET(w))
        {
            gtk_widget_hide(w);
            gdk_window_process_all_updates();
            gtk_widget_show(w);
            gdk_window_process_all_updates();
        }

        /* Recursively process all children of a container. */
        if (GTK_IS_CONTAINER(w))
            gtk_container_foreach(GTK_CONTAINER(w), (GtkCallback) plugin_widget_set_background, p);
    }
}

/* Handler for "button_press_event" signal with Plugin as parameter.
 * External so can be used from a plugin. */
gboolean plugin_button_press_event(GtkWidget *widget, GdkEventButton *event, Plugin *plugin)
{
    if (event->button == 3)	 /* right button */
    {
        GtkMenu* popup = (GtkMenu*) lxpanel_get_panel_menu(plugin->panel, plugin, FALSE);
        gtk_menu_popup(popup, NULL, NULL, NULL, NULL, event->button, event->time);
        return TRUE;
    }    
    return FALSE;
}

/* Helper for position-calculation callback for popup menus. */
void plugin_popup_set_position_helper(Plugin * p, GtkWidget * near, GtkWidget * popup, GtkRequisition * popup_req, gint * px, gint * py)
{
    /* Get the origin of the requested-near widget in screen coordinates. */
    gint x, y;
    gdk_window_get_origin(GDK_WINDOW(near->window), &x, &y);
    x += near->allocation.x;
    y += near->allocation.y;

    /* Dispatch on edge to lay out the popup menu with respect to the button.
     * Also set "push-in" to avoid any case where it might flow off screen. */
    switch (p->panel->edge)
    {
        case EDGE_TOP:          y += near->allocation.height;         break;
        case EDGE_BOTTOM:       y -= popup_req->height;                break;
        case EDGE_LEFT:         x += near->allocation.width;          break;
        case EDGE_RIGHT:        x -= popup_req->width;                 break;
    }
    *px = x;
    *py = y;
}
