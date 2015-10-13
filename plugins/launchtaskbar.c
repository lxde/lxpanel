/*
 * Copyright (C) 2006-2009 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2008 Fred Chien <fred@lxde.org>
 *               2009 Andrew Lee <ajqlee@debian.org>
 *               2009 Jürgen Hötzel <juergen@archlinux.org>
 *               2009 Ying-Chun Liu (PaulLiu) <grandpaul@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2011-2014 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2012 Piotr Sipika <Piotr.Sipika@gmail.com>
 *               2012-2014 Giuseppe Penone <giuspen@gmail.com>
 *               2013 Vincenzo di Cicco <enzodicicco@gmail.com>
 *               2013 Rouslan <rouslan-k@users.sourceforge.net>
 *               2014-2015 Andriy Grytsenko <andrej@rep.kiev.ua>
 *               2014 Vladimír Pýcha <vpycha@gmail.com>
 *               2014 Raimar Bühmann <raimar@buehmann.de>
 *               2014 Andy Balaam <axis3x3@users.sf.net>
 *
 * This file is a part of LXPanel project.
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

/*
 * Started by Giuseppe Penone <giuspen@gmail.com> merging launchbar and taskbar
 * and adding interoperability between them.
*/

/*
 * Taskbar plugin:
 * 2006.09.10 modified by Hong Jen Yee (PCMan) pcman.tw (AT) gmail.com
 * Following features are added:
 * 1. Add XUrgencyHint support. (Flashing task bar buttons, can be disabled)
 * 2. Raise window when files get dragged over taskbar buttons.
 * 3. Add Restore & Maximize menu items to popup menu of task bar buttons.
 */

//#define DEBUG // killall lxpanel && lxpanel --profile Lubuntu &

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
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>

#include <libfm/fm-gtk.h>

#include "misc.h"
#include "ev.h"
#include "plugin.h"
#include "icon.xpm"
#include "icon-grid.h"
#ifndef DISABLE_MENU
# include "menu-policy.h"
#endif
#include "gtk-compat.h"

#define PANEL_ICON_SIZE 24 /* see the private.h */

/* Column definitions for configuration dialogs. */
enum {
    COL_ICON,
    COL_TITLE,
    COL_ICON_NAME,
    COL_BTN,
    N_COLS
};

typedef enum {
    LAUNCHBAR = 0,  /* GtkComboBox is 0-indexed. */
    TASKBAR,
    LAUNCHTASKBAR
} LtbMode;

typedef struct LaunchTaskBarPlugin LaunchTaskBarPlugin;

/* Structure representing a class.  This comes from WM_CLASS, and should identify windows that come from an application. */
typedef struct _task_class {
    struct _task_class *p_taskclass_flink; /* Forward link */
    char * res_class;                      /* Class name */
    struct _task * p_task_head;            /* Head of list of tasks with this class */
    struct _task * p_task_visible;         /* Task that is visible in current desktop, if any */
    char * visible_name;                   /* Name that will be visible for grouped tasks */
    int visible_count;                     /* Count of tasks that are visible in current desktop */
} TaskClass;

/* Structure representing a "task", an open window. */
typedef struct _task {
    struct _task * p_task_flink_xwid;       /* Forward link to next task in X window ID order */
    LaunchTaskBarPlugin * tb;               /* Back pointer to plugin */
    Window win;                             /* X window ID */
    char * name;                            /* Taskbar label when normal, from WM_NAME or NET_WM_NAME */
    char * name_iconified;                  /* Taskbar label when iconified */
    char * exec_bin;                        /* Exec bin associated to Window */
    Atom name_source;                       /* Atom that is the source of taskbar label */
    TaskClass * p_taskclass;                /* Class, from WM_CLASS */
    struct _task * p_task_flink_same_class; /* Forward link to task in same class */
    GtkWidget * button;                     /* Button representing task in taskbar */
    GtkWidget * image;                      /* Icon for task, child of button */
    Atom image_source;                      /* Atom that is the source of taskbar icon */
    GtkWidget * label;                      /* Label for task, child of button */
    GtkWidget * menu_item;                  /* Menu item for grouped task after click */
    gint desktop;                           /* Desktop that contains task, needed to switch to it on Raise */
    gint monitor;                           /* Monitor that the window is on or closest to */
    guint flash_timeout;                    /* Timer for urgency notification */
    unsigned int focused                :1; /* True if window has focus */
    unsigned int iconified              :1; /* True if window is iconified, from WM_STATE */
    unsigned int urgency                :1; /* True if window has an urgency hint, from WM_HINTS */
    unsigned int flash_state            :1; /* One-bit counter to flash taskbar */
    unsigned int entered_state          :1; /* True if cursor is inside taskbar button */
    unsigned int present_in_client_list :1; /* State during WM_CLIENT_LIST processing to detect deletions */
} Task; /* FIXME: convert it into GtkWidget, eliminate button and menu_item */

/* Representative of one launch button.
 * Note that the launch parameters come from the specified desktop file, or from the configuration file.
 * This structure is also used during the "add to launchtaskbar" dialog to hold menu items. */
typedef struct {
    LaunchTaskBarPlugin * p;            /* Back pointer to plugin */
    GtkWidget * widget;                 /* Pointer to button */
    FmFileInfo * fi;                    /* Launcher application descriptor */
    config_setting_t * settings;        /* Pointer to settings */
    FmDndDest * dd;                     /* Drag and drop support */
} LaunchButton; /* FIXME: convert it into GtkWidget, button itself */

/* Private context for taskbar plugin. */
struct LaunchTaskBarPlugin {
    /* LAUNCHBAR */
    GtkWidget *lb_icon_grid;         /* Icon grid managing the container */
    GSList        *buttons;          /* Launchbar buttons */
    LaunchButton  *bootstrap_button; /* Bootstrapping button for empty launchtaskbar */
    GtkWidget     *p_button_add, *p_button_remove, *p_label_menu_app_exec, *p_label_def_app_exec;
    /* TASKBAR */
    Task * p_task_list;            /* List of tasks to be displayed in taskbar */
    TaskClass * p_taskclass_list;  /* Window class list */
    GtkWidget * tb_icon_grid;      /* Manager for taskbar buttons */
    GtkWidget * menu;              /* Popup menu for task control (Close, Raise, etc.) */
    GtkWidget * group_menu;        /* Popup menu for grouping selection */
    GtkWidget * workspace_menu0;   /* "Workspace 1" menu item */
    GdkPixbuf * fallback_pixbuf;   /* Fallback task icon when none is available */
    int number_of_desktops;        /* Number of desktops, from NET_WM_NUMBER_OF_DESKTOPS */
    int current_desktop;           /* Current desktop, from NET_WM_CURRENT_DESKTOP */
    Task * focused;                /* Task that has focus */
    Task * focused_previous;       /* Task that had focus just before panel got it */
    Task * menutask;               /* Task for which popup menu is open */
    guint dnd_delay_timer;         /* Timer for drag and drop delay */
    gboolean dnd_task_moving;      /* User is currently moving a task button */
    int icon_size;                 /* Size of task icons */
    gboolean show_all_desks;       /* User preference: show windows from all desktops */
    gboolean tooltips;             /* User preference: show tooltips */
    gboolean icons_only;           /* User preference: show icons only, omit name */
    gboolean use_mouse_wheel;      /* User preference: scroll wheel does iconify and raise */
    gboolean use_urgency_hint;     /* User preference: windows with urgency will flash */
    gboolean flat_button;          /* User preference: taskbar buttons have visible background */
    gboolean grouped_tasks;        /* User preference: windows from same task are grouped onto a single button */
    gboolean same_monitor_only;    /* User preference: only show windows that are in the same monitor as the taskbar */
    gboolean disable_taskbar_upscale; /* User preference: don't upscale taskbar icons */
    int task_width_max;            /* Maximum width of a taskbar button in horizontal orientation */
    int spacing;                   /* Spacing between taskbar buttons */
    gboolean use_net_active;       /* NET_WM_ACTIVE_WINDOW is supported by the window manager */
    gboolean net_active_checked;   /* True if use_net_active is valid */
    /* COMMON */
#ifndef DISABLE_MENU
    GtkWidget       *p_menuitem_lock_tbp;
    GtkWidget       *p_menuitem_unlock_tbp;
    GtkWidget       *p_menuitem_new_instance;
    GtkWidget       *p_menuitem_separator;
#endif
    GtkWidget * plugin;                 /* Back pointer to Plugin */
    LXPanel * panel;                    /* Back pointer to panel */
    config_setting_t * settings;
    GdkScreen       *screen;
    GtkWidget       *config_dlg;        /* Configuration dialog */
    GtkNotebook     *p_notebook;
    GtkWidget       *p_notebook_page_launch;
    GtkWidget       *p_notebook_page_task;
    GKeyFile        *p_key_file_special_cases;
    int              mode;
    gboolean         lb_built;
    gboolean         tb_built;
    gboolean         fixed_mode;        /* if mode cannot be changed */
};

static gchar *launchtaskbar_rc = "style 'launchtaskbar-style' = 'theme-panel'\n"
        "{\n"
        "GtkWidget::focus-line-width=0\n"
        "GtkWidget::focus-padding=0\n"
        "GtkButton::default-border={0,0,0,0}\n"
        "GtkButton::default-outside-border={0,0,0,0}\n"
        "GtkButton::inner-border={0,0,0,0}\n"
        "}\n"
        "widget '*launchbar.*' style 'launchtaskbar-style'\n"
        "widget '*taskbar.*' style 'launchtaskbar-style'";

#define DRAG_ACTIVE_DELAY    1000
#define TASK_WIDTH_MAX       200
#define ALL_WORKSPACES       -1
#define ICON_ONLY_EXTRA      6      /* Amount needed to have button lay out symmetrically */
#define ICON_BUTTON_TRIM 4      /* Amount needed to have button remain on panel */

static void launchtaskbar_destructor(gpointer user_data);

