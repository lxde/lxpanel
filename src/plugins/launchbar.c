/**
 * Copyright (c) 2006-2014 LxDE Developers, see the file AUTHORS for details.
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

#include <libfm/fm-gtk.h>

#include "misc.h"
#include "plugin.h"
#include "icon-grid.h"

#define PANEL_ICON_SIZE 24 /* see the private.h */

/* Column definitions for configuration dialogs. */
enum {
    COL_ICON,
    COL_TITLE,
    COL_ICON_NAME,
    COL_BTN,
    N_COLS
};

static const char DESKTOP_ENTRY[] = "Desktop Entry";

typedef struct LaunchbarPlugin LaunchbarPlugin;

/* Representative of one launch button.
 * Note that the launch parameters come from the specified desktop file, or from the configuration file.
 * This structure is also used during the "add to launchbar" dialog to hold menu items. */
typedef struct {
    LaunchbarPlugin * p;		/* Back pointer to plugin */
    GtkWidget * widget;			/* Pointer to button */
    GtkWidget * image_widget;		/* Pointer to image */
    FmFileInfo * fi;			/* Launcher application descriptor */
    config_setting_t * settings;	/* Pointer to settings */
    FmDndDest * dd;			/* Drag and drop support */
} LaunchButton;

/* Private context for launchbar plugin. */
struct LaunchbarPlugin {
    GtkWidget * plugin;			/* Back pointer to plugin */
    config_setting_t * settings;
    Panel * panel;			/* Back pointer to panel */
    IconGrid * icon_grid;		/* Icon grid managing the container */
    GSList * buttons;			/* Launchbar buttons */
    GtkWidget * config_dlg;		/* Configuration dialog */
    LaunchButton * bootstrap_button;	/* Bootstrapping button for empty launchbar */
    FmIcon * add_icon;			/* Icon for bootstrap_button */
    GtkWidget     *p_button_add, *p_button_remove, *p_label_menu_app_exec, *p_label_def_app_exec;
};

static void launchbar_destructor(gpointer user_data);

/* Deallocate a LaunchButton. */
static void launchbutton_free(LaunchButton * btn)
{
    if (btn->fi)
        fm_file_info_unref(btn->fi);
    if (btn->dd)
        g_object_unref(btn->dd);
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
        if( (p_btn != NULL) && (p_btn->fi != NULL) )
        {
            GString *p_gstring = g_string_new("");
            g_string_printf(p_gstring, "<i>%s</i>", fm_file_info_get_disp_name(p_btn->fi));
            gtk_label_set_markup(GTK_LABEL(lb->p_label_def_app_exec), p_gstring->str);
            g_string_free(p_gstring, TRUE/*free also gstring->str*/);
            label_set = TRUE;
        }
    }
    gtk_widget_set_visible(lb->p_label_def_app_exec, label_set);
    gtk_widget_set_sensitive(lb->p_button_remove, label_set);
}

static void on_menu_view_cursor_changed(GtkTreeView *p_treeview, gpointer p_data)
{
    gboolean  label_set = FALSE;
    LaunchbarPlugin *lb = (LaunchbarPlugin *)p_data;
    GAppInfo *app = fm_app_menu_view_dup_selected_app(p_treeview);

    if (app)
    {
        GString *p_gstring = g_string_new("");
        if (g_app_info_get_description(app))
            g_string_printf(p_gstring, "<i>%s</i>", g_app_info_get_description(app));
        else
            g_string_printf(p_gstring, "<i>%s</i>", g_app_info_get_name(app));
        gtk_label_set_markup(GTK_LABEL(lb->p_label_menu_app_exec), p_gstring->str);
        g_string_free(p_gstring, TRUE/*free also gstring->str*/);
        label_set = TRUE;
    }
    gtk_widget_set_visible(lb->p_label_menu_app_exec, label_set);
    gtk_widget_set_sensitive(lb->p_button_add, label_set);
}

