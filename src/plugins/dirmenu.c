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

#include <stdlib.h>
#include <unistd.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>
#include <string.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "dbg.h"

/* Temporary for sort of directory names. */
typedef struct _directory_name {
    struct _directory_name * flink;
    char * directory_name;
    char * directory_name_collate_key;
} DirectoryName;

/* Private context for directory menu plugin. */
typedef struct {
    Plugin * plugin;			/* Back pointer to plugin */
    char * image;			/* Icon for top level widget */
    char * path;			/* Top level path for widget */
    char * name;			/* User's label for widget */
    GdkPixbuf * folder_icon;		/* Icon for folders */
} DirMenuPlugin;

static void dirmenu_open_in_file_manager(Plugin * p, const char * path);
static void dirmenu_open_in_terminal(Plugin * p, const char * path);
static void dirmenu_menuitem_open_directory(GtkWidget * item, Plugin * p);
static void dirmenu_menuitem_open_in_terminal(GtkWidget * item, Plugin * p);
static void dirmenu_menuitem_select(GtkMenuItem * item, Plugin * p);
static void dirmenu_menuitem_deselect(GtkMenuItem * item, Plugin * p);
void dirmenu_menu_selection_done(GtkWidget * menu, Plugin * p);
static void dirmenu_popup_set_position(GtkWidget * menu, gint * px, gint * py, gboolean * push_in, Plugin * p);
static GtkWidget * dirmenu_create_menu(Plugin * p, const char * path, gboolean open_at_top);
static void dirmenu_show_menu(GtkWidget * widget, Plugin * p, int btn, guint32 time);
static gboolean dirmenu_button_press_event(GtkWidget * widget, GdkEventButton * event, Plugin * p);
static int dirmenu_constructor(Plugin * p, char ** fp);
static void dirmenu_destructor(Plugin * p);
static void dirmenu_apply_configuration_to_children(GtkWidget * w, DirMenuPlugin * dm);
static void dirmenu_apply_configuration(Plugin * p);
static void dirmenu_configure(Plugin * p, GtkWindow * parent);
static void dirmenu_save_configuration(Plugin * p, FILE * fp);
static void dirmenu_panel_configuration_changed(Plugin * p);

/* Open a specified path in a file manager. */
static void dirmenu_open_in_file_manager(Plugin * p, const char * path)
{
    char * quote = g_shell_quote(path);
    const char * fm = lxpanel_get_file_manager();
    char * cmd = ((strstr(fm, "%s") != NULL) ? g_strdup_printf(fm, quote) : g_strdup_printf("%s %s", fm, quote));
    g_free(quote);
    g_spawn_command_line_async(cmd, NULL);
    g_free(cmd);
}

/* Open a specified path in a terminal. */
static void dirmenu_open_in_terminal(Plugin * p, const char * path)
{
    const char * term = lxpanel_get_terminal();
    char * argv[2];
    char * sp = strchr(term, ' ');
    argv[0] = ((sp != NULL) ? g_strndup(term, sp - term) : (char *) term);
    argv[1] = NULL;
    g_spawn_async(path, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL);
    if (argv[0] != term)
        g_free(argv[0]);
}

/* Handler for activate event on popup Open menu item. */
static void dirmenu_menuitem_open_directory(GtkWidget * item, Plugin * p)
{
    dirmenu_open_in_file_manager(p, g_object_get_data(G_OBJECT(gtk_widget_get_parent(item)), "path"));
}

/* Handler for activate event on popup Open In Terminal menu item. */
static void dirmenu_menuitem_open_in_terminal(GtkWidget * item, Plugin * p)
{
    dirmenu_open_in_terminal(p, g_object_get_data(G_OBJECT(gtk_widget_get_parent(item)), "path"));
}

/* Handler for select event on popup menu item. */
static void dirmenu_menuitem_select(GtkMenuItem * item, Plugin * p)
{
    GtkWidget * sub = gtk_menu_item_get_submenu(item);
    if (sub != NULL)
    {
        /* On first reference, populate the submenu using the parent directory and the item directory name. */
        GtkMenu * parent = GTK_MENU(gtk_widget_get_parent(GTK_WIDGET(item)));
        char * path = (char *) g_object_get_data(G_OBJECT(sub), "path");
        if (path == NULL)
        {
            path = g_build_filename(
                (char *) g_object_get_data(G_OBJECT(parent), "path"),
                (char *) g_object_get_data(G_OBJECT(item), "name"),
                NULL);
            sub = dirmenu_create_menu(p, path, TRUE);
            g_free(path);
            gtk_menu_item_set_submenu(item, sub);
        }
    }
}

