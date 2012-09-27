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

/* Modified by Giuseppe Penone <giuspen@gmail.com> starting from 2012-08 and lxpanel 0.5.10 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

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
    COL_ICON_NAME,
    COL_BTN,
    N_COLS
};

static const char DESKTOP_ENTRY[] = "Desktop Entry";

/* Representative of one launch button.
 * Note that the launch parameters come from the specified desktop file, or from the configuration file.
 * This structure is also used during the "add to launchbar" dialog to hold menu items. */
typedef struct {
    Plugin * plugin;			/* Back pointer to plugin */
    GtkWidget * widget;			/* Pointer to button */
    GtkWidget * image_widget;		/* Pointer to image */
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
    GtkWidget     *p_button_add, *p_button_remove, *p_label_menu_app_exec, *p_label_def_app_exec;
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

static gboolean on_defined_view_button_press_event(GtkWidget *p_widget, GdkEventButton *p_event, gpointer p_data)
{
    if(p_event->button == 1)
    {
        if(p_event->type == GDK_2BUTTON_PRESS)
        {
            LaunchbarPlugin *lb = (LaunchbarPlugin *)p_data;
            gtk_button_clicked(GTK_BUTTON(lb->p_button_remove));
        }
    }
    return FALSE;
}

static void on_defined_view_cursor_changed(GtkTreeView *p_treeview, gpointer p_data)
{
    gboolean  label_set = FALSE;
    LaunchbarPlugin *lb = (LaunchbarPlugin *)p_data;
    GtkTreeIter  tree_iter_sel;
    GtkTreeModel* p_treemodel = gtk_tree_view_get_model(p_treeview);
    GtkTreeSelection *p_treeselection = gtk_tree_view_get_selection(p_treeview);
    if(gtk_tree_selection_get_selected(p_treeselection,
                                       (GtkTreeModel **)(&p_treemodel),
                                       &tree_iter_sel))
    {
        LaunchButton * p_btn;
        gtk_tree_model_get(p_treemodel, &tree_iter_sel, COL_BTN, &p_btn, -1);
        if( (p_btn != NULL) && (p_btn->action != NULL) )
        {
            GString *p_gstring = g_string_new("");
            g_string_printf(p_gstring, "<i>Exec =</i>  %s", p_btn->action);
            gtk_label_set_markup(GTK_LABEL(lb->p_label_def_app_exec), p_gstring->str);
            g_string_free(p_gstring, TRUE/*free also gstring->str*/);
            gtk_widget_set_visible(lb->p_label_def_app_exec, TRUE);
            label_set = TRUE;
        }
    }
    if(!label_set)
    {
        gtk_widget_set_visible(lb->p_label_def_app_exec, FALSE);
    }
}

static void on_menu_view_cursor_changed(GtkTreeView *p_treeview, gpointer p_data)
{
    gboolean  label_set = FALSE;
    LaunchbarPlugin *lb = (LaunchbarPlugin *)p_data;
    GtkTreeIter  tree_iter_sel;
    GtkTreeModel* p_treemodel = gtk_tree_view_get_model(p_treeview);
    GtkTreeSelection *p_treeselection = gtk_tree_view_get_selection(p_treeview);
    if(gtk_tree_selection_get_selected(p_treeselection,
                                       (GtkTreeModel **)(&p_treemodel),
                                       &tree_iter_sel))
    {
        LaunchButton * p_btn;
        gtk_tree_model_get(p_treemodel, &tree_iter_sel, COL_BTN, &p_btn, -1);
        if( (p_btn != NULL) && (p_btn->action != NULL) )
        {
            GString *p_gstring = g_string_new("");
            g_string_printf(p_gstring, "<i>Exec =</i>  %s", p_btn->action);
            gtk_label_set_markup(GTK_LABEL(lb->p_label_menu_app_exec), p_gstring->str);
            g_string_free(p_gstring, TRUE/*free also gstring->str*/);
            gtk_widget_set_visible(lb->p_label_menu_app_exec, TRUE);
            label_set = TRUE;
        }
    }
    if(!label_set)
    {
        gtk_widget_set_visible(lb->p_label_menu_app_exec, FALSE);
    }
}

static gboolean on_menu_view_button_press_event(GtkWidget *p_widget, GdkEventButton *p_event, gpointer p_data)
{
    if(p_event->button == 1)
    {
        if(p_event->type == GDK_2BUTTON_PRESS)
        {
            LaunchbarPlugin *lb = (LaunchbarPlugin *)p_data;
            gtk_button_clicked(GTK_BUTTON(lb->p_button_add));
        }
    }
    else if(p_event->button == 2)
    {
        GtkTreePath  *p_tree_path;
        if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(p_widget),
                                         p_event->x, p_event->y,
                                         &p_tree_path,
                                         NULL, NULL, NULL))
        {
            if(gtk_tree_view_row_expanded(GTK_TREE_VIEW(p_widget), p_tree_path))
                gtk_tree_view_collapse_row(GTK_TREE_VIEW(p_widget), p_tree_path);
            else
                gtk_tree_view_expand_row(GTK_TREE_VIEW(p_widget), p_tree_path, FALSE);
            gtk_tree_path_free(p_tree_path);
        }
    }
    return FALSE;
}