static void taskbar_redraw(LaunchTaskBarPlugin * tb);
static void task_delete(LaunchTaskBarPlugin * tb, Task * tk, gboolean unlink, gboolean remove);
static GdkPixbuf * task_update_icon(LaunchTaskBarPlugin * tb, Task * tk, Atom source);
static void flash_window_update(Task * tk);
static void taskbar_button_enter(GtkWidget * widget, Task * tk);
static void taskbar_button_leave(GtkWidget * widget, Task * tk);
static gboolean flash_window_timeout(gpointer tk);
static void task_group_menu_destroy(LaunchTaskBarPlugin * tb);
static gboolean taskbar_popup_activate_event(GtkWidget * widget, GdkEventButton * event, Task * tk);
static void taskbar_update_style(LaunchTaskBarPlugin * tb);
static void taskbar_net_client_list(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void taskbar_net_current_desktop(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void taskbar_net_number_of_desktops(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static void taskbar_net_active_window(GtkWidget * widget, LaunchTaskBarPlugin * tb);
static gboolean task_has_urgency(Task * tk);
static GdkFilterReturn taskbar_event_filter(XEvent * xev, GdkEvent * event, LaunchTaskBarPlugin * tb);
static void taskbar_make_menu(LaunchTaskBarPlugin * tb);
static void taskbar_window_manager_changed(GdkScreen * screen, LaunchTaskBarPlugin * tb);
static void taskbar_apply_configuration(LaunchTaskBarPlugin * ltbp);

static void f_get_exec_cmd_from_pid(GPid pid, gchar *buffer_128, const gchar *proc_file)
{
    buffer_128[0] = '\0';
    FILE *pipe;
    gchar  command[64];
    snprintf(command, 64, "cat /proc/%u/%s", pid, proc_file);
    pipe = popen(command, "r");
    if(pipe == NULL)
        g_warning("ltbp: popen '%s'", command);
    else if(fgets(buffer_128, 128, pipe) == NULL)
        g_warning("ltbp: fgets '%s'", command);
    else
    {
        gchar *p_char = strchr(buffer_128, '\n');
        if(p_char != NULL) *p_char = '\0';
    }
    if(pipe != NULL) pclose(pipe);
}

#ifndef DISABLE_MENU
static FmFileInfo *f_find_menu_launchbutton_recursive(const char *exec_bin)
{
    MenuCache *mc;
    guint32 flags;
    GSList *apps, *l;
    size_t len;
    const char *exec, *short_exec;
    char *str_path;
    FmPath *path;
    FmFileInfoJob *job;
    FmFileInfo *fi = NULL;

    /* FIXME: cache it in Task object */
    mc = panel_menu_cache_new(&flags);
    /* FIXME: if menu plugin wasn't loaded yet we'll get NULL list here */
    apps = menu_cache_list_all_apps(mc);
    short_exec = strrchr(exec_bin, '/');
    if (short_exec != NULL)
        short_exec++;
    else
        short_exec = exec_bin;
    len = strlen(short_exec);
    /* the same executable may be used in numerous applications so wild guess
       estimation check for desktop id equal to short_exec+".desktop" first */
    for (l = apps; l; l = l->next)
    {
        exec = menu_cache_item_get_id(MENU_CACHE_ITEM(l->data));
        /* we don't check flags here because user always can manually
           start any app that isn't visible in the desktop menu */
        if (strncmp(exec, short_exec, len) == 0 && exec[len] == '.')
            break;
    }
    /* if not found then check for non-absolute exec name in application
       since it usually is expanded by application starting functions */
    if (l == NULL) for (l = apps; l; l = l->next)
    {
        exec = menu_cache_app_get_exec(MENU_CACHE_APP(l->data));
        if (exec[0] != '/' && strncmp(exec, short_exec, len) == 0 &&
            (exec[len] == ' ' || exec[len] == 0))
            break;
    }
    /* well, not matched, let try full path, we assume here if application
       starts executable by full path then process cannot have short name */
    if (l == NULL && exec_bin[0] == '/')
    {
        len = strlen(exec_bin);
        for (l = apps; l; l = l->next)
        {
            exec = menu_cache_app_get_exec(MENU_CACHE_APP(l->data));
            if (exec[0] == '/' && strncmp(exec, exec_bin, len) == 0 &&
                (exec[len] == ' ' || exec[len] == 0))
                break;
        }
    }
    if (l)
    {
        str_path = menu_cache_dir_make_path(MENU_CACHE_DIR(l->data));
        path = fm_path_new_relative(fm_path_get_apps_menu(), str_path+13); /* skip /Applications */
        g_free(str_path);
        job = fm_file_info_job_new(NULL, FM_FILE_INFO_JOB_NONE);
        fm_file_info_job_add(job, path);
        fm_path_unref(path);
        if (!fm_job_run_sync(FM_JOB(job)))
            g_warning("launchtaskbar: problem running file info job");
        else
            fi = fm_file_info_list_pop_head(job->file_infos);
        g_object_unref(job);
    }
    g_slist_foreach(apps, (GFunc)menu_cache_item_unref, NULL);
    g_slist_free(apps);
    menu_cache_unref(mc);
    g_debug("f_find_menu_launchbutton_recursive: search '%s' found=%d", exec_bin, (fi != NULL));
    return fi;
}
#endif

/* Deallocate a LaunchButton. */
static void launchbutton_free(LaunchButton * btn)
{
    if (btn->fi)
        fm_file_info_unref(btn->fi);
    if (btn->dd)
        g_object_unref(btn->dd);
    g_free(btn);
}

/* Handler for "button-press-event" event from launchtaskbar button. */
static gboolean launchbutton_press_event(GtkWidget * widget, GdkEventButton * event, LaunchButton * b)
{
    if (event->button == 1 && event->type == GDK_BUTTON_PRESS) /* left button */
    {
        if (b->fi == NULL)  /* The bootstrap button */
            lxpanel_plugin_show_config_dialog(b->p->plugin);
        else
            lxpanel_launch_path(b->p->panel, fm_file_info_get_path(b->fi));
        return TRUE;
    }
    return FALSE;
}

/* Handler for "drag-motion" event from launchtaskbar button. */
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

/* Build the graphic elements for the bootstrap launchtaskbar button. */
static void launchbutton_build_bootstrap(LaunchTaskBarPlugin *lb)
{
    if(lb->bootstrap_button == NULL)
    {
        /* Build a button that has the stock "Add" icon.
         * The "desktop-id" being NULL is the marker that this is the bootstrap button. */
        lb->bootstrap_button = g_new0(LaunchButton, 1);
        lb->bootstrap_button->p = lb;

        /* Create an event box. */
        lb->bootstrap_button->widget = lxpanel_button_new_for_icon(lb->panel,
                                                                   GTK_STOCK_ADD,
                                                                   NULL, NULL);
        g_signal_connect(lb->bootstrap_button->widget, "button-press-event",
                         G_CALLBACK(launchbutton_press_event), lb->bootstrap_button);

        /* Add the bootstrap button to the icon grid.  By policy it is empty at this point. */
        gtk_container_add(GTK_CONTAINER(lb->lb_icon_grid), lb->bootstrap_button->widget);
        //plugin_widget_set_background(lb->bootstrap_button->widget, lb->panel);
    }
    else
        gtk_widget_show(lb->bootstrap_button->widget);
}

#ifndef DISABLE_MENU
static LaunchButton *launchbar_exec_bin_exists(LaunchTaskBarPlugin *lb, FmFileInfo *fi)
{
    LaunchButton *ret_val = NULL;
    FmPath *path;
    GSList* l;

    if (!fi)
        return NULL;
    path = fm_file_info_get_path(fi);
    for(l = lb->buttons; l != NULL; l = l->next)
    {
        LaunchButton *btn = (LaunchButton *)l->data;
        if (btn->fi && fm_path_equal(path, fm_file_info_get_path(btn->fi)))
        {
            ret_val = btn;
            break;
        }
    }
    return ret_val;
}
#endif

static void launchbar_update_after_taskbar_class_added(LaunchTaskBarPlugin *ltbp, Task *tk)
{
    GPid   pid = get_net_wm_pid(tk->win);
    gchar  exec_bin_full[128];
    f_get_exec_cmd_from_pid(pid, exec_bin_full, "cmdline");
    gchar *p_char = strrchr(exec_bin_full, '/');
    if(p_char == NULL) p_char = exec_bin_full;
    else p_char++;
    g_free(tk->exec_bin);
    if(strcmp(p_char, "python") == 0)
    {
        f_get_exec_cmd_from_pid(pid, exec_bin_full, "comm");
    }
    else
    {
        tk->exec_bin = g_key_file_get_string(ltbp->p_key_file_special_cases,
                                             "special_cases", p_char, NULL);
        if (tk->exec_bin != NULL) /* found this key */
            return;
    }
    tk->exec_bin = g_strdup(exec_bin_full);

#ifdef DEBUG
    if(ltbp->mode == LAUNCHTASKBAR)
    {
        FmFileInfo *fi = f_find_menu_launchbutton_recursive(tk->exec_bin);
        LaunchButton *btn = launchbar_exec_bin_exists(ltbp, fi);
        g_print("\nTB '%s' OPEN (pid=%u), in LB: %c\n",
            tk->exec_bin, pid, btn != NULL ? 'Y':'N');
        if (fi)
            fm_file_info_unref(fi);
    }
#endif
}

static void launchbar_update_after_taskbar_class_removed(LaunchTaskBarPlugin *ltbp, Task *tk)
{
#ifdef DEBUG
    if(ltbp->mode == LAUNCHTASKBAR)
    {
        FmFileInfo *fi = f_find_menu_launchbutton_recursive(tk->exec_bin);
        LaunchButton *btn = launchbar_exec_bin_exists(ltbp, fi);
        g_print("\nTB '%s' CLOSE, in LB: %c\n", tk->exec_bin, btn != NULL ? 'Y':'N');
        if (fi)
            fm_file_info_unref(fi);
    }
#endif
}

/* Build the graphic elements for a launchtaskbar button.  The desktop_id field is already established. */
/* NOTE: this func consumes reference on fi */
static LaunchButton *launchbutton_for_file_info(LaunchTaskBarPlugin * lb, FmFileInfo * fi)
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

    gtk_widget_set_tooltip_text(button, fm_file_info_get_disp_name(fi));

    /* Add the button to the icon grid. */
    gtk_container_add(GTK_CONTAINER(lb->lb_icon_grid), button);

    /* Drag and drop support. */
    btn->dd = fm_dnd_dest_new_with_handlers(button);

    /* Connect signals. */
    g_signal_connect(button, "button-press-event", G_CALLBACK(launchbutton_press_event), (gpointer) btn);
    g_signal_connect(button, "drag-motion", G_CALLBACK(launchbutton_drag_motion_event), btn);

    /* If the list goes from null to non-null, remove the bootstrap button. */
    if ((lb->buttons == NULL) && (lb->bootstrap_button != NULL))
        gtk_widget_hide(lb->bootstrap_button->widget);

    /* Append at end of list to preserve configured order. */
    lb->buttons = g_slist_append(lb->buttons, btn);

    /* Show the widget and return. */
    //plugin_widget_set_background(button, lb->panel);
    return btn;
}

static LaunchButton *launchbutton_build_gui(LaunchTaskBarPlugin * lb, FmPath * id)
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

static LaunchButton *launchbutton_search_and_build_gui(LaunchTaskBarPlugin * lb, FmPath * id)
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

/* Read the configuration file entry for a launchtaskbar button and create it. */
static gboolean launchbutton_constructor(LaunchTaskBarPlugin * lb, config_setting_t * s)
{
    LaunchButton *btn = NULL;
    const char *str;
    char *str_path = NULL;
    FmPath *path;

    /* Read parameters from the configuration file and validate. */
    if (!config_setting_lookup_string(s, "id", &str) || str[0] == '\0')
        return FALSE;

    /* Build the structures and return. */
    if (str[0] == '~')
    {
        str_path = expand_tilda(str);
        path = fm_path_new_for_path(str_path);
        btn = launchbutton_build_gui(lb, path);
    }
    else if (strchr(str, '/') != NULL)
    {
        path = fm_path_new_for_str(str);
        /* FIXME: check if str contains invalid path */
        btn = launchbutton_build_gui(lb, path);
    }
    else
    {
        str_path = g_strdup_printf("search://menu://applications/?recursive=1&show_hidden=1&name=%s", str);
        path = fm_path_new_for_uri(str_path);
        btn = launchbutton_search_and_build_gui(lb, path);
    }
    g_free(str_path);
    fm_path_unref(path);
    if (btn)
        btn->settings = s;
    return (btn != NULL);
}

/* prototype of this is app_info_create_from_commandline() in libfm */
static gboolean _launchbutton_create_id(LaunchTaskBarPlugin * lb, config_setting_t * s)
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
                lxpanel_config_save(lb->panel);
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

static void launchtaskbar_constructor_add_default_special_case(LaunchTaskBarPlugin *ltbp, const gchar *tk_exec, const gchar *mb_exec)
{
    g_key_file_set_value(ltbp->p_key_file_special_cases, "special_cases", tk_exec, mb_exec);
}

static void launchtaskbar_constructor_launch(LaunchTaskBarPlugin *ltbp, gboolean build_bootstrap)
{
    config_setting_t *settings;

    if(!ltbp->lb_built)
    {
        ltbp->lb_built = TRUE;
        /* Read parameters from the configuration file. */
        settings = config_setting_get_member(ltbp->settings, "");
        if (settings && config_setting_is_list(settings))
        {
            config_setting_t *s;
            guint i;

            for (i = 0; (s = config_setting_get_elem(settings, i)) != NULL; )
            {
                if (strcmp(config_setting_get_name(s), "Button") != 0)
                {
                    g_warning("launchtaskbar: illegal token %s\n", config_setting_get_name(s));
                    config_setting_destroy(s);
                }
                else if (!launchbutton_constructor(ltbp, s) &&
                         /* try to create desktop id from old-style manual setup */
                         !_launchbutton_create_id(ltbp, s))
                {
                    g_warning( "launchtaskbar: can't init button\n");
                    /* FIXME: show failed id to the user instead */
                    config_setting_destroy(s);
                }
                else /* success, accept the setting */
                    i++;
            }
        }
        if(build_bootstrap)
        {
            if(ltbp->buttons == NULL)
                launchbutton_build_bootstrap(ltbp);
        }
    }
    gtk_widget_set_visible(ltbp->lb_icon_grid, TRUE);
}

static void launchtaskbar_constructor_task(LaunchTaskBarPlugin *ltbp)
{
    if(!ltbp->tb_built)
    {
        config_setting_t *s = ltbp->settings;
        gint tmp_int;

        ltbp->tb_built = TRUE;

        /* Parse configuration now */
        if (config_setting_lookup_int(s, "tooltips", &tmp_int))
            ltbp->tooltips = (tmp_int != 0);
        if (config_setting_lookup_int(s, "IconsOnly", &tmp_int))
            ltbp->icons_only = (tmp_int != 0);
        if (config_setting_lookup_int(s, "ShowAllDesks", &tmp_int))
            ltbp->show_all_desks = (tmp_int != 0);
        if (config_setting_lookup_int(s, "SameMonitorOnly", &tmp_int))
            ltbp->same_monitor_only = (tmp_int != 0);
        if (config_setting_lookup_int(s, "DisableUpscale", &tmp_int))
            ltbp->disable_taskbar_upscale = (tmp_int != 0);
        config_setting_lookup_int(s, "MaxTaskWidth", &ltbp->task_width_max);
        config_setting_lookup_int(s, "spacing", &ltbp->spacing);
        if (config_setting_lookup_int(s, "UseMouseWheel", &tmp_int))
            ltbp->use_mouse_wheel = (tmp_int != 0);
        if (config_setting_lookup_int(s, "UseUrgencyHint", &tmp_int))
            ltbp->use_urgency_hint = (tmp_int != 0);
        if (config_setting_lookup_int(s, "FlatButton", &tmp_int))
            ltbp->flat_button = (tmp_int != 0);
        if (config_setting_lookup_int(s, "GroupedTasks", &tmp_int))
            ltbp->grouped_tasks = (tmp_int != 0);

        /* Make container for task buttons as a child of top level widget. */
        ltbp->tb_icon_grid = panel_icon_grid_new(panel_get_orientation(ltbp->panel),
                                                 ltbp->task_width_max,
                                                 ltbp->icon_size, ltbp->spacing, 0,
                                                 panel_get_height(ltbp->panel));
        panel_icon_grid_set_constrain_width(PANEL_ICON_GRID(ltbp->tb_icon_grid), TRUE);
        gtk_box_pack_start(GTK_BOX(ltbp->plugin), ltbp->tb_icon_grid, TRUE, TRUE, 0);
        taskbar_update_style(ltbp);

        /* Add GDK event filter. */
        gdk_window_add_filter(NULL, (GdkFilterFunc) taskbar_event_filter, ltbp);

        /* Connect signals to receive root window events and initialize root window properties. */
        ltbp->number_of_desktops = get_net_number_of_desktops();
        ltbp->current_desktop = get_net_current_desktop();
        g_signal_connect(G_OBJECT(fbev), "current-desktop", G_CALLBACK(taskbar_net_current_desktop), (gpointer) ltbp);
        g_signal_connect(G_OBJECT(fbev), "active-window", G_CALLBACK(taskbar_net_active_window), (gpointer) ltbp);
        g_signal_connect(G_OBJECT(fbev), "number-of-desktops", G_CALLBACK(taskbar_net_number_of_desktops), (gpointer) ltbp);
        g_signal_connect(G_OBJECT(fbev), "client-list", G_CALLBACK(taskbar_net_client_list), (gpointer) ltbp);

        /* Make right-click menu for task buttons.
         * It is retained for the life of the taskbar and will be shown as needed.
         * Number of desktops and edge is needed for this operation. */
        taskbar_make_menu(ltbp);

        /* Connect a signal to be notified when the window manager changes.  This causes re-evaluation of the "use_net_active" status. */
        g_signal_connect(ltbp->screen, "window-manager-changed", G_CALLBACK(taskbar_window_manager_changed), ltbp);

        /* Fetch the client list and redraw the taskbar.  Then determine what window has focus. */
        taskbar_net_client_list(NULL, ltbp);
        taskbar_net_active_window(NULL, ltbp);
    }
    gtk_widget_set_visible(ltbp->tb_icon_grid, TRUE);
}

/* Plugin constructor. */
static GtkWidget *_launchtaskbar_constructor(LXPanel *panel, config_setting_t *settings,
                                             LtbMode mode)
{
    GtkWidget *p;
    LaunchTaskBarPlugin *ltbp;

    gtk_rc_parse_string(launchtaskbar_rc);

    /* Allocate plugin context and set into Plugin private data pointer. */
    ltbp = g_new0(LaunchTaskBarPlugin, 1);
    ltbp->panel = panel;
    ltbp->settings = settings;
    ltbp->mode = mode;
    ltbp->screen = gtk_widget_get_screen((GtkWidget*)panel);

    /* Initialize to defaults. */
    ltbp->icon_size         = panel_get_icon_size(panel);
    ltbp->tooltips          = TRUE;
    ltbp->icons_only        = FALSE;
    ltbp->show_all_desks    = TRUE;
    ltbp->task_width_max    = TASK_WIDTH_MAX;
    ltbp->spacing           = 1;
    ltbp->use_mouse_wheel   = TRUE;
    ltbp->use_urgency_hint  = TRUE;
    ltbp->grouped_tasks     = FALSE;
    ltbp->fixed_mode        = (mode == LAUNCHBAR) || (mode == TASKBAR);

    /* Special cases key file */
    ltbp->p_key_file_special_cases = g_key_file_new();
    gchar *special_cases_filepath = g_build_filename(g_get_user_config_dir(),
                                                     "lxpanel", "launchtaskbar.cfg", NULL);
    if (!g_key_file_load_from_file(ltbp->p_key_file_special_cases,
                                   special_cases_filepath,
                                   G_KEY_FILE_KEEP_COMMENTS, NULL))
    {
        launchtaskbar_constructor_add_default_special_case(ltbp, "synaptic", "synaptic-pkexec");
        launchtaskbar_constructor_add_default_special_case(ltbp, "soffice.bin", "libreoffice");
        launchtaskbar_constructor_add_default_special_case(ltbp, "x-terminal-emulator", "lxterminal");
        gchar *key_file_data = g_key_file_to_data(ltbp->p_key_file_special_cases, NULL, NULL);
        g_file_set_contents(special_cases_filepath, key_file_data, -1, NULL);
        g_free(key_file_data);
    }
    g_free(special_cases_filepath);

    /* Allocate top level widget and set into Plugin widget pointer. */
    ltbp->plugin = p = panel_box_new(panel, FALSE, 5);
    lxpanel_plugin_set_data(p, ltbp, launchtaskbar_destructor);
    /* Allocate an icon grid manager to manage the container. */
    ltbp->lb_icon_grid = panel_icon_grid_new(panel_get_orientation(panel),
                                             ltbp->icon_size, ltbp->icon_size,
                                             3, 0, panel_get_height(panel));
    gtk_box_pack_start(GTK_BOX(p), ltbp->lb_icon_grid, FALSE, TRUE, 0);

    /* Read parameters from the configuration file. */
    config_setting_lookup_int(settings, "LaunchTaskBarMode", &ltbp->mode);
    switch (ltbp->mode) {
    case LAUNCHBAR:
        launchtaskbar_constructor_launch(ltbp, TRUE/*build_bootstrap*/);
        gtk_widget_set_name(p, "launchbar");
        break;
    default:
        ltbp->mode = LAUNCHTASKBAR; /* reset invalid value */
    case LAUNCHTASKBAR:
        launchtaskbar_constructor_launch(ltbp, TRUE/*build_bootstrap*/);
        gtk_widget_set_name(p, "launchtaskbar");
    case TASKBAR:
        launchtaskbar_constructor_task(ltbp);
        if (ltbp->mode == TASKBAR)
            gtk_widget_set_name(p, "taskbar");
    }

    return p;
}

static GtkWidget *launchtaskbar_constructor(LXPanel *panel, config_setting_t *settings)
{
    return _launchtaskbar_constructor(panel, settings, LAUNCHTASKBAR);
}

static void launchtaskbar_destructor_launch(LaunchTaskBarPlugin *ltbp)
{
    /* Free the launchbar. */
    g_slist_foreach(ltbp->buttons, (GFunc) launchbutton_free, NULL);

    /* Free the bootstrap button if it exists. */
    if(ltbp->bootstrap_button != NULL)
    {
        launchbutton_free(ltbp->bootstrap_button);
        ltbp->bootstrap_button = NULL;
    }
}

static void launchtaskbar_destructor_task(LaunchTaskBarPlugin *ltbp)
{
    /* Remove GDK event filter. */
    gdk_window_remove_filter(NULL, (GdkFilterFunc) taskbar_event_filter, ltbp);

    /* Remove root window signal handlers. */
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_current_desktop, ltbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_active_window, ltbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_number_of_desktops, ltbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_client_list, ltbp);

    /* Remove "window-manager-changed" handler. */
    g_signal_handlers_disconnect_by_func(ltbp->screen, taskbar_window_manager_changed, ltbp);

    /* Deallocate task list - widgets are already destroyed there. */
    while(ltbp->p_task_list != NULL)
        task_delete(ltbp, ltbp->p_task_list, TRUE, FALSE);

    /* Deallocate class list. */
    while(ltbp->p_taskclass_list != NULL)
    {
        TaskClass * tc = ltbp->p_taskclass_list;
        ltbp->p_taskclass_list = tc->p_taskclass_flink;
        g_free(tc->res_class);
        g_free(tc);
    }

    /* Deallocate other memory. */
    gtk_widget_destroy(ltbp->menu);
    task_group_menu_destroy(ltbp);
}

/* Plugin destructor. */
static void launchtaskbar_destructor(gpointer user_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)user_data;

    // TASKBAR
    if(ltbp->tb_built) launchtaskbar_destructor_task(ltbp);

    // LAUNCHBAR
    if(ltbp->lb_built) launchtaskbar_destructor_launch(ltbp);

    // LAUNCHTASKBAR

    /* Deallocate all memory. */
    if (ltbp->p_key_file_special_cases != NULL)
        g_key_file_free(ltbp->p_key_file_special_cases);
    g_free(ltbp);
}

