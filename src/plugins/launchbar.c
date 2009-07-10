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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include <menu-cache.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "icon-grid.h"
#include "menu-policy.h"

#include "dbg.h"

#include "glib-mem.h"

/* Drag and drop target info. */
enum {
  TARGET_URILIST,
  TARGET_UTF8_STRING,
  TARGET_STRING,
  TARGET_TEXT,
  TARGET_COMPOUND_TEXT
};

static const GtkTargetEntry target_table[] = {
    { "text/uri-list", 0, TARGET_URILIST},
    { "UTF8_STRING", 0, TARGET_UTF8_STRING },
    { "COMPOUND_TEXT", 0, 0 },
    { "TEXT",          0, 0 },
    { "STRING",        0, 0 }
};

/* Column definitions for configuration dialogs. */
enum {
    COL_ICON,
    COL_TITLE,
    COL_BTN,
    N_COLS
};

static const char desktop_ent[] = "Desktop Entry";

/* Representative of one launch button.
 * Note that the launch parameters come from the specified desktop file, or from the configuration file.
 * This structure is also used during the "add to launchbar" dialog to hold menu items. */
typedef struct {
    Plugin * plugin;			/* Back pointer to plugin */
    GtkWidget * widget;			/* Pointer to button */
    gchar * desktop_id;			/* Name of application (desktop file name less the .desktop) */
    gchar * image;			/* Image icon (from Icon entry) */
    gchar * action;			/* Action (from Exec entry) */
    gchar * tooltip;			/* Tooltip (from Name entry) */
    guchar use_terminal : 1;		/* True if Terminal=true or from configuration file */
    guchar customize_image : 1;		/* True if image icon from configuration file */
    guchar customize_action : 1;	/* True if action from configuration file */
    guchar customize_tooltip : 1;	/* True if tooltip from configuration file */
} LaunchButton;

/* Private context for launchbar plugin. */
typedef struct {
    IconGrid * icon_grid;		/* Icon grid managing the container */
    GSList * buttons;			/* Launchbar buttons */
    GtkWidget * config_dlg;		/* Configuration dialog */
    LaunchButton * bootstrap_button;	/* Bootstrapping button for empty launchbar */
} LaunchbarPlugin;

void panel_config_save(Panel * panel);  /* defined in configurator.c */

static void launchbutton_free(LaunchButton * btn);
static gboolean launchbutton_press_event(GtkWidget * widget, GdkEventButton * event, LaunchButton * b);
static void launchbutton_drag_data_received_event(
    GtkWidget * widget,
    GdkDragContext * context,
    gint x,
    gint y,
    GtkSelectionData * sd,
    guint info,
    guint time,
    LaunchButton * b);
static void launchbutton_build_bootstrap(Plugin * p);
static void launchbutton_build_gui(Plugin * p, LaunchButton * btn);
static int launchbutton_constructor(Plugin * p, char ** fp);
static int launchbar_constructor(Plugin * p, char ** fp);
static void launchbar_destructor(Plugin * p);
static void launchbar_configure_menu_item_add_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_add_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_remove_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_move_up_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_move_down_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_response(GtkDialog * dlg, int response, Plugin * p);
static void launchbar_configure_initialize_list(Plugin * p, GtkWidget * dlg, GtkTreeView * view, gboolean from_menu);
static void launchbar_configure(Plugin * p, GtkWindow * parent);
static void launchbar_save_configuration(Plugin * p, FILE * fp);
static void launchbar_panel_configuration_changed(Plugin * p);

/* Deallocate a LaunchButton. */
static void launchbutton_free(LaunchButton * btn)
{
    g_free(btn->desktop_id);
    g_free(btn->image);
    g_free(btn->action);
    g_free(btn->tooltip);
    g_free(btn);
}

/* Handler for "button-press-event" event from launchbar button. */
static gboolean launchbutton_press_event(GtkWidget * widget, GdkEventButton * event, LaunchButton * b)
{
    GtkWidget *image;

    /* Standard right-click handling. */
    if (plugin_button_press_event(widget, event, b->plugin))
        return TRUE;

    if (event->button == 1)    /* left button */
    {
        if (b->desktop_id == NULL)	/* The bootstrap button */
            launchbar_configure(b->plugin, NULL);
        else if (b->action != NULL)
            lxpanel_launch_app(b->action, NULL, b->use_terminal);
    }
    return TRUE;
}