/* Handler for "button-press-event" event from launchbar button. */
static gboolean launchbutton_press_event(GtkWidget * widget, GdkEventButton * event, LaunchButton * b)
{
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
#if GTK_CHECK_VERSION(2,14,0)
    if (gtk_selection_data_get_length(sd) > 0)
#else
    if (sd->lengh > 0)
#endif
    {
        if (info == TARGET_URILIST)
        {
#if GTK_CHECK_VERSION(2,14,0)
            gchar * s = (gchar *) gtk_selection_data_get_data(sd);
#else
            gchar * s = (gchar *) sd->data;
#endif
#if GTK_CHECK_VERSION(2,14,0)
            gchar * end = s + gtk_selection_data_get_length(sd);
#else
            gchar * end = s + sd->lenght;
#endif
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

        /* Create an event box. */
        GtkWidget * event_box = gtk_event_box_new();
        gtk_container_set_border_width(GTK_CONTAINER(event_box), 0);
#if GLIB_CHECK_VERSION(2,18,0)
        gtk_widget_set_can_focus            (event_box, FALSE);
#else
        GTK_WIDGET_UNSET_FLAGS(event_box, GTK_CAN_FOCUS);
#endif
        lb->bootstrap_button->widget = event_box;
        g_signal_connect(event_box, "button-press-event", G_CALLBACK(launchbutton_press_event), lb->bootstrap_button);

        /* Create an image containing the stock "Add" icon as a child of the event box. */
        lb->bootstrap_button->image_widget = gtk_image_new_from_pixbuf(
            lxpanel_load_icon(GTK_STOCK_ADD, p->panel->icon_size, p->panel->icon_size, FALSE));
        gtk_misc_set_padding(GTK_MISC(lb->bootstrap_button->image_widget), 0, 0);
        gtk_misc_set_alignment(GTK_MISC(lb->bootstrap_button->image_widget), 0, 0);
        gtk_container_add(GTK_CONTAINER(event_box), lb->bootstrap_button->image_widget);

        /* Add the bootstrap button to the icon grid.  By policy it is empty at this point. */
        icon_grid_add(lb->icon_grid, event_box, TRUE); 
    }
    else
        icon_grid_set_visible(lb->icon_grid, lb->bootstrap_button->widget, TRUE);
}

static gboolean load_app_key_file(gchar *filepath, GKeyFile *p_gkeyfile)
{
    gboolean loaded;
    if (g_path_is_absolute(filepath))
    {
        loaded = g_key_file_load_from_file(p_gkeyfile, filepath, G_KEY_FILE_NONE, NULL );
    }
    else
    {
        /* Load from the freedesktop.org specified data directories. */
        gchar * full_id = g_strconcat("applications/", filepath, NULL);
        loaded = g_key_file_load_from_data_dirs(
            p_gkeyfile, full_id, &filepath, G_KEY_FILE_NONE, NULL);
        g_free(full_id);
    }
    return loaded;
}


/* Build the graphic elements for a launchbar button.  The desktop_id field is already established. */
static void launchbutton_build_gui(Plugin * p, LaunchButton * btn)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    if (btn->desktop_id != NULL)
    {
        /* There is a valid desktop file name.  Try to open it. */
        GKeyFile * desktop = g_key_file_new();
        gboolean loaded = load_app_key_file(btn->desktop_id, desktop);

        if (loaded)
        {
            /* Desktop file located.  Get Icon, Name, Exec, and Terminal parameters. */
            gchar * icon = g_key_file_get_string(desktop, DESKTOP_ENTRY, "Icon", NULL);
            gchar * title = g_key_file_get_locale_string(desktop, DESKTOP_ENTRY, "Name", NULL, NULL);
            if ((btn->image == NULL) && (icon != NULL))
                btn->image = icon;

            if ( ! btn->customize_action )
            {
                gchar * exec = g_key_file_get_string(desktop, DESKTOP_ENTRY, "Exec", NULL);
                btn->action = translate_exec_to_cmd(exec, icon, title, btn->desktop_id);
                g_free(exec);
            }

            btn->use_terminal = g_key_file_get_boolean(desktop, DESKTOP_ENTRY, "Terminal", NULL);

            if ( ! btn->customize_tooltip)
                btn->tooltip = title;
            if (btn->image != icon)
                g_free(icon);
            if (btn->tooltip != title)
                g_free(title);
        }

        g_key_file_free(desktop);
    }

    /* Create a button with the specified icon. */
    GtkWidget * button = fb_button_new_from_file(btn->image, p->panel->icon_size, p->panel->icon_size, PANEL_ICON_HIGHLIGHT, TRUE);
    btn->widget = button;
#if GLIB_CHECK_VERSION(2,18,0)
     gtk_widget_set_can_focus(button, FALSE);
#else
     GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
#endif
    
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
                else if (g_ascii_strcasecmp(s.t[0], "terminal") == 0)
                {
                    btn->use_terminal = str2num(bool_pair, s.t[1], 0);
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
#if GLIB_CHECK_VERSION(2,18,0)
    gtk_widget_set_has_window(p->pwid, FALSE);
#else
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);
#endif
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

    /* Free the launchbar. */
    g_slist_foreach(lb->buttons, (GFunc) launchbutton_free, NULL);
    icon_grid_free(lb->icon_grid);

    /* Free the bootstrap button if it exists. */
    if (lb->bootstrap_button != NULL)
        g_free(lb->bootstrap_button);

    /* Ensure that the configuration dialog is dismissed. */
    if (lb->config_dlg != NULL)
        gtk_widget_destroy(lb->config_dlg);

    /* Deallocate all memory. */
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
        if( btn == NULL )
            return;

        /* We have located a selected button.
         * Add a launch button to the launchbar and refresh the view in the configuration dialog. */
        LaunchButton * defined_button = g_new0(LaunchButton, 1);
        defined_button->plugin = p;
        defined_button->desktop_id = g_strdup(btn->desktop_id);
        launchbutton_build_gui(p, defined_button);
        GtkListStore * list = GTK_LIST_STORE(gtk_tree_view_get_model(defined_view));
        GtkTreeIter it;
        GdkPixbuf* pix;
        gtk_list_store_append(list, &it);
        pix = lxpanel_load_icon(btn->image, PANEL_ICON_SIZE, PANEL_ICON_SIZE, TRUE);
        gtk_list_store_set(list, &it,
            COL_ICON, pix,
            COL_TITLE, ((btn->tooltip != NULL) ? btn->tooltip : btn->action),
            COL_BTN, defined_button,
            -1);
        g_object_unref(pix);
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
        
        gtk_widget_set_visible(lb->p_label_def_app_exec, FALSE);

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

static void launchbar_configure_free_btns_in_model(GtkTreeModel* model, GtkTreeIter *parent_it)
{
    GtkTreeIter it;
    if (gtk_tree_model_iter_children(model, &it, parent_it))
    {
        do
        {
            LaunchButton * btn;
            gtk_tree_model_get(model, &it, COL_BTN, &btn, -1);
            if(G_LIKELY(btn))
                launchbutton_free(btn);
            if( gtk_tree_model_iter_has_child(model, &it) )
                launchbar_configure_free_btns_in_model(model, &it);
        }
        while (gtk_tree_model_iter_next(model, &it));
    }
}

/* Handler for "response" signal from launchbar configuration dialog. */
static void launchbar_configure_response(GtkDialog * dlg, int response, Plugin * p)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    /* Deallocate LaunchButtons that were loaded from the menu. */
    GtkTreeView * menu_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "menu_view"));
    GtkTreeModel * model = gtk_tree_view_get_model(menu_view);
    launchbar_configure_free_btns_in_model(model, NULL);

    /* Deallocate the configuration dialog. */
    lb->config_dlg = NULL;
    gtk_widget_destroy(GTK_WIDGET(dlg));
}