static void _launchbar_configure_add(GtkTreeView *menu_view, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "defined_view"));
    FmPath * sel_path = fm_app_menu_view_dup_selected_app_desktop_path(menu_view);
    LaunchButton * btn;

    if (sel_path != NULL && (btn = launchbutton_build_gui(ltbp, sel_path)) != NULL)
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
        btn->settings = config_group_add_subgroup(ltbp->settings, "Button");
        config_group_set_string(btn->settings, "id", path);
        g_free(path);
        fm_path_unref(sel_path);
    }
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Add" button. */
static void launchbar_configure_add_button(GtkButton * widget, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * menu_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "menu_view"));

    _launchbar_configure_add(menu_view, ltbp);
}

static void  launchbar_remove_button(LaunchTaskBarPlugin *ltbp, LaunchButton *btn)
{
    ltbp->buttons = g_slist_remove(ltbp->buttons, btn);
    gtk_widget_destroy(btn->widget);
    config_setting_destroy(btn->settings);
    launchbutton_free(btn);
    /* Put the bootstrap button back if the list becomes empty. */
    if(ltbp->buttons == NULL)
        launchbutton_build_bootstrap(ltbp);
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Remove" button. */
static void launchbar_configure_remove_button(GtkButton * widget, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(defined_view), &list, &it))
    {
        LaunchButton * btn;
        gtk_tree_model_get(list, &it, COL_BTN, &btn, -1);

        /* We have found a selected button.
         * Remove it from the icon grid, the data structure, and the view. */
        gtk_list_store_remove(GTK_LIST_STORE(list), &it);
        gtk_widget_set_visible(ltbp->p_label_def_app_exec, FALSE);

        launchbar_remove_button(ltbp, btn);
    }
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Move Up" button. */
static void launchbar_configure_move_up_button(GtkButton * widget, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "defined_view"));
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
                ltbp->buttons = g_slist_remove(ltbp->buttons, btn);
                ltbp->buttons = g_slist_insert(ltbp->buttons, btn, i);
                gtk_list_store_move_before(GTK_LIST_STORE(list), &it, &it2);
                panel_icon_grid_reorder_child(PANEL_ICON_GRID(ltbp->lb_icon_grid),
                                              btn->widget, i);
                config_setting_move_elem(btn->settings,
                                         config_setting_get_parent(btn->settings),
                                         i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Move Down" button. */
static void launchbar_configure_move_down_button(GtkButton * widget, LaunchTaskBarPlugin *ltbp)
{
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->config_dlg), "defined_view"));
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
                ltbp->buttons = g_slist_remove(ltbp->buttons, btn);
                ltbp->buttons = g_slist_insert(ltbp->buttons, btn, i + 1);
                gtk_list_store_move_after(GTK_LIST_STORE(list), &it, &it2);
                panel_icon_grid_reorder_child(PANEL_ICON_GRID(ltbp->lb_icon_grid),
                                              btn->widget, i);
                config_setting_move_elem(btn->settings,
                                         config_setting_get_parent(btn->settings),
                                         i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Initialize the list of existing launchtaskbar buttons when the configuration dialog is shown. */
static void launchbar_configure_initialize_list(LaunchTaskBarPlugin *ltbp, GtkWidget * dlg, GtkTreeView * view)
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

    /* Initialize from defined launchtaskbar buttons. */
    GSList* l;
    for (l = ltbp->buttons; l != NULL; l = l->next)
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

static void  plugin_set_expand_status(LaunchTaskBarPlugin *ltbp, gboolean expand_new)
{
    gboolean     old_expand, fill;
    guint        padding;
    GtkPackType  pack_type;
    GtkWidget   *box = gtk_widget_get_parent(ltbp->plugin);
    g_return_if_fail(box);
    gtk_box_query_child_packing(GTK_BOX(box), ltbp->plugin, &old_expand, &fill, &padding, &pack_type);
    gtk_box_set_child_packing(GTK_BOX(box), ltbp->plugin, expand_new, fill, padding, pack_type);
}

static void set_config_visibility(LaunchTaskBarPlugin *ltbp)
{
    switch (ltbp->mode) {
    default:
    case LAUNCHTASKBAR:
        gtk_widget_set_visible(ltbp->p_notebook_page_launch, TRUE);
        gtk_widget_set_visible(ltbp->p_notebook_page_task, TRUE);
        gtk_notebook_set_show_tabs(ltbp->p_notebook, TRUE);
        break;
    case TASKBAR:
        gtk_widget_set_visible(ltbp->p_notebook_page_launch, FALSE);
        gtk_widget_set_visible(ltbp->p_notebook_page_task, TRUE);
        gtk_notebook_set_show_tabs(ltbp->p_notebook, FALSE);
        break;
    case LAUNCHBAR:
        gtk_widget_set_visible(ltbp->p_notebook_page_launch, TRUE);
        gtk_widget_set_visible(ltbp->p_notebook_page_task, FALSE);
        gtk_notebook_set_show_tabs(ltbp->p_notebook, FALSE);
    }
}

static void on_combobox_mode_changed(GtkComboBox *p_combobox, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = p_data;
    int new_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(p_combobox));

    if (new_mode < 0 || new_mode == ltbp->mode) /* no change was made */
        return;

    ltbp->mode = new_mode;

    set_config_visibility(ltbp);

    switch (ltbp->mode) {
    case LAUNCHBAR:
        if (ltbp->tb_icon_grid)
            gtk_widget_set_visible(ltbp->tb_icon_grid, FALSE);
        launchtaskbar_constructor_launch(ltbp, TRUE/*build_bootstrap*/);
        plugin_set_expand_status(ltbp, FALSE);
        gtk_widget_set_name(ltbp->plugin, "launchbar");
        break;
    case TASKBAR:
        gtk_widget_set_visible(ltbp->lb_icon_grid, FALSE);
        launchtaskbar_constructor_task(ltbp);
        plugin_set_expand_status(ltbp, TRUE);
        gtk_widget_set_name(ltbp->plugin, "taskbar");
        break;
    default:
        ltbp->mode = LAUNCHTASKBAR;
    case LAUNCHTASKBAR:
        launchtaskbar_constructor_launch(ltbp, TRUE/*build_bootstrap*/);
        launchtaskbar_constructor_task(ltbp);
        plugin_set_expand_status(ltbp, TRUE);
        gtk_widget_set_name(ltbp->plugin, "launchtaskbar");
        break;
    }

    config_group_set_int(ltbp->settings, "LaunchTaskBarMode", ltbp->mode);
}

static void on_checkbutton_show_tooltips_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->tooltips = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\nltbp->tooltips upd\n");
    config_group_set_int(ltbp->settings, "tooltips", ltbp->tooltips);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_icons_only_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->icons_only = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->icons_only upd\n");
    config_group_set_int(ltbp->settings, "IconsOnly", ltbp->icons_only);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_flat_buttons_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->flat_button = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->flat_button upd\n");
    config_group_set_int(ltbp->settings, "FlatButton", ltbp->flat_button);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_show_all_desks_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->show_all_desks = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->show_all_desks upd\n");
    config_group_set_int(ltbp->settings, "ShowAllDesks", ltbp->show_all_desks);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_same_monitor_only_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->same_monitor_only = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->same_monitor_only upd\n");
    config_group_set_int(ltbp->settings, "SameMonitorOnly", ltbp->same_monitor_only);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_disable_taskbar_upscale_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->disable_taskbar_upscale = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->disable_taskbar_upscale upd\n");
    config_group_set_int(ltbp->settings, "DisableUpscale", ltbp->disable_taskbar_upscale);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_mouse_wheel_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->use_mouse_wheel = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->use_mouse_wheel upd\n");
    config_group_set_int(ltbp->settings, "UseMouseWheel", ltbp->use_mouse_wheel);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_urgency_hint_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->use_urgency_hint = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->use_urgency_hint upd\n");
    config_group_set_int(ltbp->settings, "UseUrgencyHint", ltbp->use_urgency_hint);
    taskbar_apply_configuration(ltbp);
}

static void on_checkbutton_grouped_tasks_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->grouped_tasks = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->grouped_tasks upd\n");
    config_group_set_int(ltbp->settings, "GroupedTasks", ltbp->grouped_tasks);
    taskbar_apply_configuration(ltbp);
}

static void on_spinbutton_max_width_value_changed(GtkSpinButton *p_spinbutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->task_width_max = gtk_spin_button_get_value(p_spinbutton);
    //g_print("\ntb->task_width_max upd\n");
    config_group_set_int(ltbp->settings, "MaxTaskWidth", ltbp->task_width_max);
    taskbar_apply_configuration(ltbp);
}

static void on_spinbutton_spacing_value_changed(GtkSpinButton *p_spinbutton, gpointer p_data)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p_data;
    ltbp->spacing = gtk_spin_button_get_value(p_spinbutton);
    //g_print("\ntb->spacing upd\n");
    config_group_set_int(ltbp->settings, "spacing", ltbp->spacing);
    taskbar_apply_configuration(ltbp);
}

static gboolean on_defined_view_button_press_event(GtkWidget *p_widget, GdkEventButton *p_event, gpointer p_data)
{
    LaunchTaskBarPlugin *lb = (LaunchTaskBarPlugin *)p_data;
    if(p_event->button == 1)
    {
        if(p_event->type == GDK_2BUTTON_PRESS)
        {
            gtk_button_clicked(GTK_BUTTON(lb->p_button_remove));
        }
    }
    return FALSE;
}

static void on_defined_view_cursor_changed(GtkTreeView *p_treeview, gpointer p_data)
{
    gboolean  label_set = FALSE;
    LaunchTaskBarPlugin *lb = (LaunchTaskBarPlugin *)p_data;
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
    LaunchTaskBarPlugin *lb = (LaunchTaskBarPlugin *)p_data;
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

static void on_menu_view_row_activated(GtkTreeView *tree_view, GtkTreePath *path,
                                       GtkTreeViewColumn *column,
                                       LaunchTaskBarPlugin *ltbp)
{
    _launchbar_configure_add(tree_view, ltbp);
}

/* FIXME: add support for global hotkeys for launchers */

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *launchtaskbar_configure(LXPanel *panel, GtkWidget *p)
{
    LaunchTaskBarPlugin *ltbp = lxpanel_plugin_get_data(p);

    {
        GtkWidget *dlg, *btn, *defined_view, *menu_view, *menu_view_window;
        GtkBuilder *builder = gtk_builder_new();
        GObject *object;

        gtk_builder_add_from_file(builder, PACKAGE_UI_DIR "/launchtaskbar.ui", NULL);
        dlg = (GtkWidget *)gtk_builder_get_object(builder, "dlg");
        panel_apply_icon(GTK_WINDOW(dlg));

        defined_view = (GtkWidget *)gtk_builder_get_object(builder, "defined_view");
        menu_view_window = (GtkWidget*)gtk_builder_get_object(builder, "menu_view_window");
        if (menu_view_window == NULL) /* fallback for old glade file */
        {
            menu_view_window = (GtkWidget*)gtk_builder_get_object(builder, "scroll2");
            gtk_widget_destroy(gtk_bin_get_child(GTK_BIN(menu_view_window)));
        }
        menu_view = GTK_WIDGET(fm_app_menu_view_new());
        gtk_container_add(GTK_CONTAINER(menu_view_window), menu_view);
        gtk_widget_show(menu_view);
        ltbp->p_label_def_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_def_app_exec");
        ltbp->p_label_menu_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_menu_app_exec");

        /* Connect signals. */
        ltbp->p_button_add = (GtkWidget *)gtk_builder_get_object(builder, "button_add");
        g_signal_connect(ltbp->p_button_add, "clicked", G_CALLBACK(launchbar_configure_add_button), ltbp);

        ltbp->p_button_remove = (GtkWidget *)gtk_builder_get_object(builder, "button_remove");
        g_signal_connect(ltbp->p_button_remove, "clicked", G_CALLBACK(launchbar_configure_remove_button), ltbp);

        btn = (GtkWidget *)gtk_builder_get_object(builder, "button_up");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_up_button), ltbp);

        btn = (GtkWidget *)gtk_builder_get_object(builder, "button_down");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_down_button), ltbp);

        /* FIXME: add a button 'New' with launcher creation dialog */

        g_signal_connect(defined_view, "button-press-event", G_CALLBACK(on_defined_view_button_press_event), ltbp);
        g_signal_connect(defined_view, "cursor-changed", G_CALLBACK(on_defined_view_cursor_changed), ltbp);
        g_signal_connect(menu_view, "cursor-changed", G_CALLBACK(on_menu_view_cursor_changed), ltbp);
        g_signal_connect(menu_view, "row-activated", G_CALLBACK(on_menu_view_row_activated), ltbp);

        ltbp->p_notebook = GTK_NOTEBOOK(gtk_builder_get_object(builder, "notebook"));
        ltbp->p_notebook_page_launch = gtk_notebook_get_nth_page(ltbp->p_notebook, 0);
        ltbp->p_notebook_page_task = gtk_notebook_get_nth_page(ltbp->p_notebook, 1);
        set_config_visibility(ltbp);
	object = gtk_builder_get_object(builder, "combobox_mode");
        gtk_combo_box_set_active(GTK_COMBO_BOX(object), ltbp->mode);
	g_signal_connect(object, "changed",
			G_CALLBACK(on_combobox_mode_changed), ltbp);