/* Handler for deselect event on popup menu item. */
static void dirmenu_menuitem_deselect(GtkMenuItem * item, Plugin * p)
{
    /* Delete old menu on deselect to save resource. */
    gtk_menu_item_set_submenu(item, gtk_menu_new());
}

/* Handler for selection-done event on popup menu. */
void dirmenu_menu_selection_done(GtkWidget * menu, Plugin * p)
{
    gtk_widget_destroy(menu);
}

/* Position-calculation callback for popup menu. */
static void dirmenu_popup_set_position(GtkWidget * menu, gint * px, gint * py, gboolean * push_in, Plugin * p)
{
    /* Get the allocation of the popup menu. */
    GtkRequisition popup_req;
    gtk_widget_size_request(menu, &popup_req);

    /* Determine the coordinates. */
    plugin_popup_set_position_helper(p, p->pwid, menu, &popup_req, px, py);
    *push_in = TRUE;
}

/* Create a menu populated with all subdirectories. */
static GtkWidget * dirmenu_create_menu(Plugin * p, const char * path, gboolean open_at_top)
{
    DirMenuPlugin * dm = (DirMenuPlugin *) p->priv;

    /* Create a menu. */
    GtkWidget * menu = gtk_menu_new();

    if (dm->folder_icon == NULL)
    {
        int w;
        int h;
        gtk_icon_size_lookup_for_settings(gtk_widget_get_settings(menu), GTK_ICON_SIZE_MENU, &w, &h);
        dm->folder_icon = gtk_icon_theme_load_icon(
            gtk_icon_theme_get_default(),
            "gnome-fs-directory", MAX(w, h), 0, NULL);
        if (dm->folder_icon == NULL)
            dm->folder_icon = gtk_widget_render_icon(menu, GTK_STOCK_DIRECTORY, GTK_ICON_SIZE_MENU, NULL);
    }

    g_object_set_data_full(G_OBJECT(menu), "path", g_strdup(path), g_free);

    /* Scan the specified directory to populate the menu with its subdirectories. */
    DirectoryName * dir_list = NULL;
    GDir * dir = g_dir_open(path, 0, NULL);
    if (dir != NULL)
    {
        const char * name;
        while ((name = g_dir_read_name(dir)) != NULL)	/* Memory owned by glib */
        {
            /* Omit hidden files. */
            if (name[0] != '.')
            {
                char * full = g_build_filename(path, name, NULL);
                if (g_file_test(full, G_FILE_TEST_IS_DIR))
                {
                    /* Convert name to UTF-8 and to the collation key. */
                    char * directory_name = g_filename_display_name(name);
                    char * directory_name_collate_key = g_utf8_collate_key(directory_name, -1);

                    /* Locate insertion point. */
                    DirectoryName * dir_pred = NULL;
                    DirectoryName * dir_cursor;
                    for (dir_cursor = dir_list; dir_cursor != NULL; dir_pred = dir_cursor, dir_cursor = dir_cursor->flink)
                    {
                        if (strcmp(directory_name_collate_key, dir_cursor->directory_name_collate_key) <= 0)
                            break;
                    }

                    /* Allocate and initialize sorted directory name entry. */
                    dir_cursor = g_new0(DirectoryName, 1);
                    dir_cursor->directory_name = directory_name;
                    dir_cursor->directory_name_collate_key = directory_name_collate_key;
                    if (dir_pred == NULL)
                    {
                        dir_cursor->flink = dir_list;
                        dir_list = dir_cursor;
                    }
                    else
                    {
                        dir_cursor->flink = dir_pred->flink;
                        dir_pred->flink = dir_cursor;
                    }
                }
                g_free(full);
            }
        }
        g_dir_close(dir);
    }

    /* The sorted directory name list is complete.  Loop to create the menu. */
    DirectoryName * dir_cursor;
    while ((dir_cursor = dir_list) != NULL)
    {
        /* Create and initialize menu item. */
        GtkWidget * item = gtk_image_menu_item_new_with_label(dir_cursor->directory_name);
        gtk_image_menu_item_set_image(
            GTK_IMAGE_MENU_ITEM(item),
            gtk_image_new_from_stock(GTK_STOCK_DIRECTORY, GTK_ICON_SIZE_MENU));
        GtkWidget * dummy = gtk_menu_new();
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), dummy);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

        /* Unlink and free sorted directory name element, but reuse the directory name string. */
        dir_list = dir_cursor->flink;
        g_object_set_data_full(G_OBJECT(item), "name", dir_cursor->directory_name, g_free);
        g_free(dir_cursor->directory_name_collate_key);
        g_free(dir_cursor);

        /* Connect signals. */
        g_signal_connect(G_OBJECT(item), "select", G_CALLBACK(dirmenu_menuitem_select), p);
        g_signal_connect(G_OBJECT(item), "deselect", G_CALLBACK(dirmenu_menuitem_deselect), p);
    }

    /* Create "Open" and "Open in Terminal" items. */
    GtkWidget * item = gtk_image_menu_item_new_from_stock( GTK_STOCK_OPEN, NULL );
    g_signal_connect(item, "activate", G_CALLBACK(dirmenu_menuitem_open_directory), p);
    GtkWidget * term = gtk_menu_item_new_with_mnemonic( _("Open in _Terminal") );
    g_signal_connect(term, "activate", G_CALLBACK(dirmenu_menuitem_open_in_terminal), p);

    /* Insert or append based on caller's preference. */
    if (open_at_top)
    {
        gtk_menu_shell_insert(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new(), 0);
        gtk_menu_shell_insert(GTK_MENU_SHELL(menu), term, 0);
        gtk_menu_shell_insert(GTK_MENU_SHELL(menu), item, 0);
    }
    else {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), term);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }

    /* Show the menu and return. */
    gtk_widget_show_all(menu);
    return menu;
}