static void launchbar_configure_update_icons(GtkTreeStore* tree, GtkTreeIter* parent_it)
{
    GtkTreeIter it;
    if(gtk_tree_model_iter_children(GTK_TREE_MODEL(tree), &it, parent_it))
    {
        do
        {
            char* name;
            GdkPixbuf* pix;
            gtk_tree_model_get(GTK_TREE_MODEL(tree), &it, COL_ICON, &pix, -1);
            if(!pix)
            {
              gtk_tree_model_get(GTK_TREE_MODEL(tree), &it, COL_ICON_NAME, &name, -1);
              pix = lxpanel_load_icon(name, PANEL_ICON_SIZE, PANEL_ICON_SIZE, TRUE);
              gtk_tree_store_set(tree, &it, COL_ICON, pix, -1);
              g_free(name);
            }
            if(pix)
                g_object_unref(pix);
        }while(gtk_tree_model_iter_next(GTK_TREE_MODEL(tree), &it));
    }
}

static void on_app_tree_row_expanded(GtkTreeView* view, GtkTreeIter* it, GtkTreePath* tp, gpointer user_data)
{
    launchbar_configure_update_icons((GtkTreeStore*)user_data, it);
}

static void launchbar_configure_add_menu_recursive(GtkTreeStore * tree, GtkTreeIter* parent_it, MenuCacheDir * menu_dir)
{
    /* Iterate over all menu items in this directory. */
    GSList * l;
    for (l = menu_cache_dir_get_children(menu_dir); l != NULL; l = l->next)
    {
        /* Get the next menu item. */
        MenuCacheItem * item = MENU_CACHE_ITEM(l->data);
        switch (menu_cache_item_get_type(item))
        {
            case MENU_CACHE_TYPE_NONE:
            case MENU_CACHE_TYPE_SEP:
                break;

            case MENU_CACHE_TYPE_APP:
                {
                /* If an application, build a LaunchButton data structure so we can identify
                 * the button in the handler.  In this application, the desktop_id is the
                 * fully qualified desktop file path.  The image and tooltip are what is displayed in the view. */
                LaunchButton * btn = g_new0(LaunchButton, 1);
                btn->desktop_id = menu_cache_item_get_file_path(item);
                btn->image = g_strdup(menu_cache_item_get_icon(item));
                btn->tooltip = g_strdup(menu_cache_item_get_name(item));

                GKeyFile * desktop = g_key_file_new();
                gboolean loaded = load_app_key_file(btn->desktop_id, desktop);
                btn->action = loaded ? g_key_file_get_string(desktop, DESKTOP_ENTRY, "Exec", NULL) : NULL;

                /* Add the row to the view. */
                GtkTreeIter it;
                gtk_tree_store_append(tree, &it, parent_it);
                gtk_tree_store_set(tree, &it,
                    COL_ICON_NAME, menu_cache_item_get_icon(item),
                    COL_TITLE, menu_cache_item_get_name(item),
                    COL_BTN, btn,
                    -1);
                }
                break;

            case MENU_CACHE_TYPE_DIR:
                {
                GtkTreeIter it;
                gtk_tree_store_append(tree, &it, parent_it);
                gtk_tree_store_set(tree, &it,
                    COL_ICON_NAME, menu_cache_item_get_icon(item),
                    COL_TITLE, menu_cache_item_get_name(item),
                    -1);
                /* If a directory, recursively add its menu items. */
                launchbar_configure_add_menu_recursive(tree, &it, MENU_CACHE_DIR(item));
                }
                break;
        }
    }
    if(!parent_it)
        launchbar_configure_update_icons(tree, parent_it);
}