#define SETUP_TOGGLE_BUTTON(button,member) \
        object = gtk_builder_get_object(builder, #button); \
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(object), ltbp->member); \
        g_signal_connect(object, "toggled", G_CALLBACK(on_##button##_toggled), ltbp)

        SETUP_TOGGLE_BUTTON(checkbutton_show_tooltips, tooltips);
        SETUP_TOGGLE_BUTTON(checkbutton_icons_only, icons_only);
        SETUP_TOGGLE_BUTTON(checkbutton_flat_buttons, flat_button);
        SETUP_TOGGLE_BUTTON(checkbutton_show_all_desks, show_all_desks);
        SETUP_TOGGLE_BUTTON(checkbutton_same_monitor_only, same_monitor_only);
        SETUP_TOGGLE_BUTTON(checkbutton_mouse_wheel, use_mouse_wheel);
        SETUP_TOGGLE_BUTTON(checkbutton_urgency_hint, use_urgency_hint);
        SETUP_TOGGLE_BUTTON(checkbutton_grouped_tasks, grouped_tasks);
        //SETUP_TOGGLE_BUTTON(checkbutton_disable_taskbar_upscale, disable_taskbar_upscale);
#undef SETUP_TOGGLE_BUTTON
        /* FIXME: for transitional period, turn into SETUP_TOGGLE_BUTTON later */
        object = gtk_builder_get_object(builder, "checkbutton_disable_taskbar_upscale");
        if (object)
        {
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(object), ltbp->disable_taskbar_upscale); \
            g_signal_connect(object, "toggled", G_CALLBACK(on_checkbutton_disable_taskbar_upscale_toggled), ltbp);
        }

#define SETUP_SPIN_BUTTON(button,member) \
        object = gtk_builder_get_object(builder, #button); \
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(object), ltbp->member); \
        g_signal_connect(object, "value-changed", \
                         G_CALLBACK(on_##button##_value_changed), ltbp)

        SETUP_SPIN_BUTTON(spinbutton_max_width, task_width_max);
        SETUP_SPIN_BUTTON(spinbutton_spacing, spacing);
#undef SETUP_SPIN_BUTTON

        ltbp->config_dlg = dlg;

        /* Initialize the tree view contents. */
        launchbar_configure_initialize_list(ltbp, dlg, GTK_TREE_VIEW(defined_view));
        g_object_set_data(G_OBJECT(dlg), "menu_view", menu_view);

        gtk_widget_set_visible(ltbp->p_label_menu_app_exec, FALSE);
        gtk_widget_set_visible(ltbp->p_label_def_app_exec, FALSE);
        gtk_widget_set_sensitive(ltbp->p_button_add, FALSE);
        gtk_widget_set_sensitive(ltbp->p_button_remove, FALSE);
        if (ltbp->fixed_mode)
        {
            object = gtk_builder_get_object(builder, "hbox_mode");
            if (object)
                gtk_widget_destroy(GTK_WIDGET(object));
            if (ltbp->mode == LAUNCHBAR)
                gtk_window_set_title(GTK_WINDOW(ltbp->config_dlg),
                                     _("Application Launch Bar"));
            else
                gtk_window_set_title(GTK_WINDOW(ltbp->config_dlg),
                                     _("Task Bar (Window List)"));
        }

        g_object_unref(builder);
    }
    return ltbp->config_dlg;
}

/* Callback when panel configuration changes. */
static void launchtaskbar_panel_configuration_changed(LXPanel *panel, GtkWidget *p)
{
    /* Set orientation into the icon grid. */
    LaunchTaskBarPlugin *ltbp = lxpanel_plugin_get_data(p);
    int new_icon_size = panel_get_icon_size(panel);

    if (ltbp->lb_built)
        panel_icon_grid_set_geometry(PANEL_ICON_GRID(ltbp->lb_icon_grid),
                                     panel_get_orientation(panel),
                                     new_icon_size, new_icon_size, 3, 0,
                                     panel_get_height(panel));

    /* If the icon size changed, refetch all the icons. */
    if (new_icon_size != ltbp->icon_size)
    {
        Task * tk;
        ltbp->icon_size = new_icon_size;
        for (tk = ltbp->p_task_list; tk != NULL; tk = tk->p_task_flink_xwid)
        {
            GdkPixbuf * pixbuf = task_update_icon(ltbp, tk, None);
            if (pixbuf != NULL)
            {
                gtk_image_set_from_pixbuf(GTK_IMAGE(tk->image), pixbuf);
                g_object_unref(pixbuf);
            }
        }
    }

    /* Redraw all the labels.  Icon size or font color may have changed. */
    if (ltbp->tb_built)
    {
        taskbar_update_style(ltbp);
        taskbar_make_menu(ltbp);
        taskbar_redraw(ltbp);
    }
}

/* Set an urgency timer on a task. */
static void set_timer_on_task(Task * tk)
{
    gint interval;
    g_return_if_fail(tk->flash_timeout == 0);
    g_object_get(gtk_widget_get_settings(tk->button), "gtk-cursor-blink-time", &interval, NULL);
    tk->flash_timeout = g_timeout_add(interval / 2, flash_window_timeout, tk);
}

/* Determine if a task is visible considering only its desktop placement. */
static gboolean task_is_visible_on_current_desktop(LaunchTaskBarPlugin * tb, Task * tk)
{
    return ((tk->desktop == ALL_WORKSPACES) || (tk->desktop == tb->current_desktop) || (tb->show_all_desks) || (tk->urgency));
}

/* Recompute the visible task for a class when the class membership changes.
 * Also transfer the urgency state to the visible task if necessary. */
static void recompute_group_visibility_for_class(LaunchTaskBarPlugin * tb, TaskClass * tc)
{
    tc->visible_count = 0;
    tc->p_task_visible = NULL;
    tc->visible_name = NULL;
    Task * flashing_task = NULL;
    gboolean class_has_urgency = FALSE;
    Task * tk;
    for (tk = tc->p_task_head; tk != NULL; tk = tk->p_task_flink_same_class)
    {
        if (task_is_visible_on_current_desktop(tb, tk))
        {
            /* Count visible tasks and make the first visible task the one that is used for display. */
            if (tc->visible_count == 0)
                tc->p_task_visible = tk;
            tc->visible_count += 1;

            /* Compute summary bit for urgency anywhere in the class. */
            if (tk->urgency && !tk->focused)
                class_has_urgency = TRUE;

            /* If there is urgency, record the currently flashing task. */
            if (tk->flash_timeout != 0)
                flashing_task = tk;

            /* Compute the visible name.  If all visible windows have the same title, use that.
             * Otherwise, use the class name.  This follows WNCK.
             * Note that the visible name is not a separate string, but is set to point to one of the others. */
            if (tc->visible_name == NULL)
                tc->visible_name = tk->name;
            else if ((tc->visible_name != tc->res_class)
            && (tc->visible_name != NULL) && (tk->name != NULL)
            && (strcmp(tc->visible_name, tk->name) != 0))
                tc->visible_name = tc->res_class;
        }
    }

    /* Transfer the flash timeout to the visible task. */
    if (class_has_urgency)
    {
        if (flashing_task == NULL)
        {
            /* Set the flashing context and flash the window immediately. */
            tc->p_task_visible->flash_state = TRUE;
            flash_window_update(tc->p_task_visible);
        }
        else if (flashing_task != tc->p_task_visible)
        {
            /* Reset the timer on the new representative.
             * There will be a slight hiccup on the flash cadence. */
            g_source_remove(flashing_task->flash_timeout);
            flashing_task->flash_timeout = 0;
            tc->p_task_visible->flash_state = flashing_task->flash_state;
            flashing_task->flash_state = FALSE;
            if (tc->p_task_visible->menu_item != NULL)
                g_object_unref(tc->p_task_visible->menu_item);
            tc->p_task_visible->menu_item = flashing_task->menu_item;
            flashing_task->menu_item = NULL;
        }
        if (tc->p_task_visible->flash_timeout == 0)
            set_timer_on_task(tc->p_task_visible);
    }
    else
    {
        /* No task has urgency.  Cancel the timer if one is set. */
        if (flashing_task != NULL)
        {
            g_source_remove(flashing_task->flash_timeout);
            flashing_task->flash_state = FALSE;
            flashing_task->flash_timeout = 0;
        }
    }
}

/* Recompute the visible task for all classes when the desktop changes. */
static void recompute_group_visibility_on_current_desktop(LaunchTaskBarPlugin * tb)
{
    TaskClass * tc;
    for (tc = tb->p_taskclass_list; tc != NULL; tc = tc->p_taskclass_flink)
    {
        recompute_group_visibility_for_class(tb, tc);
    }
}

/* Draw the label and tooltip on a taskbar button. */
static void task_draw_label(Task * tk)
{
    TaskClass * tc = tk->p_taskclass;
    gboolean bold_style = (((tk->entered_state) || (tk->flash_state)) && (tk->tb->flat_button));
    char *label;

    if ((tk->tb->grouped_tasks) && (tc != NULL) && (tc->p_task_visible == tk) && (tc->visible_count > 1))
    {
        label = g_strdup_printf("(%d) %s", tc->visible_count, tc->visible_name);
    }
    else
    {
        label = g_strdup(tk->iconified ? tk->name_iconified : tk->name);
    }

    if (tk->tb->tooltips)
        gtk_widget_set_tooltip_text(tk->button, label);

    lxpanel_draw_label_text(tk->tb->panel, tk->label, label, bold_style, 1,
            tk->tb->flat_button);

    g_free(label);
}

/* Determine if a task is visible. */
static gboolean task_is_visible(LaunchTaskBarPlugin * tb, Task * tk)
{
    /* Not visible due to grouping. */
    if ((tb->grouped_tasks) && (tk->p_taskclass != NULL) && (tk->p_taskclass->p_task_visible != tk))
        return FALSE;

    /* Not on same monitor */
    if (tb->same_monitor_only && panel_get_monitor(tb->panel) != tk->monitor
        && panel_get_monitor(tb->panel) >= 0)
        return FALSE;

    /* Desktop placement. */
    return task_is_visible_on_current_desktop(tb, tk);
}

/* Redraw a task button. */
static void task_button_redraw(Task * tk, LaunchTaskBarPlugin * tb)
{
    if (task_is_visible(tb, tk))
    {
        task_draw_label(tk);
        gtk_widget_set_visible(tk->button, TRUE);
    }
    else
        gtk_widget_set_visible(tk->button, FALSE);
}

/* Redraw all tasks in the taskbar. */
static void taskbar_redraw(LaunchTaskBarPlugin * tb)
{
    Task * tk;
    for (tk = tb->p_task_list; tk != NULL; tk = tk->p_task_flink_xwid)
        task_button_redraw(tk, tb);
}

/* Determine if a task should be visible given its NET_WM_STATE. */
static gboolean accept_net_wm_state(NetWMState * nws)
{
    return ( ! (nws->skip_taskbar));
}

/* Determine if a task should be visible given its NET_WM_WINDOW_TYPE. */
static gboolean accept_net_wm_window_type(NetWMWindowType * nwwt)
{
    return ( ! ((nwwt->desktop) || (nwwt->dock) || (nwwt->splash)));
}

/* Free the names associated with a task. */
static void task_free_names(Task * tk)
{
    TaskClass * tc = tk->p_taskclass;

    if (tc != NULL && tk->name != NULL)
        /* Reset the name from class */
        if (tc->visible_name == tk->name)
            tc->visible_name = tc->res_class;
    g_free(tk->name);
    g_free(tk->name_iconified);
    tk->name = tk->name_iconified = NULL;
}

/* Set the names associated with a task.
 * This is expected to be the same as the title the window manager is displaying. */
static void task_set_names(Task * tk, Atom source)
{
    char * name = NULL;

    /* Try _NET_WM_VISIBLE_NAME, which supports UTF-8.
     * If it is set, the window manager is displaying it as the window title. */
    if ((source == None) || (source == a_NET_WM_VISIBLE_NAME))
    {
        name = get_utf8_property(tk->win,  a_NET_WM_VISIBLE_NAME);
        if (name != NULL)
            tk->name_source = a_NET_WM_VISIBLE_NAME;
    }

    /* Try _NET_WM_NAME, which supports UTF-8, but do not overwrite _NET_WM_VISIBLE_NAME. */
    if ((name == NULL)
    && ((source == None) || (source == a_NET_WM_NAME))
    && ((tk->name_source == None) || (tk->name_source == a_NET_WM_NAME) || (tk->name_source == XA_WM_NAME)))
    {
        name = get_utf8_property(tk->win,  a_NET_WM_NAME);
        if (name != NULL)
            tk->name_source = a_NET_WM_NAME;
    }

    /* Try WM_NAME, which supports only ISO-8859-1, but do not overwrite _NET_WM_VISIBLE_NAME or _NET_WM_NAME. */
    if ((name == NULL)
    && ((source == None) || (source == XA_WM_NAME))
    && ((tk->name_source == None) || (tk->name_source == XA_WM_NAME)))
    {
        name = get_textproperty(tk->win,  XA_WM_NAME);
        if (name != NULL)
            tk->name_source = XA_WM_NAME;
    }

    /* Set the name into the task context, and also on the tooltip. */
    if (name != NULL)
    {
        task_free_names(tk);
        tk->name = name;
        tk->name_iconified = g_strdup_printf("[%s]", name);

        /* Redraw the button. */
        task_button_redraw(tk, tk->tb);
    }
}

/* Unlink a task from the class list because its class changed or it was deleted. */
static void task_unlink_class(Task * tk)
{
    TaskClass * tc = tk->p_taskclass;
    if (tc != NULL)
    {
        /* Reset the name from class */
        if (tc->visible_name == tk->name)
            tc->visible_name = tc->res_class;

        /* Action in Launchbar after class removed */
        launchbar_update_after_taskbar_class_removed(tk->tb, tk);

        /* Remove from per-class task list. */
        if (tc->p_task_head == tk)
        {
            /* Removing the head of the list.  This causes a new task to be the visible task, so we redraw. */
            tc->p_task_head = tk->p_task_flink_same_class;
            if (tc->p_task_head != NULL)
                task_button_redraw(tc->p_task_head, tk->tb);
        }
        else
        {
            /* Locate the task and its predecessor in the list and then remove it.  For safety, ensure it is found. */
            Task * tk_pred = NULL;
            Task * tk_cursor;
            for (
              tk_cursor = tc->p_task_head;
              ((tk_cursor != NULL) && (tk_cursor != tk));
              tk_pred = tk_cursor, tk_cursor = tk_cursor->p_task_flink_same_class) ;
            if (tk_cursor == tk)
                tk_pred->p_task_flink_same_class = tk->p_task_flink_same_class;
        }
        tk->p_task_flink_same_class = NULL;
        tk->p_taskclass = NULL;

        /* Recompute group visibility. */
        recompute_group_visibility_for_class(tk->tb, tc);
    }
}

/* Enter class with specified name. */
static TaskClass * taskbar_enter_res_class(LaunchTaskBarPlugin * tb, char * res_class, gboolean * p_name_consumed)
{
    /* Find existing entry or insertion point. */
    *p_name_consumed = FALSE;
    TaskClass * tc_pred = NULL;
    TaskClass * tc;
    for (tc = tb->p_taskclass_list; tc != NULL; tc_pred = tc, tc = tc->p_taskclass_flink)
    {
        int status = strcmp(res_class, tc->res_class);
        if (status == 0)
            return tc;
        if (status < 0)
            break;
    }

    /* Insert new entry. */
    tc = g_new0(TaskClass, 1);
    tc->res_class = res_class;
    *p_name_consumed = TRUE;
    if (tc_pred == NULL)
    {
        tc->p_taskclass_flink = tb->p_taskclass_list;
        tb->p_taskclass_list = tc;
    }
    else
    {
        tc->p_taskclass_flink = tc_pred->p_taskclass_flink;
        tc_pred->p_taskclass_flink = tc;
    }
    return tc;
}

/* Set the class associated with a task. */
static void task_set_class(Task * tk)
{
    /* Read the WM_CLASS property. */
    XClassHint ch;
    ch.res_name = NULL;
    ch.res_class = NULL;
    XGetClassHint(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), tk->win, &ch);

    /* If the res_name was returned, free it.  We make no use of it at this time. */
    if (ch.res_name != NULL)
    {
        XFree(ch.res_name);
    }

    /* If the res_class was returned, process it.
     * This identifies the application that created the window and is the basis for taskbar grouping. */
    if (ch.res_class != NULL)
    {
        /* Convert the class to UTF-8 and enter it in the class table. */
        gchar * res_class = g_locale_to_utf8(ch.res_class, -1, NULL, NULL, NULL);
        if (res_class != NULL)
        {
            gboolean name_consumed;
            TaskClass * tc = taskbar_enter_res_class(tk->tb, res_class, &name_consumed);
            if ( ! name_consumed) g_free(res_class);

            /* If the task changed class, update data structures. */
            TaskClass * old_tc = tk->p_taskclass;
            if (old_tc != tc)
            {
                /* Unlink from previous class, if any. */
                task_unlink_class(tk);

                /* Add to end of per-class task list.  Do this to keep the popup menu in order of creation. */
                if (tc->p_task_head == NULL)
                    tc->p_task_head = tk;
                else
                {
                    Task * tk_pred;
                    for (tk_pred = tc->p_task_head; tk_pred->p_task_flink_same_class != NULL; tk_pred = tk_pred->p_task_flink_same_class) ;
                    tk_pred->p_task_flink_same_class = tk;
                    task_button_redraw(tk, tk->tb);
                }
                tk->p_taskclass = tc;

                /* Recompute group visibility. */
                recompute_group_visibility_for_class(tk->tb, tc);

                /* Action in Launchbar after class added */
                launchbar_update_after_taskbar_class_added(tk->tb, tk);
            }
        }
        XFree(ch.res_class);
    }
}