/* Handler for "drag-data-received" event from launchbar button. */
static void launchbutton_drag_data_received_event(
    GtkWidget * widget,
    GdkDragContext * context,
    gint x,
    gint y,
    GtkSelectionData * sd,
    guint info,
    guint time,
    LaunchButton * b)
{
    if (sd->length > 0)
    {
        if (info == TARGET_URILIST)
        {
            gchar * s = (gchar *) sd->data;
            gchar * end = s + sd->length;
            gchar * str = g_strdup(b->action);
            while (s < end)
            {
                while (s < end && g_ascii_isspace(*s))
                    s++;
                gchar * e = s;
                while (e < end && !g_ascii_isspace(*e))
                    e++;
                if (s != e)
                {
                    *e = 0;
                    s = g_filename_from_uri(s, NULL, NULL);
                    if (s)
                    {
                        gchar * tmp = g_strconcat(str, " '", s, "'", NULL);
                        g_free(str);
                        g_free(s);
                        str = tmp;
                    }
                }
                s = e+1;
            }

            g_spawn_command_line_async(str, NULL);
            g_free(str);
        }
    }
}

/* Build the graphic elements for the bootstrap launchbar button. */
static void launchbutton_build_bootstrap(Plugin * p)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    if (lb->bootstrap_button == NULL)
    {
        /* Build a button that has the stock "Add" icon.
         * The "desktop-id" being NULL is the marker that this is the bootstrap button. */
        lb->bootstrap_button = g_new0(LaunchButton, 1);
        lb->bootstrap_button->plugin = p;
        lb->bootstrap_button->widget = gtk_button_new();
        gtk_button_set_image(GTK_BUTTON(lb->bootstrap_button->widget),
            gtk_image_new_from_pixbuf(lxpanel_load_icon(GTK_STOCK_ADD, p->panel->icon_size, p->panel->icon_size, FALSE)));
        g_signal_connect(lb->bootstrap_button->widget, "button-press-event", G_CALLBACK(launchbutton_press_event), lb->bootstrap_button);

        /* Add the bootstrap button to the icon grid.  By policy it is empty at this point. */
        icon_grid_add(lb->icon_grid, lb->bootstrap_button->widget, TRUE); 
    }
    else
        icon_grid_set_visible(lb->icon_grid, lb->bootstrap_button->widget, TRUE);
}