static void destroy_menu_cache(gpointer* param, GObject* tree)
{
    MenuCache* mc = (MenuCache*)param[0];
    gpointer id = param[1];
    menu_cache_remove_reload_notify(mc, id);
    menu_cache_unref(mc);
    g_slice_free1(sizeof(gpointer) * 2, param);
}

static void on_menu_cache_reload(MenuCache* menu_cache, GtkTreeStore* tree)
{
    MenuCacheDir * dir = menu_cache_get_root_dir(menu_cache);
    gtk_tree_store_clear(tree);
    if(dir)
        launchbar_configure_add_menu_recursive(tree, NULL, dir);
}

/* Initialize the list of existing launchbar buttons when the configuration dialog is shown. */
static void launchbar_configure_initialize_list(Plugin * p, GtkWidget * dlg, GtkTreeView * view, gboolean from_menu)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    /* Set the selection mode. */
    gtk_tree_selection_set_mode(gtk_tree_view_get_selection(view), GTK_SELECTION_BROWSE);

    /* Define a column. */
    GtkTreeViewColumn* col = gtk_tree_view_get_column(view, 0);

    /* Establish the pixbuf column cell renderer. */
    GtkCellRenderer * render = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start(col, render, FALSE);
    gtk_tree_view_column_set_attributes(col, render, "pixbuf", COL_ICON, NULL);

    /* Establish the text column cell renderer. */
    render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start(col, render, TRUE);
    gtk_tree_view_column_add_attribute(col, render, "text", COL_TITLE);

    if (from_menu)
    {
        GtkTreeStore* tree = GTK_TREE_STORE(gtk_tree_view_get_model(view));
        /* Initialize from all menu items. */
        guint32 flags;
        MenuCache *menu_cache = panel_menu_cache_new(&flags);

        g_signal_connect(view, "row-expanded", G_CALLBACK(on_app_tree_row_expanded), tree);

        if (menu_cache != NULL)
        {
            MenuCacheDir * dir = menu_cache_get_root_dir(menu_cache);
            gpointer id = menu_cache_add_reload_notify(menu_cache, (GFunc)on_menu_cache_reload, tree);
            gpointer *param = g_slice_alloc(sizeof(gpointer) * 2);
            if(dir)
                launchbar_configure_add_menu_recursive(tree, NULL, dir);
            param[0] = menu_cache;
            param[1] = id;
            g_object_weak_ref(G_OBJECT(tree), (GWeakNotify)destroy_menu_cache, param);
        }
        g_object_set_data(G_OBJECT(dlg), "menu_view", view);
    }
    else
    {
        /* Establish the column data types. */
        GtkListStore* list = GTK_LIST_STORE(gtk_tree_view_get_model(view));

        /* Initialize from defined launchbar buttons. */
        GSList* l;
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
}