/* Look up a task in the task list. */
static Task * task_lookup(LaunchTaskBarPlugin * tb, Window win)
{
    Task * tk;
    for (tk = tb->p_task_list; tk != NULL; tk = tk->p_task_flink_xwid)
        {
        if (tk->win == win)
        return tk;
        if (tk->win > win)
            break;
        }
    return NULL;
}

/* Delete a task and optionally unlink it from the task list. */
static void task_delete(LaunchTaskBarPlugin * tb, Task * tk, gboolean unlink, gboolean remove)
{
    /* If we think this task had focus, remove that. */
    if (tb->focused == tk)
        tb->focused = NULL;

    if (tb->menutask == tk)
        tb->menutask = NULL;

    /* If there is an urgency timeout, remove it. */
    if (tk->flash_timeout != 0) {
        g_source_remove(tk->flash_timeout);
        tk->flash_timeout = 0;
    }

    if (tk->menu_item)
    {
        g_object_unref(tk->menu_item);
        tk->menu_item = NULL;
    }

    /* Deallocate structures. */
    if (remove)
    {
        g_signal_handlers_disconnect_by_func(tk->button, taskbar_button_enter, tk);
        g_signal_handlers_disconnect_by_func(tk->button, taskbar_button_leave, tk);
        gtk_widget_destroy(tk->button);
        task_unlink_class(tk);
    }
    task_free_names(tk);
    g_free(tk->exec_bin);

    /* If requested, unlink the task from the task list.
     * If not requested, the caller will do this. */
    if (unlink)
    {
        if (tb->p_task_list == tk)
            tb->p_task_list = tk->p_task_flink_xwid;
        else
        {
            /* Locate the task and its predecessor in the list and then remove it.  For safety, ensure it is found. */
            Task * tk_pred = NULL;
            Task * tk_cursor;
            for (
              tk_cursor = tb->p_task_list;
              ((tk_cursor != NULL) && (tk_cursor != tk));
              tk_pred = tk_cursor, tk_cursor = tk_cursor->p_task_flink_xwid) ;
            if (tk_cursor == tk)
                tk_pred->p_task_flink_xwid = tk->p_task_flink_xwid;
        }
    }

    /* Deallocate the task structure. */
    g_free(tk);
}

/* Get a pixbuf from a pixmap.
 * Originally from libwnck, Copyright (C) 2001 Havoc Pennington. */
static GdkPixbuf * _wnck_gdk_pixbuf_get_from_pixmap(GdkScreen *screen, Pixmap xpixmap, int width, int height)
{
    /* Get the drawable. */
#if GTK_CHECK_VERSION(2, 24, 0)
    GdkDrawable * drawable = gdk_x11_window_lookup_for_display(gdk_display_get_default(), xpixmap);
#else
    GdkDrawable * drawable = gdk_xid_table_lookup(xpixmap);
#endif
    if (drawable != NULL)
        g_object_ref(G_OBJECT(drawable));
    else
        drawable = gdk_pixmap_foreign_new(xpixmap);

    GdkColormap * colormap = NULL;
    GdkPixbuf * retval = NULL;
    if (drawable != NULL)
    {
        /* Get the colormap.
         * If the drawable has no colormap, use no colormap or the system colormap as recommended in the documentation of gdk_drawable_get_colormap. */
        colormap = gdk_drawable_get_colormap(drawable);
        gint depth = gdk_drawable_get_depth(drawable);
        if (colormap != NULL)
            g_object_ref(G_OBJECT(colormap));
        else if (depth == 1)
            colormap = NULL;
        else
        {
            colormap = gdk_screen_get_system_colormap(screen);
            g_object_ref(G_OBJECT(colormap));
        }

        /* Be sure we aren't going to fail due to visual mismatch. */
        if ((colormap != NULL) && (gdk_visual_get_depth(gdk_colormap_get_visual(colormap)) != depth))
        {
            g_object_unref(G_OBJECT(colormap));
            colormap = NULL;
        }

        /* Do the major work. */
        retval = gdk_pixbuf_get_from_drawable(NULL, drawable, colormap, 0, 0, 0, 0, width, height);
    }

    /* Clean up and return. */
    if (colormap != NULL)
        g_object_unref(G_OBJECT(colormap));
    if (drawable != NULL)
        g_object_unref(G_OBJECT(drawable));
    return retval;
}

/* Apply a mask to a pixbuf.
 * Originally from libwnck, Copyright (C) 2001 Havoc Pennington. */
static GdkPixbuf * apply_mask(GdkPixbuf * pixbuf, GdkPixbuf * mask)
{
    /* Initialize. */
    int w = MIN(gdk_pixbuf_get_width(mask), gdk_pixbuf_get_width(pixbuf));
    int h = MIN(gdk_pixbuf_get_height(mask), gdk_pixbuf_get_height(pixbuf));
    GdkPixbuf * with_alpha = gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);
    guchar * dst = gdk_pixbuf_get_pixels(with_alpha);
    guchar * src = gdk_pixbuf_get_pixels(mask);
    int dst_stride = gdk_pixbuf_get_rowstride(with_alpha);
    int src_stride = gdk_pixbuf_get_rowstride(mask);

    /* Loop to do the work. */
    int i;
    for (i = 0; i < h; i += 1)
    {
        int j;
        for (j = 0; j < w; j += 1)
        {
            guchar * s = src + i * src_stride + j * 3;
            guchar * d = dst + i * dst_stride + j * 4;

            /* s[0] == s[1] == s[2], they are 255 if the bit was set, 0 otherwise. */
            d[3] = ((s[0] == 0) ? 0 : 255); /* 0 = transparent, 255 = opaque */
        }
    }

    return with_alpha;
}

/* Get an icon from the window manager for a task, and scale it to a specified size. */
static GdkPixbuf * get_wm_icon(Window task_win, guint required_width,
                               guint required_height, Atom source,
                               Atom * current_source, LaunchTaskBarPlugin * tb)
{
    /* The result. */
    GdkPixbuf * pixmap = NULL;
    Atom possible_source = None;
    int result = -1;
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    GdkScreen *screen = gtk_widget_get_screen(tb->plugin);

    if ((source == None) || (source == a_NET_WM_ICON))
    {
        /* Important Notes:
         * According to freedesktop.org document:
         * http://standards.freedesktop.org/wm-spec/wm-spec-1.4.html#id2552223
         * _NET_WM_ICON contains an array of 32-bit packed CARDINAL ARGB.
         * However, this is incorrect. Actually it's an array of long integers.
         * Toolkits like gtk+ use unsigned long here to store icons.
         * Besides, according to manpage of XGetWindowProperty, when returned format,
         * is 32, the property data will be stored as an array of longs
         * (which in a 64-bit application will be 64-bit values that are
         * padded in the upper 4 bytes).
         */

        /* Get the window property _NET_WM_ICON, if possible. */
        Atom type = None;
        int format;
        gulong nitems;
        gulong bytes_after;
        gulong * data = NULL;
        result = XGetWindowProperty(
            xdisplay,
            task_win,
            a_NET_WM_ICON,
            0, G_MAXLONG,
            False, XA_CARDINAL,
            &type, &format, &nitems, &bytes_after, (void *) &data);

        /* Inspect the result to see if it is usable.  If not, and we got data, free it. */
        if ((type != XA_CARDINAL) || (nitems <= 0))
        {
            if (data != NULL)
                XFree(data);
            result = -1;
        }

        /* If the result is usable, extract the icon from it. */
        if (result == Success)
        {
            /* Get the largest icon available, unless there is one that is the desired size. */
            /* FIXME: should we try to find an icon whose size is closest to
             * required_width and required_height to reduce unnecessary resizing? */
            gulong * pdata = data;
            gulong * pdata_end = data + nitems;
            gulong * max_icon = NULL;
            gulong max_w = 0;
            gulong max_h = 0;
            while ((pdata + 2) < pdata_end)
            {
                /* Extract the width and height. */
                guint w = pdata[0];
                guint h = pdata[1];
                gulong size = w * h;
                pdata += 2;

                /* Bounds check the icon. Also check for invalid width and height,
                   see http://bugs.debian.org/cgi-bin/bugreport.cgi?bug=801319 */
                if (size == 0 || w > 1024 || h > 1024 || pdata + size > pdata_end)
                    break;

                /* Rare special case: the desired size is the same as icon size. */
                if ((required_width == w) && (required_height == h))
                {
                    max_icon = pdata;
                    max_w = w;
                    max_h = h;
                    break;
                }

                /* If the icon is the largest so far, capture it. */
                if ((w > max_w) && (h > max_h))
                {
                    max_icon = pdata;
                    max_w = w;
                    max_h = h;
                }
                pdata += size;
            }

            /* If an icon was extracted, convert it to a pixbuf.
             * Its size is max_w and max_h. */
            if (max_icon != NULL)
            {
                /* Allocate enough space for the pixel data. */
                gulong len = max_w * max_h;
                guchar * pixdata = g_new(guchar, len * 4);

                /* Loop to convert the pixel data. */
                guchar * p = pixdata;
                gulong i;
                for (i = 0; i < len; p += 4, i += 1)
                {
                    guint argb = max_icon[i];
                    guint rgba = (argb << 8) | (argb >> 24);
                    p[0] = rgba >> 24;
                    p[1] = (rgba >> 16) & 0xff;
                    p[2] = (rgba >> 8) & 0xff;
                    p[3] = rgba & 0xff;
                }

                /* Initialize a pixmap with the pixel data. */
                pixmap = gdk_pixbuf_new_from_data(
                    pixdata,
                    GDK_COLORSPACE_RGB,
                    TRUE, 8,    /* has_alpha, bits_per_sample */
                    max_w, max_h, max_w * 4,
                    (GdkPixbufDestroyNotify) g_free,
                    NULL);
                possible_source = a_NET_WM_ICON;
            }
        else
            result = -1;

            /* Free the X property data. */
            XFree(data);
        }
    }

    /* No icon available from _NET_WM_ICON.  Next try WM_HINTS, but do not overwrite _NET_WM_ICON. */
    if ((result != Success) && (*current_source != a_NET_WM_ICON)
    && ((source == None) || (source != a_NET_WM_ICON)))
    {
        XWMHints * hints = XGetWMHints(xdisplay, task_win);
        result = (hints != NULL) ? Success : -1;
        Pixmap xpixmap = None;
        Pixmap xmask = None;

        if (result == Success)
        {
            /* WM_HINTS is available.  Extract the X pixmap and mask. */
            if ((hints->flags & IconPixmapHint))
                xpixmap = hints->icon_pixmap;
            if ((hints->flags & IconMaskHint))
                xmask = hints->icon_mask;
            XFree(hints);
            if (xpixmap != None)
            {
                result = Success;
                possible_source = XA_WM_HINTS;
            }
            else
                result = -1;
        }

        if (result != Success)
        {
            /* No icon available from _NET_WM_ICON or WM_HINTS.  Next try KWM_WIN_ICON. */
            Atom type = None;
            int format;
            gulong nitems;
            gulong bytes_after;
            Pixmap *icons = NULL;
            Atom kwin_win_icon_atom = gdk_x11_get_xatom_by_name("KWM_WIN_ICON");
            result = XGetWindowProperty(
                xdisplay,
                task_win,
                kwin_win_icon_atom,
                0, G_MAXLONG,
                False, kwin_win_icon_atom,
                &type, &format, &nitems, &bytes_after, (void *) &icons);

            /* Inspect the result to see if it is usable.  If not, and we got data, free it. */
            if (type != kwin_win_icon_atom)
            {
                if (icons != NULL)
                    XFree(icons);
                result = -1;
            }

            /* If the result is usable, extract the X pixmap and mask from it. */
            if (result == Success)
            {
                xpixmap = icons[0];
                xmask = icons[1];
                if (xpixmap != None)
                {
                    result = Success;
                    possible_source = kwin_win_icon_atom;
                }
                else
                    result = -1;
            }
        }

        /* If we have an X pixmap, get its geometry.*/
        unsigned int w, h;
        if (result == Success)
        {
            Window unused_win;
            int unused;
            unsigned int unused_2;
            result = XGetGeometry(
                xdisplay, xpixmap,
                &unused_win, &unused, &unused, &w, &h, &unused_2, &unused_2) ? Success : -1;
        }

        /* If we have an X pixmap and its geometry, convert it to a GDK pixmap. */
        if (result == Success)
        {
            pixmap = _wnck_gdk_pixbuf_get_from_pixmap(screen, xpixmap, w, h);
            result = ((pixmap != NULL) ? Success : -1);
        }

        /* If we have success, see if the result needs to be masked.
         * Failures here are implemented as nonfatal. */
        if ((result == Success) && (xmask != None))
        {
            Window unused_win;
            int unused;
            unsigned int unused_2;
            if (XGetGeometry(
                xdisplay, xmask,
                &unused_win, &unused, &unused, &w, &h, &unused_2, &unused_2))
            {
                /* Convert the X mask to a GDK pixmap. */
                GdkPixbuf * mask = _wnck_gdk_pixbuf_get_from_pixmap(screen, xmask, w, h);
                if (mask != NULL)
                {
                    /* Apply the mask. */
                    GdkPixbuf * masked_pixmap = apply_mask(pixmap, mask);
                    g_object_unref(G_OBJECT(pixmap));
                    g_object_unref(G_OBJECT(mask));
                    pixmap = masked_pixmap;
                }
            }
        }
    }

    /* If we got a pixmap, scale it and return it. */
    if (pixmap == NULL)
        return NULL;
    else
    {
        GdkPixbuf * ret;

        *current_source = possible_source;
        if (tb->disable_taskbar_upscale)
        {
            guint w = gdk_pixbuf_get_width (pixmap);
            guint h = gdk_pixbuf_get_height (pixmap);
            if (w <= required_width || h <= required_height)
            {
                return pixmap;
            }
        }
        ret = gdk_pixbuf_scale_simple(pixmap, required_width, required_height,
                                      GDK_INTERP_BILINEAR);
        g_object_unref(pixmap);
        return ret;
    }
}

/* Update the icon of a task. */
static GdkPixbuf * task_update_icon(LaunchTaskBarPlugin * tb, Task * tk, Atom source)
{
    /* Get the icon from the window's hints. */
    GdkPixbuf * pixbuf = get_wm_icon(tk->win, MAX(0, tb->icon_size - ICON_BUTTON_TRIM),
                                     MAX(0, tb->icon_size - ICON_BUTTON_TRIM),
                                     source, &tk->image_source, tb);

    /* If that fails, and we have no other icon yet, return the fallback icon. */
    if ((pixbuf == NULL)
    && ((source == None) || (tk->image_source == None)))
    {
        /* Establish the fallback task icon.  This is used when no other icon is available. */
        if (tb->fallback_pixbuf == NULL)
            tb->fallback_pixbuf = gdk_pixbuf_new_from_xpm_data((const char **) icon_xpm);
        g_object_ref(tb->fallback_pixbuf);
        pixbuf = tb->fallback_pixbuf;
    }

    /* Return what we have.  This may be NULL to indicate that no change should be made to the icon. */
    return pixbuf;
}