/* Handler for "button-press-event" event from launchbar button. */
static gboolean launchbutton_press_event(GtkWidget * widget, GdkEventButton * event, LaunchButton * b)
{
    /* Standard right-click handling. */
    if (lxpanel_plugin_button_press_event(b->p->plugin, event, b->p->panel))
        return TRUE;

    if (event->button == 1)    /* left button */
    {
        if (b->fi == NULL)	/* The bootstrap button */
            lxpanel_plugin_show_config_dialog(b->p->panel, b->p->plugin);
        else
            lxpanel_launch_path(b->p->panel, fm_file_info_get_path(b->fi));
    }
    return TRUE;
}

/* Handler for "drag-motion" event from launchbar button. */
static gboolean launchbutton_drag_motion_event(
    GtkWidget * widget,
    GdkDragContext * context,
    gint x,
    gint y,
    guint time,
    LaunchButton * b)
{
    GdkAtom target;
    GdkDragAction action = 0;

    fm_dnd_dest_set_dest_file(b->dd, b->fi);
    target = fm_dnd_dest_find_target(b->dd, context);
    if (target != GDK_NONE && fm_dnd_dest_is_target_supported(b->dd, target))
        action = fm_dnd_dest_get_default_action(b->dd, context, target);
    gdk_drag_status(context, action, time);
    /* g_debug("launchbutton_drag_motion_event: act=%u",action); */
    return (action != 0);
}

/* Build the graphic elements for the bootstrap launchbar button. */
static void launchbutton_build_bootstrap(LaunchbarPlugin * lb)
{
    if (lb->bootstrap_button == NULL)
    {
        GdkPixbuf * icon;
        /* Build a button that has the stock "Add" icon.
         * The "desktop-id" being NULL is the marker that this is the bootstrap button. */
        lb->bootstrap_button = g_new0(LaunchButton, 1);
        lb->bootstrap_button->p = lb;

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
        lb->add_icon = fm_icon_from_name(GTK_STOCK_ADD);
        icon = fm_pixbuf_from_icon(lb->add_icon, panel_get_icon_size(lb->panel));
        lb->bootstrap_button->image_widget = gtk_image_new_from_pixbuf(icon);
        g_object_unref(icon);
        gtk_misc_set_padding(GTK_MISC(lb->bootstrap_button->image_widget), 0, 0);
        gtk_misc_set_alignment(GTK_MISC(lb->bootstrap_button->image_widget), 0, 0);
        gtk_container_add(GTK_CONTAINER(event_box), lb->bootstrap_button->image_widget);

        /* Add the bootstrap button to the icon grid.  By policy it is empty at this point. */
        icon_grid_add(lb->icon_grid, event_box, TRUE); 
    }
    else
        icon_grid_set_visible(lb->icon_grid, lb->bootstrap_button->widget, TRUE);
}

/* Build the graphic elements for a launchbar button.  The desktop_id field is already established. */
static LaunchButton *launchbutton_for_file_info(LaunchbarPlugin * lb, FmFileInfo * fi)
{
    LaunchButton *btn;
    GtkWidget *button;

    if (fi == NULL)
    {
        g_warning("launchbar: desktop entry does not exist\n");
        return NULL;
    }

    /* Allocate the LaunchButton structure. */
    btn = g_new0(LaunchButton, 1);
    btn->p = lb;
    btn->fi = fi;

    /* Create a button with the specified icon. */
    button = lxpanel_button_new_for_fm_icon(lb->panel, fm_file_info_get_icon(fi),
                                            NULL, NULL);
    btn->widget = button;
#if GLIB_CHECK_VERSION(2,18,0)
     gtk_widget_set_can_focus(button, FALSE);
#else
     GTK_WIDGET_UNSET_FLAGS(button, GTK_CAN_FOCUS);
#endif

    gtk_widget_set_tooltip_text(button, fm_file_info_get_disp_name(fi));

    /* Add the button to the icon grid. */
    icon_grid_add(lb->icon_grid, button, TRUE);

    /* Drag and drop support. */
    btn->dd = fm_dnd_dest_new_with_handlers(button);

    /* Connect signals. */
    g_signal_connect(button, "button-press-event", G_CALLBACK(launchbutton_press_event), (gpointer) btn);
    g_signal_connect(button, "drag-motion", G_CALLBACK(launchbutton_drag_motion_event), btn);

    /* If the list goes from null to non-null, remove the bootstrap button. */
    if ((lb->buttons == NULL) && (lb->bootstrap_button != NULL))
        icon_grid_set_visible(lb->icon_grid, lb->bootstrap_button->widget, FALSE);

    /* Append at end of list to preserve configured order. */
    lb->buttons = g_slist_append(lb->buttons, btn);

    /* Show the widget and return. */
    gtk_widget_show(button);
    plugin_widget_set_background(button, lb->panel);
    return btn;
}