/* Show a menu of subdirectories. */
static void dirmenu_show_menu(GtkWidget * widget, Plugin * p, int btn, guint32 time)
{
    DirMenuPlugin * dm = (DirMenuPlugin *) p->priv;

    /* Create a menu populated with all subdirectories. */
    GtkWidget * menu = dirmenu_create_menu(
        p,
        ((dm->path != NULL) ? expand_tilda(dm->path) : g_get_home_dir()),
        FALSE);
    g_signal_connect(menu, "selection-done", G_CALLBACK(dirmenu_menu_selection_done), NULL);

    /* Show the menu.  Use a positioning function to get it placed next to the top level widget. */
    gtk_menu_popup(GTK_MENU(menu), NULL, NULL, (GtkMenuPositionFunc) dirmenu_popup_set_position, p, btn, time);
}

/* Handler for button-press-event on top level widget. */
static gboolean dirmenu_button_press_event(GtkWidget * widget, GdkEventButton * event, Plugin * p)
{
    DirMenuPlugin * dm = (DirMenuPlugin *) p->priv;

    /* Standard left-click handling. */
    if (plugin_button_press_event(widget, event, p))
        return TRUE;

    if (event->button == 1)
    {
        dirmenu_show_menu(widget, p, event->button, event->time);
    }
    else
    {
        dirmenu_open_in_terminal(p, ((dm->path != NULL) ? expand_tilda(dm->path) : g_get_home_dir()));
    }
    return TRUE;
}

/* Plugin constructor. */
static int dirmenu_constructor(Plugin * p, char ** fp)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    DirMenuPlugin * dm = g_new0(DirMenuPlugin, 1);
    dm->plugin = p;
    p->priv = dm;

    /* Load parameters from the configuration file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "dirmenu: illegal token %s\n", s.str);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "image") == 0)
                    dm->image = g_strdup(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "path") == 0)
                    dm->path = g_strdup(s.t[1]);
		else if (g_ascii_strcasecmp(s.t[0], "name") == 0)
                    dm->name = g_strdup( s.t[1] );
                else
                    ERR( "dirmenu: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "dirmenu: illegal in this context %s\n", s.str);
                return 0;
            }
        }
    }

    /* Allocate top level widget and set into Plugin widget pointer.
     * It is not known why, but the button text will not draw if it is edited from empty to non-empty
     * unless this strategy of initializing it with a non-empty value first is followed. */
    p->pwid = fb_button_new_from_file_with_label(
        ((dm->image != NULL) ? dm->image : "file-manager"),
        p->panel->icon_size, p->panel->icon_size, PANEL_ICON_HIGHLIGHT, TRUE, p->panel, "Temp");
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 0);
    g_signal_connect(p->pwid, "button_press_event", G_CALLBACK(dirmenu_button_press_event), p);

    /* Initialize the widget. */
    dirmenu_apply_configuration(p);

    /* Show the widget and return. */
    gtk_widget_show(p->pwid);
    return 1;
}