/* Build the graphic elements for a launchbar button.  The desktop_id field is already established. */
static void launchbutton_build_gui(Plugin * p, LaunchButton * btn)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    if (btn->desktop_id != NULL)
    {
        /* There is a valid desktop file name.  Try to open it. */
        GKeyFile * desktop = g_key_file_new();
        
	gchar * desktop_file = NULL;
        gboolean loaded;	
	if (g_path_is_absolute(btn->desktop_id))
        {
            desktop_file = g_strdup(btn->desktop_id);
            loaded = g_key_file_load_from_file(desktop, desktop_file, G_KEY_FILE_NONE, NULL );
	}
	else 
	{
            /* Load from the freedesktop.org specified data directories. */
            gchar * full_id = g_strconcat("applications/", btn->desktop_id, NULL);
            loaded = g_key_file_load_from_data_dirs(
                desktop, full_id, &desktop_file, G_KEY_FILE_NONE, NULL);
            g_free(full_id);
        }

	if (loaded)
        {
            /* Desktop file located.  Get Icon, Name, Exec, and Terminal parameters. */
            gchar * icon = g_key_file_get_string(desktop, desktop_ent, "Icon", NULL);
            gchar * title = g_key_file_get_locale_string(desktop, desktop_ent, "Name", NULL, NULL);
            if ((btn->image == NULL) && (icon != NULL))
                btn->image = icon;

            if ( ! btn->customize_action )
            {
                gchar * exec = g_key_file_get_string(desktop, desktop_ent, "Exec", NULL);
                btn->action = translate_exec_to_cmd(exec, icon, title, desktop_file);
                g_free(exec);
            }

            btn->use_terminal = g_key_file_get_boolean(desktop, desktop_ent, "Terminal", NULL);

            if ( ! btn->customize_tooltip)
                btn->tooltip = title;
            if (btn->image != icon)
                g_free(icon);
            if (btn->tooltip != title)
                g_free(title);
        }

        g_free(desktop_file);
        g_key_file_free(desktop);
    }

    /* Create a button with the specified icon. */
    GtkWidget * button = fb_button_new_from_file(btn->image, p->panel->icon_size, p->panel->icon_size, PANEL_ICON_HIGHLIGHT, TRUE);
    btn->widget = button;
    GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
    if (btn->tooltip != NULL)
        gtk_widget_set_tooltip_text(button, btn->tooltip);

    /* Add the button to the icon grid. */
    icon_grid_add(lb->icon_grid, button, TRUE);

    /* Drag and drop support. */
    gtk_drag_dest_set(GTK_WIDGET(button),
        GTK_DEST_DEFAULT_ALL,
        target_table, G_N_ELEMENTS(target_table),
        GDK_ACTION_COPY);

    /* Connect signals. */
    g_signal_connect(button, "button-press-event", G_CALLBACK(launchbutton_press_event), (gpointer) btn);
    g_signal_connect(button, "drag_data_received", G_CALLBACK(launchbutton_drag_data_received_event), (gpointer) btn);

    /* If the list goes from null to non-null, remove the bootstrap button. */
    if ((lb->buttons == NULL) && (lb->bootstrap_button != NULL))
        icon_grid_set_visible(lb->icon_grid, lb->bootstrap_button->widget, FALSE);

    /* Append at end of list to preserve configured order. */
    lb->buttons = g_slist_append(lb->buttons, btn);

    /* Show the widget and return. */
    gtk_widget_show(button);
    plugin_widget_set_background(button, p->panel);
}

/* Read the configuration file entry for a launchbar button and create it. */
static int launchbutton_constructor(Plugin * p, char ** fp)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    /* Allocate the LaunchButton structure. */
    LaunchButton * btn = g_new0(LaunchButton, 1);
    btn->plugin = p;

    /* Read parameters from the configuration file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "launchbar: illegal token %s\n", s.str);
                launchbutton_free(btn);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "id") == 0)
                    btn->desktop_id = g_strdup(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "image") == 0)
                {
                    btn->customize_image = TRUE;
                    btn->image = expand_tilda(g_strdup(s.t[1]));
                }
                else if (g_ascii_strcasecmp(s.t[0], "tooltip") == 0)
                {
                    btn->customize_tooltip = TRUE;
                    btn->tooltip = g_strdup(s.t[1]);
                }
                else if (g_ascii_strcasecmp(s.t[0], "action") == 0)
                {
                    btn->customize_action = TRUE;
                    btn->action = g_strdup(s.t[1]);
                }
                else
                    ERR( "launchbar: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "launchbar: illegal in this context %s\n", s.str);
                launchbutton_free(btn);
                return 0;
            }
        }
    }

    /* Build the structures and return. */
    launchbutton_build_gui(p, btn);
    return 1;
}