static LaunchButton *launchbutton_build_gui(LaunchbarPlugin * lb, FmPath * id)
{
    /* Try to get the file data */
    FmFileInfoJob *job = fm_file_info_job_new(NULL, FM_FILE_INFO_JOB_NONE);
    FmFileInfo *fi;

    fm_file_info_job_add(job, id);
    if (!fm_job_run_sync(FM_JOB(job)))
    {
        g_warning("launchbar: problem running file info job\n");
        g_object_unref(job);
        return NULL;
    }
    fi = fm_file_info_list_pop_head(job->file_infos);
    g_object_unref(job);
    return launchbutton_for_file_info(lb, fi);
}

static LaunchButton *launchbutton_search_and_build_gui(LaunchbarPlugin * lb, FmPath * id)
{
    FmDirListJob *job = fm_dir_list_job_new2(id, FM_DIR_LIST_JOB_FAST);
    FmFileInfo *fi;

    if (!fm_job_run_sync(FM_JOB(job)))
    {
        g_warning("launchbar: problem running file search job\n");
        g_object_unref(job);
        return NULL;
    }
    fi = fm_file_info_list_pop_head(job->files);
    g_object_unref(job);
    return launchbutton_for_file_info(lb, fi);
}

/* Read the configuration file entry for a launchbar button and create it. */
static gboolean launchbutton_constructor(LaunchbarPlugin * lb, config_setting_t * s)
{
    LaunchButton *btn;
    const char *str;
    char *str_path = NULL;
    FmPath *path;

    /* Read parameters from the configuration file and validate. */
    if (!config_setting_lookup_string(s, "id", &str) || str[0] == '\0')
        return FALSE;

    /* Build the structures and return. */
    if (str[0] == '/')
    {
        path = fm_path_new_for_str(str);
        btn = launchbutton_build_gui(lb, path);
    }
    else if (str[0] == '~')
    {
        str_path = expand_tilda(str);
        path = fm_path_new_for_str(str_path);
        btn = launchbutton_build_gui(lb, path);
    }
    else
    {
        str_path = g_strdup_printf("search://menu://applications/?recursive=1&show_hidden=1&name=%s", str);
        path = fm_path_new_for_str(str_path);
        btn = launchbutton_search_and_build_gui(lb, path);
    }
    g_free(str_path);
    fm_path_unref(path);
    if (btn)
        btn->settings = s;
    return (btn != NULL);
}

/* prototype of this is app_info_create_from_commandline() in libfm */
static gboolean _launchbutton_create_id(LaunchbarPlugin * lb, config_setting_t * s)
{
    const char *icon = NULL, *name, *exec, *path = NULL;
    char *dirname, *filename;
    int fd, terminal = 0;
    gboolean ret = FALSE;

    if (!config_setting_lookup_string(s, "action", &exec) || exec[0] == '\0')
        return FALSE;
    if (!config_setting_lookup_string(s, "tooltip", &name) || name[0] == '\0')
        name = "Launcher"; /* placeholder, XDG requires a non-empty name */
    config_setting_lookup_string(s, "image", &icon);
    config_setting_lookup_string(s, "path", &path);
    config_setting_lookup_int(s, "terminal", &terminal);

    dirname = g_build_filename(g_get_user_data_dir(), "applications", NULL);
    if (g_mkdir_with_parents(dirname, 0700) == 0)
    {
        filename = g_strdup_printf("%s/lxpanel-launcher-XXXXXX.desktop", dirname);
        fd = g_mkstemp (filename);
        if (fd != -1)
        {
            GString* content = g_string_sized_new(256);

            g_string_printf(content,
                "[" G_KEY_FILE_DESKTOP_GROUP "]\n"
                G_KEY_FILE_DESKTOP_KEY_TYPE "=" G_KEY_FILE_DESKTOP_TYPE_APPLICATION "\n"
                G_KEY_FILE_DESKTOP_KEY_NAME "=%s\n"
                G_KEY_FILE_DESKTOP_KEY_EXEC "=%s\n"
                G_KEY_FILE_DESKTOP_KEY_CATEGORIES "=X-LXPanel;\n",
                name, exec);
            if (icon)
                g_string_append_printf(content, "Icon=%s\n", icon);
            if (terminal)
                g_string_append(content, G_KEY_FILE_DESKTOP_KEY_TERMINAL "=true\n");
            if (path && path[0] == '/')
                g_string_append_printf(content, "Path=%s\n", path);
            close(fd);
            ret = g_file_set_contents(filename, content->str, content->len, NULL);
            if (ret) {
                config_group_set_string(s, "id", filename);
                /* FIXME: is it reasonable to remove obsolete keys too? */
                panel_config_save(lb->panel);
            } else
                g_unlink(filename);
            g_string_free(content, TRUE);
        }
        g_free(filename);
    }
    g_free(dirname);
    if (ret) /* we created it, let use it */
        return launchbutton_constructor(lb, s);
    return FALSE;
}