/* Plugin destructor. */
static void dirmenu_destructor(Plugin * p)
{
    DirMenuPlugin * dm = (DirMenuPlugin *) p->priv;

    /* Release a reference on the folder icon if held. */
    if (dm->folder_icon)
        g_object_unref(dm->folder_icon);

    /* Deallocate all memory. */
    g_free(dm->image);
    g_free(dm->path);
    g_free(dm->name);
    g_free(dm);
}

/* Recursively apply a configuration change. */
static void dirmenu_apply_configuration_to_children(GtkWidget * w, DirMenuPlugin * dm)
{
    if (GTK_IS_CONTAINER(w))
	gtk_container_foreach(GTK_CONTAINER(w), (GtkCallback) dirmenu_apply_configuration_to_children, (gpointer) dm);
    else if (GTK_IS_LABEL(w))
    {
        if (dm->name == NULL)
	    gtk_label_set_text(GTK_LABEL(w), NULL);
        else
            panel_draw_label_text(dm->plugin->panel, w, dm->name, FALSE, TRUE);
    }
}

/* Callback when the configuration dialog has recorded a configuration change. */
static void dirmenu_apply_configuration(Plugin * p)
{
    DirMenuPlugin * dm = (DirMenuPlugin *) p->priv;

    fb_button_set_from_file(p->pwid,
        ((dm->image != NULL) ? dm->image : "file-manager"),
        ((dm->image != NULL) ? -1 : p->panel->icon_size), p->panel->icon_size, TRUE);

    gtk_widget_set_tooltip_text(p->pwid, ((dm->path != NULL) ? expand_tilda(dm->path) : g_get_home_dir()));
    gtk_container_foreach(GTK_CONTAINER(p->pwid), (GtkCallback) dirmenu_apply_configuration_to_children, (gpointer) dm);
}

/* Callback when the configuration dialog is to be shown. */
static void dirmenu_configure(Plugin * p, GtkWindow * parent)
{

    DirMenuPlugin * dm = (DirMenuPlugin *) p->priv;
    GtkWidget * dlg = create_generic_config_dlg(
        _(p->class->name),
        GTK_WIDGET(parent),
        (GSourceFunc) dirmenu_apply_configuration, (gpointer) p,
        _("Directory"), &dm->path, CONF_TYPE_DIRECTORY_ENTRY,
        _("Label"), &dm->name, CONF_TYPE_STR,
        _("Icon"), &dm->image, CONF_TYPE_FILE_ENTRY,
        NULL);
    gtk_window_present(GTK_WINDOW(dlg));
}

/* Callback when the configuration is to be saved. */
static void dirmenu_save_configuration(Plugin * p, FILE * fp)
{
    DirMenuPlugin * dm = (DirMenuPlugin *) p->priv;
    lxpanel_put_str(fp, "path", dm->path);
    lxpanel_put_str(fp, "name", dm->name);
    lxpanel_put_str(fp, "image", dm->image);
}

/* Callback when panel configuration changes. */
static void dirmenu_panel_configuration_changed(Plugin * p)
{
    DirMenuPlugin * dm = (DirMenuPlugin *) p->priv;
    fb_button_set_from_file(p->pwid,
        ((dm->image != NULL) ? dm->image : "file-manager"),
        p->panel->icon_size, p->panel->icon_size, TRUE);
    dirmenu_apply_configuration(p);
}

/* Plugin descriptor. */
PluginClass dirmenu_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "dirmenu",
    name : N_("Directory Menu"),
    version: "1.0",
    description : N_("Browse directory tree via menu (Author: PCMan)"),

    constructor : dirmenu_constructor,
    destructor  : dirmenu_destructor,
    config : dirmenu_configure,
    save : dirmenu_save_configuration,
    panel_configuration_changed : dirmenu_panel_configuration_changed

};