/* Callback when the configuration dialog is to be shown. */
static void launchbar_configure(Plugin * p, GtkWindow * parent)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *) p->priv;

    if (lb->config_dlg == NULL)
    {
        GtkWidget *dlg, *btn, *defined_view, *menu_view;
        GtkBuilder *builder = gtk_builder_new();

        gtk_builder_add_from_file(builder, PACKAGE_UI_DIR "/launchbar.ui", NULL);
        dlg = (GtkWidget*)gtk_builder_get_object(builder, "dlg");
        panel_apply_icon(GTK_WINDOW(dlg));

        defined_view = (GtkWidget*)gtk_builder_get_object(builder, "defined_view");
        menu_view = (GtkWidget*)gtk_builder_get_object(builder, "menu_view");
        lb->p_label_def_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_def_app_exec");
        lb->p_label_menu_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_menu_app_exec");

        /* Connect signals. */
        g_signal_connect(dlg, "response", G_CALLBACK(launchbar_configure_response), p);

        lb->p_button_add = (GtkWidget*)gtk_builder_get_object(builder, "add");
        g_signal_connect(lb->p_button_add, "clicked", G_CALLBACK(launchbar_configure_add_button), p);

        lb->p_button_remove = (GtkWidget*)gtk_builder_get_object(builder, "remove");
        g_signal_connect(lb->p_button_remove, "clicked", G_CALLBACK(launchbar_configure_remove_button), p);

        btn = (GtkWidget*)gtk_builder_get_object(builder, "up");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_up_button), p);

        btn = (GtkWidget*)gtk_builder_get_object(builder, "down");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_down_button), p);

        g_signal_connect(defined_view, "button-press-event", G_CALLBACK(on_defined_view_button_press_event), lb);
        g_signal_connect(defined_view, "cursor-changed", G_CALLBACK(on_defined_view_cursor_changed), lb);
        g_signal_connect(menu_view, "button-press-event", G_CALLBACK(on_menu_view_button_press_event), lb);
        g_signal_connect(menu_view, "cursor-changed", G_CALLBACK(on_menu_view_cursor_changed), lb);

        gtk_window_present(GTK_WINDOW(dlg));
        lb->config_dlg = dlg;

        /* Establish a callback when the dialog completes. */
        g_object_weak_ref(G_OBJECT(dlg), (GWeakNotify) panel_config_save, p->panel);

        /* Initialize the tree view contents. */
        launchbar_configure_initialize_list(p, dlg, GTK_TREE_VIEW(defined_view), FALSE);
        launchbar_configure_initialize_list(p, dlg, GTK_TREE_VIEW(menu_view), TRUE);

        gtk_widget_set_visible(lb->p_label_menu_app_exec, FALSE);
        gtk_widget_set_visible(lb->p_label_def_app_exec, FALSE);

        g_object_unref(builder);
        return;
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
        if (btn->use_terminal)
            lxpanel_put_bool(fp, "terminal", TRUE);
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
        gtk_image_set_from_pixbuf(GTK_IMAGE(lb->bootstrap_button->image_widget),
            lxpanel_load_icon(GTK_STOCK_ADD, p->panel->icon_size, p->panel->icon_size, FALSE));
}

/* Plugin descriptor. */
PluginClass launchbar_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "launchbar",
    name : N_("Application Launch Bar"),
    version: "2.0",
    description : N_("Bar with buttons to launch application"),

    constructor : launchbar_constructor,
    destructor  : launchbar_destructor,
    config : launchbar_configure,
    save : launchbar_save_configuration,
    panel_configuration_changed : launchbar_panel_configuration_changed
};