/* Plugin constructor. */
static int launchbar_constructor(Plugin * p, char ** fp)
{
    static gchar * launchbar_rc = "style 'launchbar-style'\n"
        "{\n"
        "GtkWidget::focus-line-width = 0\n"
        "GtkWidget::focus-padding = 0\n"
        "GtkButton::default-border = { 0, 0, 0, 0 }\n"
        "GtkButton::default-outside-border = { 0, 0, 0, 0 }\n"
        "}\n"
        "widget '*launchbar*' style 'launchbar-style'";

    gtk_rc_parse_string(launchbar_rc);

    /* Allocate plugin context and set into Plugin private data pointer. */
    LaunchbarPlugin * lb = g_new0(LaunchbarPlugin, 1);
    p->priv = lb;

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);
    gtk_widget_set_name(p->pwid, "launchbar");

    /* Allocate an icon grid manager to manage the container. */
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    lb->icon_grid = icon_grid_new(p->panel, p->pwid, bo, p->panel->icon_size, p->panel->icon_size, 3, 0, p->panel->height);

    /* Read parameters from the configuration file. */
    if (fp != NULL)
    {
        line s;
        s.len = 256;
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "launchbar: illegal token %s\n", s.str);
                return FALSE;
            }
            if (s.type == LINE_BLOCK_START)
            {
                if (g_ascii_strcasecmp(s.t[0], "button") == 0)
                {
                    if ( ! launchbutton_constructor(p, fp))
                    {
                        ERR( "launchbar: can't init button\n");
                        return FALSE;
                    }
                }
                else
                {
                    ERR( "launchbar: unknown var %s\n", s.t[0]);
                    return FALSE;
                }
            }
        else
            {
                ERR( "launchbar: illegal in this context %s\n", s.str);
                return FALSE;
            }
        }
    }

    if (lb->buttons == NULL)
        launchbutton_build_bootstrap(p);
    return TRUE;
}

/* Plugin destructor. */
static void launchbar_destructor(Plugin * p)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    g_slist_foreach(lb->buttons, (GFunc) launchbutton_free, NULL);
    icon_grid_free(lb->icon_grid);
    if (lb->bootstrap_button != NULL)
        {
        g_free(lb->bootstrap_button);
        }
    g_free(lb);
}

/* Handler for "clicked" action on launchbar configuration dialog "Add" button. */
static void launchbar_configure_add_button(GtkButton * widget, Plugin * p)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;
    GtkTreeView * menu_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "menu_view"));
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(menu_view), &list, &it))
    {
        LaunchButton * btn;
        gtk_tree_model_get(list, &it, COL_BTN, &btn, -1);

        /* We have located a selected button.
         * Add a launch button to the launchbar and refresh the view in the configuration dialog. */
        LaunchButton * defined_button = g_new0(LaunchButton, 1);
        defined_button->plugin = p;
        defined_button->desktop_id = g_strdup(btn->desktop_id);
        launchbutton_build_gui(p, defined_button);
        GtkListStore * list = GTK_LIST_STORE(gtk_tree_view_get_model(defined_view));
        GtkTreeIter it;
        gtk_list_store_append(list, &it);
        gtk_list_store_set(list, &it,
            COL_ICON, lxpanel_load_icon(btn->image, PANEL_ICON_SIZE, PANEL_ICON_SIZE, TRUE),
            COL_TITLE, ((btn->tooltip != NULL) ? btn->tooltip : btn->action),
            COL_BTN, defined_button,
            -1);
    }
}

/* Handler for "clicked" action on launchbar configuration dialog "Remove" button. */
static void launchbar_configure_remove_button(GtkButton * widget, Plugin * p)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(defined_view), &list, &it))
    {
        LaunchButton * btn;
        gtk_tree_model_get(list, &it, COL_BTN, &btn, -1);

        /* We have found a selected button.
         * Remove it from the icon grid, the data structure, and the view. */
        gtk_list_store_remove(GTK_LIST_STORE(list), &it);
        icon_grid_remove(lb->icon_grid, btn->widget);
        lb->buttons = g_slist_remove(lb->buttons, btn);
        launchbutton_free(btn);

        /* Put the bootstrap button back if the list becomes empty. */
        if (lb->buttons == NULL)
            launchbutton_build_bootstrap(p);
    }
}