/* Timer expiration for urgency notification.  Also used to draw the button in setting and clearing urgency. */
static void flash_window_update(Task * tk)
{
    /* Set state on the button and redraw. */
    if ( ! tk->tb->flat_button)
        gtk_widget_set_state(tk->button, tk->flash_state ? GTK_STATE_SELECTED : GTK_STATE_NORMAL);
    task_draw_label(tk);
    if (tk->menu_item != NULL && gtk_widget_get_mapped(tk->menu_item))
        /* if submenu exists and mapped then set state too */
        gtk_widget_set_state(tk->menu_item, tk->flash_state ? GTK_STATE_SELECTED : GTK_STATE_NORMAL);

    /* Complement the flashing context. */
    tk->flash_state = ! tk->flash_state;
}

static gboolean flash_window_timeout(gpointer tk)
{
    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;
    flash_window_update(tk);
    return TRUE;
}

/* Set urgency notification. */
static void task_set_urgency(Task * tk)
{
    LaunchTaskBarPlugin * tb = tk->tb;
    TaskClass * tc = tk->p_taskclass;
    if ((tb->grouped_tasks) && (tc != NULL) && (tc->visible_count > 1))
        recompute_group_visibility_for_class(tk->tb, tk->p_taskclass);
    else
    {
        /* Set the flashing context and flash the window immediately. */
        tk->flash_state = TRUE;
        flash_window_update(tk);

        /* Set the timer if none is set. */
        if (tk->flash_timeout == 0)
            set_timer_on_task(tk);
    }
}

/* Clear urgency notification. */
static void task_clear_urgency(Task * tk)
{
    LaunchTaskBarPlugin * tb = tk->tb;
    TaskClass * tc = tk->p_taskclass;
    if ((tb->grouped_tasks) && (tc != NULL) && (tc->visible_count > 1))
        recompute_group_visibility_for_class(tk->tb, tk->p_taskclass);
    else
    {
        /* Remove the timer if one is set. */
        if (tk->flash_timeout != 0)
        {
            g_source_remove(tk->flash_timeout);
            tk->flash_timeout = 0;
        }
        if (tk->menu_item)
        {
            g_object_unref(tk->menu_item);
            tk->menu_item = NULL;
        }

        /* Clear the flashing context and unflash the window immediately. */
        tk->flash_state = FALSE;
        flash_window_update(tk);
        tk->flash_state = FALSE;
    }
}

/* Do the proper steps to raise a window.
 * This means removing it from iconified state and bringing it to the front.
 * We also switch the active desktop and viewport if needed. */
static void task_raise_window(Task * tk, guint32 time)
{
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

    /* Change desktop if needed. */
    if ((tk->desktop != ALL_WORKSPACES) && (tk->desktop != tk->tb->current_desktop))
        Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, tk->desktop, 0, 0, 0, 0);

    /* Evaluate use_net_active if not yet done. */
    if ( ! tk->tb->net_active_checked)
    {
        LaunchTaskBarPlugin * tb = tk->tb;
        GdkAtom net_active_atom = gdk_x11_xatom_to_atom(a_NET_ACTIVE_WINDOW);
        tb->use_net_active = gdk_x11_screen_supports_net_wm_hint(tb->screen, net_active_atom);
        tb->net_active_checked = TRUE;
    }

    /* Raise the window.  We can use NET_ACTIVE_WINDOW if the window manager supports it.
     * Otherwise, do it the old way with XMapRaised and XSetInputFocus. */
    if (tk->tb->use_net_active)
        Xclimsg(tk->win, a_NET_ACTIVE_WINDOW, 2, time, 0, 0, 0);
    else
    {
#if GTK_CHECK_VERSION(2, 24, 0)
        GdkWindow * gdkwindow = gdk_x11_window_lookup_for_display(gdk_display_get_default(), tk->win);
#else
        GdkWindow * gdkwindow = gdk_xid_table_lookup(tk->win);
#endif
        if (gdkwindow != NULL)
            gdk_window_show(gdkwindow);
        else
            XMapRaised(xdisplay, tk->win);

    /* There is a race condition between the X server actually executing the XMapRaised and this code executing XSetInputFocus.
     * If the window is not viewable, the XSetInputFocus will fail with BadMatch. */
    XWindowAttributes attr;
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    XGetWindowAttributes(xdisplay, tk->win, &attr);
    if (attr.map_state == IsViewable)
            XSetInputFocus(xdisplay, tk->win, RevertToNone, time);
    }

    /* Change viewport if needed. */
    XWindowAttributes xwa;
    XGetWindowAttributes(xdisplay, tk->win, &xwa);
    Xclimsg(tk->win, a_NET_DESKTOP_VIEWPORT, xwa.x, xwa.y, 0, 0, 0);
}

/* Position-calculation callback for grouped-task and window-management popup menu. */
static void taskbar_popup_set_position(GtkWidget * menu, gint * px, gint * py, gboolean * push_in, gpointer data)
{
    Task * tk = (Task *) data;

    /* Determine the coordinates. */
    lxpanel_plugin_popup_set_position_helper(tk->tb->panel, tk->button, menu,
                                             px, py);
    *push_in = TRUE;
}

/* Handler for "activate" event from "close all windows" menu*/
static void taskbar_close_all_windows (GtkWidget * widget, Task * tk )
{
    Task * tk_cursor;
    for (tk_cursor = tk->p_taskclass->p_task_head; tk_cursor != NULL;
            tk_cursor = tk_cursor->p_task_flink_same_class)
    {
        if (task_is_visible_on_current_desktop(tk->tb, tk_cursor))
        {
            Xclimsgwm(tk_cursor->win, a_WM_PROTOCOLS, a_WM_DELETE_WINDOW);
        }
    }
    task_group_menu_destroy(tk->tb);
}

/* Remove the grouped-task popup menu from the screen. */
static void task_group_menu_destroy(LaunchTaskBarPlugin * tb)
{
    if (tb->group_menu != NULL)
    {
        gtk_widget_destroy(tb->group_menu);
        tb->group_menu = NULL;
    }
}

/* Handler for "button-press-event" event from taskbar button,
 * or "activate" event from grouped-task popup menu item. */
static gboolean taskbar_task_control_event(GtkWidget * widget, GdkEventButton * event, Task * tk, gboolean popup_menu)
{
    LaunchTaskBarPlugin * tb = tk->tb;
    TaskClass * tc = tk->p_taskclass;
    if ((tb->grouped_tasks) && (tc != NULL) && (tc->visible_count > 1) && (GTK_IS_BUTTON(widget)))
    {
        /* This is grouped-task representative, meaning that there is a class
         * with at least two windows. */
        GtkWidget * menu = NULL;
        if( event->button == 1 ) /* Left click */
        {
            menu = gtk_menu_new();
            /* Bring up a popup menu listing all the class members. */
            Task * tk_cursor;
            GtkWidget * flashing_menu = NULL;
            for (tk_cursor = tc->p_task_head; tk_cursor != NULL;
                    tk_cursor = tk_cursor->p_task_flink_same_class)
            {
                if (task_is_visible_on_current_desktop(tb, tk_cursor))
                {
                    /* The menu item has the name, or the iconified name, and
                     * the icon of the application window. */
                    GtkWidget * mi = gtk_image_menu_item_new_with_label(((tk_cursor->iconified) ?
                                tk_cursor->name_iconified : tk_cursor->name));
                    GtkWidget * im = gtk_image_new_from_pixbuf(gtk_image_get_pixbuf(
                                GTK_IMAGE(tk_cursor->image)));
                    gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), im);
                    g_signal_connect(mi, "button-press-event",
                            G_CALLBACK(taskbar_popup_activate_event), (gpointer) tk_cursor);
                    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
                    /* set mi as if it's urgent with reference */
                    if (tk_cursor->menu_item != NULL)
                        g_object_unref(tk_cursor->menu_item);
                    tk_cursor->menu_item = NULL;
                    if (tk_cursor->urgency && !tk_cursor->focused && flashing_menu == NULL)
                        flashing_menu = g_object_ref_sink(mi);
                }
            }
            /* since tc->visible_count > 1, tc->p_task_visible cannot be NULL */
            g_assert(tc->p_task_visible != NULL);
            g_assert(tc->p_task_visible->menu_item == NULL);
            tc->p_task_visible->menu_item = flashing_menu;
        }
        else if(event->button == 3) /* Right click */
        {
            menu = gtk_menu_new();
            GtkWidget * mi = gtk_menu_item_new_with_mnemonic (_("_Close all windows"));
            gtk_menu_shell_append ( GTK_MENU_SHELL(menu), mi);
            g_signal_connect( mi, "activate", G_CALLBACK(taskbar_close_all_windows), tk);
        }

        /* Show the menu.  Set context so we can find the menu later to dismiss it.
         * Use a position-calculation callback to get the menu nicely
         * positioned with respect to the button. */
        if (menu) {
            gtk_widget_show_all(menu);
            task_group_menu_destroy(tb);
            tb->group_menu = menu;
            gtk_menu_popup(GTK_MENU(menu), NULL, NULL,
                    (GtkMenuPositionFunc) taskbar_popup_set_position, (gpointer) tk,
                    event->button, event->time);
        }
    }
    else
    {
        /* Not a grouped-task representative, or entered from the grouped-task popup menu. */
        Task * visible_task = (((tk->p_taskclass == NULL) || ( ! tk->tb->grouped_tasks)) ? tk : tk->p_taskclass->p_task_visible);
        task_group_menu_destroy(tb);

        if (event->button == 1)
        {
            Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
            /* Left button.
             * If the task is iconified, raise it.
             * If the task is not iconified and has focus, iconify it.
             * If the task is not iconified and does not have focus, raise it. */
            if (tk->iconified)
                task_raise_window(tk, event->time);
            else if ((tk->focused) || (tk == tb->focused_previous))
                XIconifyWindow(xdisplay, tk->win, DefaultScreen(xdisplay));
            else
                task_raise_window(tk, event->time);
        }
        else if (event->button == 2)
        {
            /* Middle button.  Toggle the shaded state of the window. */
            Xclimsg(tk->win, a_NET_WM_STATE,
                2,      /* a_NET_WM_STATE_TOGGLE */
                a_NET_WM_STATE_SHADED,
                0, 0, 0);
        }
        else if(event->button == 3)
        {
            /* Right button.  Bring up the window state popup menu. */
            tk->tb->menutask = tk;
#ifndef DISABLE_MENU
            LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)tk->tb;
            if(ltbp->mode == LAUNCHTASKBAR)
            {
                FmFileInfo *fi = f_find_menu_launchbutton_recursive(tk->exec_bin);
                LaunchButton *btn = launchbar_exec_bin_exists(ltbp, fi);
                /* FIXME: shouldn't we make file info at task button creation? */
#ifdef DEBUG
                g_print("\nTB '%s' right-click, in LB: %c\n", tk->exec_bin, btn != NULL ? 'Y':'N');
#endif
                if(btn != NULL)
                {
                    gtk_widget_set_visible(ltbp->p_menuitem_lock_tbp, FALSE);
                    gtk_widget_set_visible(ltbp->p_menuitem_unlock_tbp, TRUE);
                    gtk_widget_set_visible(ltbp->p_menuitem_new_instance, TRUE);
                }
                else
                {
                    gtk_widget_set_visible(ltbp->p_menuitem_lock_tbp, fi != NULL);
                    gtk_widget_set_visible(ltbp->p_menuitem_unlock_tbp, FALSE);
                    gtk_widget_set_visible(ltbp->p_menuitem_new_instance, fi != NULL);
                }
                gtk_widget_set_visible(ltbp->p_menuitem_separator, TRUE);
                if (fi)
                    fm_file_info_unref(fi);
            }
            else
            {
                gtk_widget_set_visible(ltbp->p_menuitem_lock_tbp, FALSE);
                gtk_widget_set_visible(ltbp->p_menuitem_unlock_tbp, FALSE);
                gtk_widget_set_visible(ltbp->p_menuitem_new_instance, FALSE);
                gtk_widget_set_visible(ltbp->p_menuitem_separator, FALSE);
            }
#endif
            if (tb->workspace_menu0)
            {
                GList *items = gtk_container_get_children(GTK_CONTAINER(gtk_widget_get_parent(tb->workspace_menu0)));
                GList *item = g_list_find(items, tb->workspace_menu0);
                int i;
                if (item != NULL) /* else error */
                    for (i = 0; i < tb->number_of_desktops; i++, item = item->next)
                        gtk_widget_set_sensitive(item->data, i != tk->desktop);
                g_list_free(items);
            }
            gtk_menu_popup(
                GTK_MENU(tb->menu),
                NULL, NULL,
                (GtkMenuPositionFunc) taskbar_popup_set_position, (gpointer) visible_task,
                0, event->time);
        }
    }

    /* As a matter of policy, avoid showing selected or prelight states on flat buttons. */
    if (tb->flat_button)
        gtk_widget_set_state(widget, GTK_STATE_NORMAL);
    return TRUE;
}

/* Handler for "button-press-event" event from taskbar button. */
static gboolean taskbar_button_press_event(GtkWidget * widget, GdkEventButton * event, Task * tk)
{
    // needed to prevent releasing focused task button
    return TRUE;
}

/* Handler for "button-release-event" event from taskbar button. */
static gboolean taskbar_button_release_event(GtkWidget * widget, GdkEventButton * event, Task * tk)
{
    if (!tk->tb->dnd_task_moving && tk->entered_state)
        /* SF bug#731: don't process button release with DND. Also if button was
           released outside of widget but DND wasn't activated: this might happen
           if drag started at edge of button so drag treshold wasn't reached. */
        return taskbar_task_control_event(widget, event, tk, FALSE);
    return TRUE;
}

/* Handler for "activate" event from grouped-task popup menu item. */
static gboolean taskbar_popup_activate_event(GtkWidget * widget, GdkEventButton * event, Task * tk)
{
    return taskbar_task_control_event(widget, event, tk, TRUE);
}

/* Handler for "drag-motion" timeout. */
static gboolean taskbar_button_drag_motion_timeout(Task * tk)
{
    guint time;
    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;
    time = gtk_get_current_event_time();
    task_raise_window(tk, ((time != 0) ? time : CurrentTime));
    tk->tb->dnd_delay_timer = 0;
    return FALSE;
}

/* Handler for "drag-motion" event from taskbar button. */
static gboolean taskbar_button_drag_motion(GtkWidget * widget, GdkDragContext * drag_context, gint x, gint y, guint time, Task * tk)
{
    GtkWidget * drag_source = gtk_drag_get_source_widget(drag_context);
    if (drag_source != NULL && gtk_widget_get_parent(drag_source) == gtk_widget_get_parent(tk->button))
    {
        tk->tb->dnd_task_moving = TRUE;
        gdk_drag_status(drag_context, GDK_ACTION_MOVE, time);
    }
    else
    {
        /* Prevent excessive motion notification. */
        if (tk->tb->dnd_delay_timer == 0)
            tk->tb->dnd_delay_timer = g_timeout_add(DRAG_ACTIVE_DELAY, (GSourceFunc) taskbar_button_drag_motion_timeout, tk);

        gdk_drag_status(drag_context, 0, time);
    }
    return TRUE;
}

/* Handler for "drag-drop" event from taskbar button. */
static gboolean taskbar_button_drag_drop(GtkWidget * widget, GdkDragContext * drag_context, gint x, gint y, guint time, Task * tk)
{
    tk->tb->dnd_task_moving = FALSE;
    GtkWidget * drag_source = gtk_drag_get_source_widget(drag_context);
    if (drag_source != NULL && gtk_widget_get_parent(drag_source) == gtk_widget_get_parent(tk->button))
    {
        if (drag_source != tk->button)
        {
            PanelIconGrid *ig = PANEL_ICON_GRID(tk->tb->tb_icon_grid);
            gint i = panel_icon_grid_get_child_position(ig, tk->button);
            panel_icon_grid_reorder_child(ig, drag_source, i);
        }
        gtk_drag_finish(drag_context, TRUE, TRUE, time);
        return TRUE;
    }

    return FALSE;
}