/* Plugin constructor. */
static GtkWidget *launchbar_constructor(Panel *panel, config_setting_t *settings)
{
    static gchar * launchbar_rc = "style 'launchbar-style'\n"
        "{\n"
        "GtkWidget::focus-line-width = 0\n"
        "GtkWidget::focus-padding = 0\n"
        "GtkButton::default-border = { 0, 0, 0, 0 }\n"
        "GtkButton::default-outside-border = { 0, 0, 0, 0 }\n"
        "}\n"
        "widget '*launchbar*' style 'launchbar-style'";
    GtkWidget *p;
    LaunchbarPlugin * lb;
    config_setting_t *s;

    gtk_rc_parse_string(launchbar_rc);

    /* Allocate plugin context and set into Plugin private data pointer. */
    lb = g_new0(LaunchbarPlugin, 1);

    /* Save construction pointers */
    lb->panel = panel;
    lb->settings = settings;

    /* Allocate top level widget and set into Plugin widget pointer. */
    lb->plugin = p = gtk_event_box_new();
    lxpanel_plugin_set_data(p, lb, launchbar_destructor);
#if GLIB_CHECK_VERSION(2,18,0)
    gtk_widget_set_has_window(p, FALSE);
#else
    GTK_WIDGET_SET_FLAGS(p, GTK_NO_WINDOW);
#endif
    gtk_widget_set_name(p, "launchbar");

    /* Allocate an icon grid manager to manage the container. */
    lb->icon_grid = icon_grid_new(panel, p, panel_get_orientation(panel),
                                  panel_get_icon_size(panel),
                                  panel_get_icon_size(panel), 3, 0, panel_get_height(panel));

    /* Read parameters from the configuration file. */
    settings = config_setting_get_member(settings, "");
    if (settings && config_setting_is_list(settings))
    {
        guint i;

        for (i = 0; (s = config_setting_get_elem(settings, i)) != NULL; )
        {
            if (strcmp(config_setting_get_name(s), "Button") != 0)
            {
                g_warning("launchbar: illegal token %s\n", config_setting_get_name(s));
                config_setting_destroy(s);
            }
            else if (!launchbutton_constructor(lb, s) &&
                     /* try to create desktop id from old-style manual setup */
                     !_launchbutton_create_id(lb, s))
            {
                g_warning( "launchbar: can't init button\n");
                /* FIXME: show failed id to the user instead */
                config_setting_destroy(s);
            }
            else /* success, accept the setting */
                i++;
        }
    }

    if (lb->buttons == NULL)
        launchbutton_build_bootstrap(lb);
    return p;
}

/* Plugin destructor. */
static void launchbar_destructor(gpointer user_data)
{
    LaunchbarPlugin * lb = (LaunchbarPlugin *)user_data;

    /* Free the launchbar. */
    g_slist_foreach(lb->buttons, (GFunc) launchbutton_free, NULL);
    icon_grid_free(lb->icon_grid);

    /* Free the bootstrap button if it exists. */
    if (lb->bootstrap_button != NULL)
        launchbutton_free(lb->bootstrap_button);

    if (lb->add_icon != NULL)
        g_object_unref(lb->add_icon);

    /* Deallocate all memory. */
    g_free(lb);
}