/* Handler for "clicked" action on launchbar configuration dialog "Move Up" button. */
static void launchbar_configure_move_up_button(GtkButton * widget, Plugin * p)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(defined_view), &list, &it))
    {
        LaunchButton *btn;
        gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_BTN, &btn, -1);
        GtkTreePath * path = gtk_tree_model_get_path(GTK_TREE_MODEL(list), &it);
        if ((gtk_tree_path_get_indices(path)[0] > 0)
        && (gtk_tree_path_prev(path)))
        {
            GtkTreeIter it2;
            if (gtk_tree_model_get_iter(list, &it2, path))
            {
                /* We have found a selected button that can be moved.
                 * Reorder it in the icon grid, the data structure, and the view. */
                int i = gtk_tree_path_get_indices(path)[0];
                lb->buttons = g_slist_remove(lb->buttons, btn);
                lb->buttons = g_slist_insert(lb->buttons, btn, i);
                gtk_list_store_move_before(GTK_LIST_STORE(list), &it, &it2);
                icon_grid_reorder_child(lb->icon_grid, btn->widget, i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Handler for "clicked" action on launchbar configuration dialog "Move Down" button. */
static void launchbar_configure_move_down_button(GtkButton * widget, Plugin * p)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(defined_view), &list, &it))
    {
        LaunchButton *btn;
        gtk_tree_model_get(GTK_TREE_MODEL(list), &it, COL_BTN, &btn, -1);
        GtkTreePath * path = gtk_tree_model_get_path(GTK_TREE_MODEL(list), &it);
        int n = gtk_tree_model_iter_n_children(list, NULL);
        if (gtk_tree_path_get_indices(path)[0] < (n - 1))
        {
            gtk_tree_path_next(path);
            GtkTreeIter it2;
            if (gtk_tree_model_get_iter( list, &it2, path))
            {
                /* We have found a selected button that can be moved.
                 * Reorder it in the icon grid, the data structure, and the view. */
                int i = gtk_tree_path_get_indices(path)[0];
                lb->buttons = g_slist_remove(lb->buttons, btn);
                lb->buttons = g_slist_insert(lb->buttons, btn, i + 1);
                gtk_list_store_move_after(GTK_LIST_STORE(list), &it, &it2);
                icon_grid_reorder_child( lb->icon_grid, btn->widget, i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Handler for "response" signal from launchbar configuration dialog. */
static void launchbar_configure_response(GtkDialog * dlg, int response, Plugin * p)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    /* Deallocate LaunchButtons that were loaded from the menu. */
    GtkTreeView * menu_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "menu_view"));
    GtkTreeModel * model = gtk_tree_view_get_model(menu_view);
    GtkTreeIter it;
    if (gtk_tree_model_get_iter_first(model, &it))
    {
        do
        {
            LaunchButton * btn;
            gtk_tree_model_get(model, &it, COL_BTN, &btn, -1);
            launchbutton_free(btn);           
        }
        while (gtk_tree_model_iter_next(model, &it));
    }

    /* Deallocate the configuration dialog. */
    lb->config_dlg = NULL;
    gtk_widget_destroy(GTK_WIDGET(dlg));
}

static void launchbar_configure_add_menu_recursive(GtkListStore * list, MenuCacheDir * menu_dir)
{
    /* Iterate over all menu items in this directory. */
    GSList * l;
    for (l = menu_cache_dir_get_children(menu_dir); l != NULL; l = l->next)
    {
        /* Get the next menu item. */
        MenuCacheItem * item = MENU_CACHE_ITEM(l->data);
        switch (menu_cache_item_get_type(item))
        {
            case MENU_CACHE_TYPE_APP:
                {
                /* If an application, build a LaunchButton data structure so we can identify
                 * the button in the handler.  In this application, the desktop_id is the
                 * fully qualified desktop file path.  The image and tooltip are what is displayed in the view. */
                LaunchButton * btn = g_new0(LaunchButton, 1);
                btn->desktop_id = g_strdup(menu_cache_item_get_file_path(item));
                btn->image = g_strdup(menu_cache_item_get_icon(item));
                btn->tooltip = g_strdup(menu_cache_item_get_name(item));

                /* Add the row to the view. */
                GtkTreeIter it;
                gtk_list_store_append(list, &it);
                gtk_list_store_set(list, &it,
                    COL_ICON, lxpanel_load_icon(btn->image, PANEL_ICON_SIZE, PANEL_ICON_SIZE, TRUE),
                    COL_TITLE, ((btn->tooltip != NULL) ? btn->tooltip : btn->desktop_id),
                    COL_BTN, btn,
                    -1);
                }
                break;

            case MENU_CACHE_TYPE_DIR:
                /* If a directory, recursively add its menu items. */
                launchbar_configure_add_menu_recursive(list, MENU_CACHE_DIR(item));
                break;
        }

    }
}

/* Initialize the list of existing launchbar buttons when the configuration dialog is shown. */
static void launchbar_configure_initialize_list(Plugin * p, GtkWidget * dlg, GtkTreeView * view, gboolean from_menu)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    /* Set the selection mode. */
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(view), GTK_SELECTION_BROWSE);

    /* Establish the column data types. */
    GtkListStore * list = gtk_list_store_new(N_COLS,
        GDK_TYPE_PIXBUF,
        G_TYPE_STRING,
        G_TYPE_POINTER);

    /* Define a column. */
    GtkTreeViewColumn * col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title(col, ((from_menu) ? _("Available Applications") : _("Applications")));

    /* Establish the pixbuf column cell renderer. */
    GtkCellRenderer * render = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
    gtk_tree_view_column_set_attributes(col, render, "pixbuf", COL_ICON, NULL);

    /* Establish the text column cell renderer. */
    render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, render, TRUE);
    gtk_tree_view_column_add_attribute(col, render, "text", COL_TITLE);

    /* Append the column to the view. */
    gtk_tree_view_append_column(view, col);

    if (from_menu)
    {
        /* Initialize from all menu items. */
        MenuCache * menu_cache = panel_menu_cache_new();
        if (menu_cache != NULL)
        {
            MenuCacheDir * dir = menu_cache_get_root_dir(menu_cache);
            launchbar_configure_add_menu_recursive(list, dir);
            menu_cache_unref(menu_cache);
        }
        g_object_set_data(G_OBJECT(dlg), "menu_view", view);
    }
    else
    {
        /* Initialize from defined launchbar buttons. */
        GSList * l;
        for (l = lb->buttons; l != NULL; l = l->next)
        {
            LaunchButton * btn = (LaunchButton *) l->data;
            GtkTreeIter it;
            gtk_list_store_append(list, &it);
            gtk_list_store_set(list, &it,
                COL_ICON, lxpanel_load_icon(btn->image, PANEL_ICON_SIZE, PANEL_ICON_SIZE, TRUE),
                COL_TITLE, ((btn->tooltip != NULL) ? btn->tooltip : btn->action),
                COL_BTN, btn,
                -1);
        }
        g_object_set_data(G_OBJECT(dlg), "defined_view", view);
    }

    /* Finish the setup and return. */
    gtk_tree_view_set_model(view, GTK_TREE_MODEL(list));
}