/* Handler for "drag-leave" event from taskbar button. */
static void taskbar_button_drag_leave(GtkWidget * widget, GdkDragContext * drag_context, guint time, Task * tk)
{
    /* Cancel the timer if set. */
    if (tk->tb->dnd_delay_timer != 0)
    {
        g_source_remove(tk->tb->dnd_delay_timer);
        tk->tb->dnd_delay_timer = 0;
    }
    return;
}

/* Handler for "enter" event from taskbar button.  This indicates that the cursor position has entered the button. */
static void taskbar_button_enter(GtkWidget * widget, Task * tk)
{
    tk->tb->dnd_task_moving = FALSE;
    tk->entered_state = TRUE;
    if (tk->tb->flat_button)
        gtk_widget_set_state(widget, GTK_STATE_NORMAL);
    task_draw_label(tk);
}

/* Handler for "leave" event from taskbar button.  This indicates that the cursor position has left the button. */
static void taskbar_button_leave(GtkWidget * widget, Task * tk)
{
    tk->entered_state = FALSE;
    task_draw_label(tk);
}

/* Handler for "scroll-event" event from taskbar button. */
static gboolean taskbar_button_scroll_event(GtkWidget * widget, GdkEventScroll * event, Task * tk)
{
    LaunchTaskBarPlugin * tb = tk->tb;
    TaskClass * tc = tk->p_taskclass;
    if ((tb->use_mouse_wheel)
    && (( ! tb->grouped_tasks) || (tc == NULL) || (tc->visible_count == 1)))
    {
        if ((event->direction == GDK_SCROLL_UP) || (event->direction == GDK_SCROLL_LEFT))
            task_raise_window(tk, event->time);
        else
        {
            Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
            XIconifyWindow(xdisplay, tk->win, DefaultScreen(xdisplay));
        }
    }
    return TRUE;
}

/* Handler for "size-allocate" event from taskbar button. */
static void taskbar_button_size_allocate(GtkWidget * btn, GtkAllocation * alloc, Task * tk)
{
    if (gtk_widget_get_realized(btn))
    {
        /* Get the coordinates of the button. */
        int x, y;
        gdk_window_get_origin(gtk_button_get_event_window(GTK_BUTTON(btn)), &x, &y);


        /* Send a NET_WM_ICON_GEOMETRY property change on the window. */
        gulong data[4];
        data[0] = x;
        data[1] = y;
        data[2] = alloc->width;
        data[3] = alloc->height;
        XChangeProperty(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), tk->win,
            gdk_x11_get_xatom_by_name("_NET_WM_ICON_GEOMETRY"),
            XA_CARDINAL, 32, PropModeReplace, (guchar *) &data, 4);
    }
}

/* Update style on the taskbar when created or after a configuration change. */
static void taskbar_update_style(LaunchTaskBarPlugin * tb)
{
    panel_icon_grid_set_geometry(PANEL_ICON_GRID(tb->tb_icon_grid),
        panel_get_orientation(tb->panel),
        ((tb->icons_only) ? tb->icon_size + ICON_ONLY_EXTRA : tb->task_width_max),
        tb->icon_size, tb->spacing, 0, panel_get_height(tb->panel));
}

/* Update style on a task button when created or after a configuration change. */
static void task_update_style(Task * tk, LaunchTaskBarPlugin * tb)
{
    if (tb->icons_only)
        gtk_widget_hide(tk->label);
    else
        gtk_widget_show(tk->label);

    if( tb->flat_button )
    {
        gtk_toggle_button_set_active((GtkToggleButton*)tk->button, FALSE);
        gtk_button_set_relief(GTK_BUTTON(tk->button), GTK_RELIEF_NONE);
    }
    else
    {
        gtk_toggle_button_set_active((GtkToggleButton*)tk->button, tk->focused);
        gtk_button_set_relief(GTK_BUTTON(tk->button), GTK_RELIEF_NORMAL);
    }

    task_draw_label(tk);
}

enum {
    TARGET_TASK_BUTTON
};

static GtkTargetEntry task_button_target_list[] = {
    { "task_button", GTK_TARGET_SAME_APP, TARGET_TASK_BUTTON }
};

static guint task_button_n_targets = G_N_ELEMENTS(task_button_target_list);

/* Build graphic elements needed for a task button. */
static void task_build_gui(LaunchTaskBarPlugin * tb, Task * tk)
{
    GdkDisplay *display = gdk_display_get_default();
    /* NOTE
     * 1. the extended mask is sum of taskbar and pager needs
     * see bug [ 940441 ] pager loose track of windows
     *
     * Do not change event mask to gtk windows spawned by this gtk client
     * this breaks gtk internals */
#if GTK_CHECK_VERSION(2, 24, 0)
    if (!gdk_x11_window_lookup_for_display(display, tk->win))
#else
    if (! gdk_window_lookup(tk->win))
#endif
        XSelectInput(GDK_DISPLAY_XDISPLAY(display), tk->win,
                     PropertyChangeMask | StructureNotifyMask);

    /* Allocate a toggle button as the top level widget. */
    tk->button = gtk_toggle_button_new();
    gtk_container_set_border_width(GTK_CONTAINER(tk->button), 0);
    if (!tb->flat_button)
        gtk_widget_set_state(tk->button, GTK_STATE_NORMAL);
    gtk_drag_dest_set(tk->button, 0, NULL, 0, 0);
    gtk_drag_source_set(tk->button, GDK_BUTTON1_MASK, task_button_target_list, task_button_n_targets, GDK_ACTION_MOVE);

    /* Connect signals to the button. */
    g_signal_connect(tk->button, "button-press-event", G_CALLBACK(taskbar_button_press_event), (gpointer) tk);
    g_signal_connect(tk->button, "button-release-event", G_CALLBACK(taskbar_button_release_event), (gpointer) tk);
    g_signal_connect(G_OBJECT(tk->button), "drag-motion", G_CALLBACK(taskbar_button_drag_motion), (gpointer) tk);
    g_signal_connect(G_OBJECT(tk->button), "drag-leave", G_CALLBACK(taskbar_button_drag_leave), (gpointer) tk);
    g_signal_connect(G_OBJECT(tk->button), "drag-drop", G_CALLBACK(taskbar_button_drag_drop), (gpointer) tk);
    g_signal_connect_after(G_OBJECT (tk->button), "enter", G_CALLBACK(taskbar_button_enter), (gpointer) tk);
    g_signal_connect_after(G_OBJECT (tk->button), "leave", G_CALLBACK(taskbar_button_leave), (gpointer) tk);
    g_signal_connect_after(G_OBJECT(tk->button), "scroll-event", G_CALLBACK(taskbar_button_scroll_event), (gpointer) tk);
    g_signal_connect(tk->button, "size-allocate", G_CALLBACK(taskbar_button_size_allocate), (gpointer) tk);

    /* Create a box to contain the application icon and window title. */
    GtkWidget * container = gtk_hbox_new(FALSE, 1);
    gtk_container_set_border_width(GTK_CONTAINER(container), 0);

    /* Create an image to contain the application icon and add it to the box. */
    GdkPixbuf* pixbuf = task_update_icon(tb, tk, None);
    tk->image = gtk_image_new_from_pixbuf(pixbuf);
    gtk_misc_set_padding(GTK_MISC(tk->image), 0, 0);
    g_object_unref(pixbuf);
    gtk_widget_show(tk->image);
    gtk_box_pack_start(GTK_BOX(container), tk->image, FALSE, FALSE, 0);

    /* Create a label to contain the window title and add it to the box. */
    tk->label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(tk->label), 0.0, 0.5);
    gtk_label_set_ellipsize(GTK_LABEL(tk->label), PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(container), tk->label, TRUE, TRUE, 0);

    /* Add the box to the button. */
    gtk_container_add(GTK_CONTAINER(tk->button), container);
    gtk_container_set_border_width(GTK_CONTAINER(tk->button), 0);

    /* Add the button to the taskbar. */
    gtk_container_add(GTK_CONTAINER(tb->tb_icon_grid), tk->button);
    gtk_widget_show_all(tk->button);
    gtk_widget_set_can_focus(GTK_WIDGET(tk->button),FALSE);
    gtk_widget_set_can_default(GTK_WIDGET(tk->button),FALSE);

    /* Update styles on the button. */
    task_update_style(tk, tb);

    /* Flash button for window with urgency hint. */
    if (tk->urgency && !tk->focused)
        task_set_urgency(tk);
}

/* Determine which monitor a given window is associated with */
static gint get_window_monitor(Window win)
{
    GdkDisplay *display;
    GdkWindow *gwin;
    gint m;

    display = gdk_display_get_default();
    g_assert(display);
    gwin = gdk_x11_window_foreign_new_for_display(display,win);
    g_assert(gwin);
    m = gdk_screen_get_monitor_at_window(gdk_window_get_screen(gwin),gwin);
    g_object_unref(gwin);
    return m;
}

/*****************************************************
 * handlers for NET actions                          *
 *****************************************************/

/* Handler for "client-list" event from root window listener. */
static void taskbar_net_client_list(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return;

    /* Get the NET_CLIENT_LIST property. */
    int client_count;
    Window * client_list = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST, XA_WINDOW, &client_count);
    Task * tk;
    for (tk = tb->p_task_list; tk != NULL; tk = tk->p_task_flink_xwid)
        tk->present_in_client_list = FALSE;
    if (client_list != NULL)
    {
        /* Loop over client list, correlating it with task list. */
        int i;
        for (i = 0; i < client_count; i++)
        {
            /* Search for the window in the task list.  Set up context to do an insert right away if needed. */
            Task * tk_pred = NULL;
            Task * tk_cursor;
            Task * tk = NULL;
            for (tk_cursor = tb->p_task_list; tk_cursor != NULL; tk_pred = tk_cursor, tk_cursor = tk_cursor->p_task_flink_xwid)
            {
                if (tk_cursor->win == client_list[i])
                {
                    tk = tk_cursor;
                    break;
                }
                if (tk_cursor->win > client_list[i])
                    break;
            }

            /* Task is already in task list. */
            if (tk != NULL)
                tk->present_in_client_list = TRUE;

            /* Task is not in task list. */
            else
            {
                /* Evaluate window state and window type to see if it should be in task list. */
                NetWMWindowType nwwt;
                NetWMState nws;
                get_net_wm_state(client_list[i], &nws);
                get_net_wm_window_type(client_list[i], &nwwt);
                if ((accept_net_wm_state(&nws))
                && (accept_net_wm_window_type(&nwwt)))
                {
                    /* Allocate and initialize new task structure. */
                    tk = g_new0(Task, 1);
                    tk->present_in_client_list = TRUE;
                    tk->win = client_list[i];
                    tk->tb = tb;
                    tk->name_source = None;
                    tk->image_source = None;
                    tk->iconified = (get_wm_state(tk->win) == IconicState);
                    tk->desktop = get_net_wm_desktop(tk->win);
                    if (tb->same_monitor_only)
                        tk->monitor = get_window_monitor(tk->win);
                    if (tb->use_urgency_hint)
                        tk->urgency = task_has_urgency(tk);
                    task_build_gui(tb, tk);
                    task_set_names(tk, None);
                    task_set_class(tk);

                    /* Link the task structure into the task list. */
                    if (tk_pred == NULL)
                    {
                        tk->p_task_flink_xwid = tb->p_task_list;
                        tb->p_task_list = tk;
                    }
                    else
                    {
                        tk->p_task_flink_xwid = tk_pred->p_task_flink_xwid;
                        tk_pred->p_task_flink_xwid = tk;
                    }
                }
            }
        }
        XFree(client_list);
    }

    /* Remove windows from the task list that are not present in the NET_CLIENT_LIST. */
    Task * tk_pred = NULL;
    tk = tb->p_task_list;
    while (tk != NULL)
    {
        Task * tk_succ = tk->p_task_flink_xwid;
        if (tk->present_in_client_list)
            tk_pred = tk;
        else
        {
            if (tk_pred == NULL)
                tb->p_task_list = tk_succ;
            else
                tk_pred->p_task_flink_xwid = tk_succ;
            task_delete(tb, tk, FALSE, TRUE);
        }
        tk = tk_succ;
    }

    /* Redraw the taskbar. */
    taskbar_redraw(tb);
}

/* Handler for "current-desktop" event from root window listener. */
static void taskbar_net_current_desktop(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return;

    /* Store the local copy of current desktops.  Redisplay the taskbar. */
    tb->current_desktop = get_net_current_desktop();
    recompute_group_visibility_on_current_desktop(tb);
    taskbar_redraw(tb);
}

/* Handler for "number-of-desktops" event from root window listener. */
static void taskbar_net_number_of_desktops(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return;

    /* Store the local copy of number of desktops.  Recompute the popup menu and redisplay the taskbar. */
    tb->number_of_desktops = get_net_number_of_desktops();
    taskbar_make_menu(tb);
    taskbar_redraw(tb);
}

/* Handler for "active-window" event from root window listener. */
static void taskbar_net_active_window(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return;

    gboolean drop_old = FALSE;
    gboolean make_new = FALSE;
    Task * ctk = tb->focused;
    Task * ntk = NULL;

    /* Get the window that has focus. */
    Window * f = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_ACTIVE_WINDOW, XA_WINDOW, 0);
    if (f == NULL)
    {
        /* No window has focus. */
        drop_old = TRUE;
        tb->focused_previous = NULL;
    }
    else
    {
        if (*f == panel_get_xwindow(tb->panel))
        {
        /* Taskbar window gained focus (this isn't supposed to be able to happen).  Remember current focus. */
            if (ctk != NULL)
            {
                tb->focused_previous = ctk;
                drop_old = TRUE;
            }
        }
        else
        {
            /* Identify task that gained focus. */
            tb->focused_previous = NULL;
            ntk = task_lookup(tb, *f);
            if (ntk != ctk)
            {
                drop_old = TRUE;
                make_new = TRUE;
            }
        }
        XFree(f);
    }

    /* If our idea of the current task lost focus, update data structures. */
    if ((ctk != NULL) && (drop_old))
    {
        ctk->focused = FALSE;
        if (ctk->urgency)
            task_set_urgency(ctk);
        tb->focused = NULL;
        if(!tb->flat_button) /* relieve the button if flat buttons is not used. */
            gtk_toggle_button_set_active((GtkToggleButton*)ctk->button, FALSE);

        task_button_redraw(ctk, tb);
    }

    /* If a task gained focus, update data structures. */
    if ((ntk != NULL) && (make_new))
    {
        if(!tb->flat_button) /* depress the button if flat buttons is not used. */
            gtk_toggle_button_set_active((GtkToggleButton*)ntk->button, TRUE);
        ntk->focused = TRUE;
        if (ntk->urgency)
            task_clear_urgency(ntk);
        tb->focused = ntk;
        task_button_redraw(ntk, tb);
    }
}

/* Determine if the "urgency" hint is set on a window. */
static gboolean task_has_urgency(Task * tk)
{
    gboolean result = FALSE;
    XWMHints * hints = (XWMHints *) get_xaproperty(tk->win, XA_WM_HINTS, XA_WM_HINTS, 0);
    if (hints != NULL)
    {
        if (hints->flags & XUrgencyHint)
            result = TRUE;
        XFree(hints);
    }
    return result;
}

/* Handle PropertyNotify event.
 * http://tronche.com/gui/x/icccm/
 * http://standards.freedesktop.org/wm-spec/wm-spec-1.4.html */