/* Handler for "clicked" action on launchbar configuration dialog "Add" button. */
static void launchbar_configure_add_button(GtkButton * widget, LaunchbarPlugin * lb)
{
    GtkTreeView * menu_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "menu_view"));
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(lb->config_dlg), "defined_view"));
    FmPath * sel_path = fm_app_menu_view_dup_selected_app_desktop_path(menu_view);
    LaunchButton * btn;

    if (sel_path != NULL && (btn = launchbutton_build_gui(lb, sel_path)) != NULL)
    {
        GtkListStore * list = GTK_LIST_STORE(gtk_tree_view_get_model(defined_view));
        GtkTreeIter it;
        GdkPixbuf* pix;
        char *path;
        gtk_list_store_append(list, &it);
        pix = fm_pixbuf_from_icon(fm_file_info_get_icon(btn->fi), PANEL_ICON_SIZE);
        gtk_list_store_set(list, &it,
            COL_ICON, pix,
            COL_TITLE, fm_file_info_get_disp_name(btn->fi),
            COL_BTN, btn,
            -1);
        g_object_unref(pix);
        path = fm_path_to_str(sel_path);
        /* g_debug("*** path '%s'",path); */
        btn->settings = config_group_add_subgroup(lb->settings, "Button");
        config_group_set_string(btn->settings, "id", path);
        g_free(path);
        fm_path_unref(sel_path);
    }
}

/* Handler for "clicked" action on launchbar configuration dialog "Remove" button. */
static void launchbar_configure_remove_button(GtkButton * widget, LaunchbarPlugin * lb)
{
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
        config_setting_destroy(btn->settings);
        launchbutton_free(btn);
        gtk_widget_set_visible(lb->p_label_def_app_exec, FALSE);

        /* Put the bootstrap button back if the list becomes empty. */
        if (lb->buttons == NULL)
            launchbutton_build_bootstrap(lb);
    }
}