/* Callback when the configuration dialog is to be shown. */
static void launchbar_configure(Plugin * p, GtkWindow * parent)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    if (lb->config_dlg == NULL)
    {
        /* Create the configuration dialog. */
        GtkWidget * dlg = gtk_dialog_new_with_buttons(
            _(p->class->name),
            parent,
            0,
            GTK_STOCK_CLOSE,
            GTK_RESPONSE_CLOSE,
            NULL);
        gtk_window_set_default_size(GTK_WINDOW(dlg), 640, 400);
        panel_apply_icon(GTK_WINDOW(dlg));

        /* Create a horizontal box. */
        GtkWidget * hbox = gtk_hbox_new(FALSE, 4);
        gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dlg)->vbox), hbox, TRUE, TRUE, 2);

        /* Create a scrollbar as the child of the horizontal box. */
        GtkWidget * defined_scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(defined_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(defined_scroll), GTK_SHADOW_IN);
        gtk_box_pack_start(GTK_BOX(hbox), defined_scroll, TRUE, TRUE, 2);

        /* Create a tree view as the child of the scrollbar. */
        GtkWidget * defined_view = gtk_tree_view_new();
        gtk_container_add(GTK_CONTAINER(defined_scroll), defined_view);

        /* Create a vertical box as the child of the horizontal box. */
        GtkWidget * vbox = gtk_vbox_new(FALSE, 2);
        gtk_box_pack_start(GTK_BOX(hbox), vbox, FALSE, FALSE, 2);

        /* Create a scrollbar as the child of the horizontal box. */
        GtkWidget * menu_scroll = gtk_scrolled_window_new(NULL, NULL);
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(menu_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(menu_scroll), GTK_SHADOW_IN);
        gtk_box_pack_start(GTK_BOX(hbox), menu_scroll, TRUE, TRUE, 2);

        /* Create a tree view as the child of the scrollbar. */
        GtkWidget * menu_view = gtk_tree_view_new();
        gtk_container_add(GTK_CONTAINER(menu_scroll), menu_view);

        /* Create an "Add" button as the child of the vertical box. */
        GtkWidget * btn = gtk_button_new_from_stock(GTK_STOCK_ADD);
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_add_button), p);
        gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 2);

        /* Create a "Remove" button as the child of the vertical box. */
        btn = gtk_button_new_from_stock(GTK_STOCK_REMOVE);
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_remove_button), p);
        gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 2);

        /* Create a "Move Up" button as the child of the vertical box. */
        btn = gtk_button_new_from_stock(GTK_STOCK_GO_UP);
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_up_button), p);
        gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 2);

        /* Create a "Move Down" button as the child of the vertical box. */
        btn = gtk_button_new_from_stock(GTK_STOCK_GO_DOWN);
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_down_button), p);
        gtk_box_pack_start(GTK_BOX(vbox), btn, FALSE, FALSE, 2);

        /* Connect signals. */
        g_signal_connect(dlg, "response", G_CALLBACK(launchbar_configure_response), p);

        /* Initialize the tree view contents. */
        launchbar_configure_initialize_list(p, dlg, GTK_TREE_VIEW(defined_view), FALSE);
        launchbar_configure_initialize_list(p, dlg, GTK_TREE_VIEW(menu_view), TRUE);

        /* Show the dialog. */
        gtk_widget_show_all(dlg);

        /* Establish a callback when the dialog completes. */
        g_object_weak_ref(G_OBJECT(dlg), (GWeakNotify) panel_config_save, p->panel);
        gtk_window_present(GTK_WINDOW(dlg));
        lb->config_dlg = dlg;
    }
}