static void taskbar_property_notify_event(LaunchTaskBarPlugin *tb, XEvent *ev)
{
    /* State may be PropertyNewValue, PropertyDeleted. */
    if (((XPropertyEvent*) ev)->state == PropertyNewValue)
    {
        Atom at = ev->xproperty.atom;
        Window win = ev->xproperty.window;
        if (win != GDK_ROOT_WINDOW())
        {
            /* Look up task structure by X window handle. */
            Task * tk = task_lookup(tb, win);
            if (tk != NULL)
            {
                /* Install an error handler that ignores BadWindow.
                 * We frequently get a PropertyNotify event on deleted windows. */
                XErrorHandler previous_error_handler = XSetErrorHandler(panel_handle_x_error_swallow_BadWindow_BadDrawable);

                /* Dispatch on atom. */
                if (at == a_NET_WM_DESKTOP)
                {
                    /* Window changed desktop. */
                    tk->desktop = get_net_wm_desktop(win);
                    taskbar_redraw(tb);
                }
                else if ((at == XA_WM_NAME) || (at == a_NET_WM_NAME) || (at == a_NET_WM_VISIBLE_NAME))
                {
                    /* Window changed name. */
                    task_set_names(tk, at);
                    if (tk->p_taskclass != NULL)
                    {
                        /* A change to the window name may change the visible name of the class. */
                        recompute_group_visibility_for_class(tb, tk->p_taskclass);
                        if (tk->p_taskclass->p_task_visible != NULL)
                            task_draw_label(tk->p_taskclass->p_task_visible);
                    }
                }
                else if (at == XA_WM_CLASS)
                {
                    /* Window changed class. */
                    task_set_class(tk);
                    taskbar_redraw(tb);
                }
                else if (at == a_WM_STATE)
                {
                    /* Window changed state. */
                    tk->iconified = (get_wm_state(win) == IconicState);
                    task_draw_label(tk);
                }
                else if (at == XA_WM_HINTS)
                {
                    /* Window changed "window manager hints".
                     * Some windows set their WM_HINTS icon after mapping. */
                    GdkPixbuf * pixbuf = task_update_icon(tb, tk, XA_WM_HINTS);
                    if (pixbuf != NULL)
                    {
                        gtk_image_set_from_pixbuf(GTK_IMAGE(tk->image), pixbuf);
                        g_object_unref(pixbuf);
                    }

                    if (tb->use_urgency_hint)
                    {
                        tk->urgency = task_has_urgency(tk);
                        if (tk->urgency && !tk->focused)
                            task_set_urgency(tk);
                        else
                            task_clear_urgency(tk);
                    }
                }
                else if (at == a_NET_WM_STATE)
                {
                    /* Window changed EWMH state. */
                    NetWMState nws;
                    get_net_wm_state(tk->win, &nws);
                    if ( ! accept_net_wm_state(&nws))
                    {
                        task_delete(tb, tk, TRUE, TRUE);
                        taskbar_redraw(tb);
                    }
                }
                else if (at == a_NET_WM_ICON)
                {
                    /* Window changed EWMH icon. */
                    GdkPixbuf * pixbuf = task_update_icon(tb, tk, a_NET_WM_ICON);
                    if (pixbuf != NULL)
                    {
                        gtk_image_set_from_pixbuf(GTK_IMAGE(tk->image), pixbuf);
                        g_object_unref(pixbuf);
                    }
                }
                else if (at == a_NET_WM_WINDOW_TYPE)
                {
                    /* Window changed EWMH window type. */
                    NetWMWindowType nwwt;
                    get_net_wm_window_type(tk->win, &nwwt);
                    if ( ! accept_net_wm_window_type(&nwwt))
                    {
                        task_delete(tb, tk, TRUE, TRUE);
                        taskbar_redraw(tb);
                    }
                }
                XSetErrorHandler(previous_error_handler);
            }
        }
    }
}

/* Handle ConfigureNotify events */
static void taskbar_configure_notify_event(LaunchTaskBarPlugin * tb, XConfigureEvent * ev)
{
    /* If the same_monitor_only option is set and the window is on a different
       monitor than before, redraw the taskbar */
    Task *task;
    gint m;
    if(tb->same_monitor_only && ev->window != GDK_ROOT_WINDOW())
    {
        task = task_lookup(tb, ev->window);
        if(task)
        {
            /* Deleted windows seem to get ConfigureNotify events too. */
            XErrorHandler previous_error_handler = XSetErrorHandler(panel_handle_x_error_swallow_BadWindow_BadDrawable);

            m = get_window_monitor(task->win);
            if(m != task->monitor)
            {
                task->monitor = m;
                taskbar_redraw(tb);
            }

            XSetErrorHandler(previous_error_handler);
        }
    }
}

/* GDK event filter. */
static GdkFilterReturn taskbar_event_filter(XEvent * xev, GdkEvent * event, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = tb;
    if(ltbp->mode == LAUNCHBAR) return GDK_FILTER_CONTINUE;

    if (xev->type == PropertyNotify)
        taskbar_property_notify_event(tb, xev);
    else if (xev->type == ConfigureNotify)
        taskbar_configure_notify_event(tb, &xev->xconfigure);

    return GDK_FILTER_CONTINUE;
}

/* Handler for "activate" event on Raise item of right-click menu for task buttons. */
static void menu_raise_window(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    if ((tb->menutask->desktop != ALL_WORKSPACES) && (tb->menutask->desktop != tb->current_desktop))
        Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, tb->menutask->desktop, 0, 0, 0, 0);
    XMapRaised(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), tb->menutask->win);
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Restore item of right-click menu for task buttons. */
static void menu_restore_window(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
#if GTK_CHECK_VERSION(2, 24, 0)
    GdkWindow * win = gdk_x11_window_foreign_new_for_display(gdk_display_get_default(),
                                                             tb->menutask->win);
#else
    GdkWindow * win = gdk_window_foreign_new(tb->menutask->win);
#endif

    gdk_window_unmaximize(win);
    g_object_unref(win);
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Maximize item of right-click menu for task buttons. */
static void menu_maximize_window(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
#if GTK_CHECK_VERSION(2, 24, 0)
    GdkWindow * win = gdk_x11_window_foreign_new_for_display(gdk_display_get_default(),
                                                             tb->menutask->win);
#else
    GdkWindow * win = gdk_window_foreign_new(tb->menutask->win);
#endif

    gdk_window_maximize(win);
    g_object_unref(win);
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Iconify item of right-click menu for task buttons. */
static void menu_iconify_window(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    XIconifyWindow(xdisplay, tb->menutask->win, DefaultScreen(xdisplay));
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Move to Workspace item of right-click menu for task buttons. */
static void menu_move_to_workspace(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    int num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "num"));
    Xclimsg(tb->menutask->win, a_NET_WM_DESKTOP, num, 0, 0, 0, 0);
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Close item of right-click menu for task buttons. */
static void menu_close_window(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    Xclimsgwm(tb->menutask->win, a_WM_PROTOCOLS, a_WM_DELETE_WINDOW);
    task_group_menu_destroy(tb);
}

#ifndef DISABLE_MENU
static void  on_menuitem_lock_tbp_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    FmFileInfo *fi = f_find_menu_launchbutton_recursive(tb->menutask->exec_bin);
    LaunchButton *btn;
    char *path;

    if (fi)
    {
        /* Create a button and add settings for it */
        btn = launchbutton_for_file_info(tb, fi);
        path = fm_path_to_str(fm_file_info_get_path(fi));
        /* g_debug("*** path '%s'",path); */
        btn->settings = config_group_add_subgroup(tb->settings, "Button");
        config_group_set_string(btn->settings, "id", path);
        g_free(path);
        lxpanel_config_save(tb->panel);
    }
}

static void  on_menuitem_unlock_tbp_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)tb;
    FmFileInfo *fi = f_find_menu_launchbutton_recursive(tb->menutask->exec_bin);
    LaunchButton *btn = launchbar_exec_bin_exists(ltbp, fi);
    if(btn != NULL)
    {
        launchbar_remove_button(ltbp, btn);
        lxpanel_config_save(tb->panel);
    }
    if (fi)
        fm_file_info_unref(fi);
}

static void  on_menuitem_new_instance_clicked(GtkWidget * widget, LaunchTaskBarPlugin * tb)
{
    FmFileInfo *fi = f_find_menu_launchbutton_recursive(tb->menutask->exec_bin);

    if (fi)
    {
        lxpanel_launch_path(tb->panel, fm_file_info_get_path(fi));
        fm_file_info_unref(fi);
    }
}
#endif

/* Make right-click menu for task buttons.
 * This depends on number of desktops and edge. */
static void taskbar_make_menu(LaunchTaskBarPlugin * tb)
{
    void (*_m_add)(GtkMenuShell *self, GtkWidget* child);

    /* Deallocate old menu if present. */
    if (tb->menu != NULL)
        gtk_widget_destroy(tb->menu);
    /* The pointer to menu became invalid, reset it now. */
    tb->workspace_menu0 = NULL;

    /* Allocate menu. */
    GtkWidget * menu = gtk_menu_new();

    /* Add Raise menu item. */
    GtkWidget *mi = gtk_menu_item_new_with_mnemonic(_("_Raise"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", (GCallback) menu_raise_window, tb);

    /* Add Restore menu item. */
    mi = gtk_menu_item_new_with_mnemonic(_("R_estore"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", (GCallback) menu_restore_window, tb);

    /* Add Maximize menu item. */
    mi = gtk_menu_item_new_with_mnemonic(_("Ma_ximize"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", (GCallback) menu_maximize_window, tb);

    /* Add Iconify menu item. */
    mi = gtk_menu_item_new_with_mnemonic(_("Ico_nify"));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    g_signal_connect(G_OBJECT(mi), "activate", (GCallback) menu_iconify_window, tb);

    /* FIXME: if WM is Openbox then add "Window special parameters" submenu */

    /* If multiple desktops are supported, add menu items to select them. */
    if (tb->number_of_desktops > 1)
    {
        char label[128];

        /* Allocate submenu. */
        GtkWidget * workspace_menu = gtk_menu_new();

        /* Loop over all desktops. */
        int i;
        for (i = 1; i <= tb->number_of_desktops; i++)
        {
            /* For the first 9 desktops, allow the desktop number as a keyboard shortcut. */
            if (i <= 9)
            {
                g_snprintf(label, sizeof(label), _("Workspace _%d"), i);
                mi = gtk_menu_item_new_with_mnemonic(label);
            }
            else
            {
                g_snprintf(label, sizeof(label), _("Workspace %d"), i);
                mi = gtk_menu_item_new_with_label(label);
            }

            /* Set the desktop number as a property on the menu item. */
            g_object_set_data(G_OBJECT(mi), "num", GINT_TO_POINTER(i - 1));
            g_signal_connect(mi, "activate", G_CALLBACK(menu_move_to_workspace), tb);
            gtk_menu_shell_append(GTK_MENU_SHELL(workspace_menu), mi);
            if (G_UNLIKELY(tb->workspace_menu0 == NULL))
                tb->workspace_menu0 = mi;
        }

        /* Add a separator. */
        gtk_menu_shell_append(GTK_MENU_SHELL(workspace_menu), gtk_separator_menu_item_new());

        /* Add "move to all workspaces" item.  This causes the window to be visible no matter what desktop is active. */
        mi = gtk_menu_item_new_with_mnemonic(_("_All workspaces"));
        g_object_set_data(G_OBJECT(mi), "num", GINT_TO_POINTER(ALL_WORKSPACES));
        g_signal_connect(mi, "activate", G_CALLBACK(menu_move_to_workspace), tb);
        gtk_menu_shell_append(GTK_MENU_SHELL(workspace_menu), mi);

        /* FIXME: add "Current workspace" item, active if not on a current */

        /* Add Move to Workspace menu item as a submenu. */
        mi = gtk_menu_item_new_with_mnemonic(_("_Move to Workspace"));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), workspace_menu);
    }

    /* Add Close menu item.  By popular demand, we place this menu item closest to the cursor. */
    mi = gtk_menu_item_new_with_mnemonic (_("_Close Window"));
#ifndef DISABLE_MENU
    tb->p_menuitem_lock_tbp = gtk_menu_item_new_with_mnemonic(_("A_dd to Launcher"));
    tb->p_menuitem_unlock_tbp = gtk_menu_item_new_with_mnemonic(_("Rem_ove from Launcher"));
    tb->p_menuitem_new_instance = gtk_menu_item_new_with_mnemonic(_("_New Instance"));
    tb->p_menuitem_separator = gtk_separator_menu_item_new();
#endif

    if (panel_is_at_bottom(tb->panel))
        _m_add = gtk_menu_shell_append;
    else
        _m_add = gtk_menu_shell_prepend;

#ifndef DISABLE_MENU
    _m_add(GTK_MENU_SHELL(menu), tb->p_menuitem_separator);
    _m_add(GTK_MENU_SHELL(menu), tb->p_menuitem_lock_tbp);
    _m_add(GTK_MENU_SHELL(menu), tb->p_menuitem_unlock_tbp);
    _m_add(GTK_MENU_SHELL(menu), tb->p_menuitem_new_instance);
#endif
    _m_add(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    _m_add(GTK_MENU_SHELL(menu), mi);

    g_signal_connect(G_OBJECT(mi), "activate", (GCallback)menu_close_window, tb);
#ifndef DISABLE_MENU
    g_signal_connect(G_OBJECT(tb->p_menuitem_lock_tbp), "activate", (GCallback)on_menuitem_lock_tbp_clicked, tb);
    g_signal_connect(G_OBJECT(tb->p_menuitem_unlock_tbp), "activate", (GCallback)on_menuitem_unlock_tbp_clicked, tb);
    g_signal_connect(G_OBJECT(tb->p_menuitem_new_instance), "activate", (GCallback)on_menuitem_new_instance_clicked, tb);
#endif
    gtk_widget_show_all(menu);
    tb->menu = menu;
}

/* Handler for "window-manager-changed" event. */
static void taskbar_window_manager_changed(GdkScreen * screen, LaunchTaskBarPlugin * tb)
{
    /* Force re-evaluation of use_net_active. */
    tb->net_active_checked = FALSE;
}

/* Callback from configuration dialog mechanism to apply the configuration. */
static void taskbar_apply_configuration(LaunchTaskBarPlugin *ltbp)
{
    Task * tk;

    /* Update style on taskbar. */
    taskbar_update_style(ltbp);

    /* Update styles on each button. */
    for (tk = ltbp->p_task_list; tk != NULL; tk = tk->p_task_flink_xwid)
    {
        /* If same_monitor_only wasn't set before, the monitor information
           wasn't tracked, so update it now. */
        if (ltbp->same_monitor_only)
            tk->monitor = get_window_monitor(tk->win);

        task_update_style(tk, ltbp);
    }

    /* Refetch the client list and redraw. */
    recompute_group_visibility_on_current_desktop(ltbp);
    taskbar_net_client_list(NULL, ltbp);
}

static GtkWidget *launchbar_constructor(LXPanel *panel, config_setting_t *settings)
{
    return _launchtaskbar_constructor(panel, settings, LAUNCHBAR);
}

static GtkWidget *taskbar_constructor(LXPanel *panel, config_setting_t *settings)
{
    return _launchtaskbar_constructor(panel, settings, TASKBAR);
}

static LXPanelPluginInit _launchbar_init = {
    .name = N_("Application Launch Bar"),
    .description = N_("Bar with buttons to launch application"),

    .new_instance = launchbar_constructor,
    .config = launchtaskbar_configure,
    .reconfigure = launchtaskbar_panel_configuration_changed
};

static LXPanelPluginInit _taskbar_init = {
    .name = N_("Task Bar (Window List)"),
    .description = N_("Taskbar shows all opened windows and allow to iconify them, shade or get focus"),

    .expand_available = TRUE,
    .expand_default = TRUE,

    .new_instance = taskbar_constructor,
    .config = launchtaskbar_configure,
    .reconfigure = launchtaskbar_panel_configuration_changed
};

static void launchtaskbar_init(void)
{
    lxpanel_register_plugin_type("launchbar", &_launchbar_init);
    lxpanel_register_plugin_type("taskbar", &_taskbar_init);
}

/* Plugin descriptor. */
LXPanelPluginInit lxpanel_static_plugin_launchtaskbar = {
    .name = N_("Application Launch and Task Bar"),
    .description = N_("Bar with buttons to launch application and/or show all opened windows"),

    .expand_available = TRUE,
    .expand_default = TRUE,

    .init = launchtaskbar_init,
    .new_instance = launchtaskbar_constructor,
    .config = launchtaskbar_configure,
    /* .update_context_menu = launchtaskbar_update_context_menu, */
    .reconfigure = launchtaskbar_panel_configuration_changed
};


/* vim: set sw=4 sts=4 et : */