/* Handler for "clicked" action on launchbar configuration dialog "Move Up" button. */
static void launchbar_configure_move_up_button(GtkButton * widget, LaunchbarPlugin * lb)
{
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
                config_setting_move_elem(btn->settings,
                                         config_setting_get_member(lb->settings, ""),
                                         i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Handler for "clicked" action on launchbar configuration dialog "Move Down" button. */
static void launchbar_configure_move_down_button(GtkButton * widget, LaunchbarPlugin * lb)
{
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
                config_setting_move_elem(btn->settings,
                                         config_setting_get_member(lb->settings, ""),
                                         i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Initialize the list of existing launchbar buttons when the configuration dialog is shown. */
static void launchbar_configure_initialize_list(LaunchbarPlugin * lb, GtkWidget * dlg, GtkTreeView * view)
{
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

    /* Establish the column data types. */
    GtkListStore* list = GTK_LIST_STORE(gtk_tree_view_get_model(view));

    /* Initialize from defined launchbar buttons. */
    GSList* l;
    for (l = lb->buttons; l != NULL; l = l->next)
    {
        LaunchButton * btn = (LaunchButton *) l->data;
        GdkPixbuf * pix;
        GtkTreeIter it;
        gtk_list_store_append(list, &it);
        pix = fm_pixbuf_from_icon(fm_file_info_get_icon(btn->fi), PANEL_ICON_SIZE);
        gtk_list_store_set(list, &it,
            COL_ICON, pix,
            COL_TITLE, fm_file_info_get_disp_name(btn->fi),
            COL_BTN, btn,
            -1);
        g_object_unref(pix);
    }
    g_object_set_data(G_OBJECT(dlg), "defined_view", view);
}

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *launchbar_configure(Panel *panel, GtkWidget *p, GtkWindow *parent)
{
    LaunchbarPlugin * lb = lxpanel_plugin_get_data(p);

    {
        GtkWidget *dlg, *btn, *defined_view, *menu_view, *menu_view_window;
        GtkBuilder *builder = gtk_builder_new();

        gtk_builder_add_from_file(builder, PACKAGE_UI_DIR "/launchbar.ui", NULL);
        dlg = (GtkWidget*)gtk_builder_get_object(builder, "dlg");
        panel_apply_icon(GTK_WINDOW(dlg));

        defined_view = (GtkWidget*)gtk_builder_get_object(builder, "defined_view");
        menu_view_window = (GtkWidget*)gtk_builder_get_object(builder, "menu_view_window");
        if (menu_view_window == NULL) /* fallback for old glade file */
        {
            menu_view_window = (GtkWidget*)gtk_builder_get_object(builder, "scroll2");
            gtk_widget_destroy(gtk_bin_get_child(GTK_BIN(menu_view_window)));
        }
        menu_view = GTK_WIDGET(fm_app_menu_view_new());
        gtk_container_add(GTK_CONTAINER(menu_view_window), menu_view);
        gtk_widget_show(menu_view);
        lb->p_label_def_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_def_app_exec");
        lb->p_label_menu_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_menu_app_exec");

        /* Connect signals. */
        lb->p_button_add = (GtkWidget*)gtk_builder_get_object(builder, "add");
        g_signal_connect(lb->p_button_add, "clicked", G_CALLBACK(launchbar_configure_add_button), lb);

        lb->p_button_remove = (GtkWidget*)gtk_builder_get_object(builder, "remove");
        g_signal_connect(lb->p_button_remove, "clicked", G_CALLBACK(launchbar_configure_remove_button), lb);

        btn = (GtkWidget*)gtk_builder_get_object(builder, "up");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_up_button), lb);

        btn = (GtkWidget*)gtk_builder_get_object(builder, "down");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_down_button), lb);

        g_signal_connect(defined_view, "button-press-event", G_CALLBACK(on_defined_view_button_press_event), lb);
        g_signal_connect(defined_view, "cursor-changed", G_CALLBACK(on_defined_view_cursor_changed), lb);
        g_signal_connect(menu_view, "cursor-changed", G_CALLBACK(on_menu_view_cursor_changed), lb);

        lb->config_dlg = dlg;

        /* Initialize the tree view contents. */
        launchbar_configure_initialize_list(lb, dlg, GTK_TREE_VIEW(defined_view));
        g_object_set_data(G_OBJECT(dlg), "menu_view", menu_view);

        gtk_widget_set_visible(lb->p_label_menu_app_exec, FALSE);
        gtk_widget_set_visible(lb->p_label_def_app_exec, FALSE);
        gtk_widget_set_sensitive(lb->p_button_add, FALSE);
        gtk_widget_set_sensitive(lb->p_button_remove, FALSE);

        g_object_unref(builder);
    }
    return lb->config_dlg;
}

/* Callback when panel configuration changes. */
static void launchbar_panel_configuration_changed(Panel *panel, GtkWidget *p)
{
    /* Set orientation into the icon grid. */
    LaunchbarPlugin * lb = lxpanel_plugin_get_data(p);
    icon_grid_set_geometry(lb->icon_grid, panel_get_orientation(panel),
                           panel_get_icon_size(panel), panel_get_icon_size(panel),
                           3, 0, panel_get_height(panel));

    /* Reset all the images to resize them. */
    GSList * l;
    for (l = lb->buttons; l != NULL; l = l->next)
    {
        LaunchButton * btn = (LaunchButton *) l->data;
        lxpanel_button_update_icon(btn->widget, fm_file_info_get_icon(btn->fi),
                                   panel_get_icon_size(panel));
    }

    /* Reset the bootstrap button. */
    if (lb->bootstrap_button != NULL)
    {
        GdkPixbuf * icon = fm_pixbuf_from_icon(lb->add_icon, panel_get_icon_size(panel));
        gtk_image_set_from_pixbuf(GTK_IMAGE(lb->bootstrap_button->image_widget), icon);
        g_object_unref(icon);
    }
}

/* Plugin descriptor. */
LXPanelPluginInit lxpanel_static_plugin_launchbar = {
    .name = N_("Application Launch Bar"),
    .description = N_("Bar with buttons to launch application"),

    .new_instance = launchbar_constructor,
    .config = launchbar_configure,
    .reconfigure = launchbar_panel_configuration_changed
};