/* Callback when the configuration is to be saved. */
static void launchbar_save_configuration(Plugin * p, FILE * fp)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;
    GSList * l;
    for (l = lb->buttons; l != NULL; l = l->next)
    {
        LaunchButton * btn = (LaunchButton *) l->data;
        lxpanel_put_line(fp, "Button {");
        if (btn->desktop_id != NULL)
            lxpanel_put_str(fp, "id", btn->desktop_id);
        if (btn->customize_image)
            lxpanel_put_str(fp, "image", btn->image);
        if(btn->customize_tooltip)
            lxpanel_put_str(fp, "tooltip", btn->tooltip);
        if (btn->customize_action)
            lxpanel_put_str(fp, "action", btn->action);
        lxpanel_put_line(fp, "}");
    }
}

/* Callback when panel configuration changes. */
static void launchbar_panel_configuration_changed(Plugin * p)
{
    /* Set orientation into the icon grid. */
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    icon_grid_set_geometry(lb->icon_grid, bo, p->panel->icon_size, p->panel->icon_size, 3, 0, p->panel->height);

    /* Reset all the images to resize them. */
    GSList * l;
    for (l = lb->buttons; l != NULL; l = l->next)
    {
        LaunchButton * btn = (LaunchButton *) l->data;
        fb_button_set_from_file(btn->widget, btn->image, p->panel->icon_size, p->panel->icon_size, TRUE);
    }

    /* Reset the bootstrap button. */
    if (lb->bootstrap_button != NULL)
        gtk_button_set_image(GTK_BUTTON(lb->bootstrap_button->widget),
            gtk_image_new_from_pixbuf(lxpanel_load_icon(GTK_STOCK_ADD, p->panel->icon_size, p->panel->icon_size, FALSE)));
}

/* Plugin descriptor. */
PluginClass launchbar_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "launchbar",
    name : N_("Application Launch Bar"),
    version: "1.0",
    description : N_("Bar with buttons to launch application"),

    constructor : launchbar_constructor,
    destructor  : launchbar_destructor,
    config : launchbar_configure,
    save : launchbar_save_configuration,
    panel_configuration_changed : launchbar_panel_configuration_changed
};
