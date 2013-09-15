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

/*
 * Started by Giuseppe Penone <giuspen@gmail.com> merging launchbar and taskbar
 * and adding interoperability between them.
*/

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

#include <menu-cache.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "icon.xpm"
#include "gtkbar.h"
#include "icon-grid.h"
#include "menu-policy.h"

#include "dbg.h"


struct _taskbar;
struct _task_class;
struct _task;

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
    struct _taskbar * tb;                   /* Back pointer to taskbar */
    Window win;                             /* X window ID */
    char * name;                            /* Taskbar label when normal, from WM_NAME or NET_WM_NAME */
    char * name_iconified;                  /* Taskbar label when iconified */
    char exec_bin[128];                     /* Exec bin associated to Window */
    Atom name_source;                       /* Atom that is the source of taskbar label */
    TaskClass * p_taskclass;                /* Class, from WM_CLASS */
    struct _task * p_task_flink_same_class; /* Forward link to task in same class */
    GtkWidget * button;                     /* Button representing task in taskbar */
    GtkWidget * image;                      /* Icon for task, child of button */
    Atom image_source;                      /* Atom that is the source of taskbar icon */
    GtkWidget * label;                      /* Label for task, child of button */
    int desktop;                            /* Desktop that contains task, needed to switch to it on Raise */
    gint monitor;                           /* Monitor that the window is on or closest to */
    guint flash_timeout;                    /* Timer for urgency notification */
    unsigned int focused                :1; /* True if window has focus */
    unsigned int iconified              :1; /* True if window is iconified, from WM_STATE */
    unsigned int urgency                :1; /* True if window has an urgency hint, from WM_HINTS */
    unsigned int flash_state            :1; /* One-bit counter to flash taskbar */
    unsigned int entered_state          :1; /* True if cursor is inside taskbar button */
    unsigned int present_in_client_list :1; /* State during WM_CLIENT_LIST processing to detect deletions */
} Task;

/* Private context for taskbar plugin. */
typedef struct _taskbar {
    Plugin * plug;                 /* Back pointer to Plugin */
    Task * p_task_list;            /* List of tasks to be displayed in taskbar */
    TaskClass * p_taskclass_list;  /* Window class list */
    IconGrid * icon_grid;          /* Manager for taskbar buttons */
    GtkWidget * menu;              /* Popup menu for task control (Close, Raise, etc.) */
    GtkWidget * group_menu;        /* Popup menu for grouping selection */
    GdkPixbuf * fallback_pixbuf;   /* Fallback task icon when none is available */
    int number_of_desktops;        /* Number of desktops, from NET_WM_NUMBER_OF_DESKTOPS */
    int current_desktop;           /* Current desktop, from NET_WM_CURRENT_DESKTOP */
    Task * focused;                /* Task that has focus */
    Task * focused_previous;       /* Task that had focus just before panel got it */
    Task * menutask;               /* Task for which popup menu is open */
    guint dnd_delay_timer;         /* Timer for drag and drop delay */
    int icon_size;                 /* Size of task icons */
    gboolean show_all_desks;       /* User preference: show windows from all desktops */
    gboolean tooltips;             /* User preference: show tooltips */
    gboolean icons_only;           /* User preference: show icons only, omit name */
    gboolean use_mouse_wheel;      /* User preference: scroll wheel does iconify and raise */
    gboolean use_urgency_hint;     /* User preference: windows with urgency will flash */
    gboolean flat_button;          /* User preference: taskbar buttons have visible background */
    gboolean grouped_tasks;        /* User preference: windows from same task are grouped onto a single button */
    gboolean same_monitor_only;    /* User preference: only show windows that are in the same monitor as the taskbar */
    int task_width_max;            /* Maximum width of a taskbar button in horizontal orientation */
    int spacing;                   /* Spacing between taskbar buttons */
    gboolean use_net_active;       /* NET_WM_ACTIVE_WINDOW is supported by the window manager */
    gboolean net_active_checked;   /* True if use_net_active is valid */
    GtkWidget       *p_menuitem_lock_tbp;
    GtkWidget       *p_menuitem_unlock_tbp;
    GtkWidget       *p_menuitem_new_instance;
} TaskbarPlugin;

static gchar *taskbar_rc = "style 'taskbar-style'\n"
        "{\n"
        "GtkWidget::focus-line-width=0\n"
        "GtkWidget::focus-padding=0\n" /* FIXME: seem to fix #2821771, not sure if this is ok. */
        "GtkButton::default-border={0,0,0,0}\n"
        "GtkWidget::focus-padding=0\n"
        "GtkButton::default-outside-border={0,0,0,0}\n"
        "GtkButton::inner-border={0,0,0,0}\n" /* added in gtk+ 2.10 */
        "}\n"
        "widget '*.taskbar.*' style 'taskbar-style'";

static gchar *launchbar_rc = "style 'launchtaskbar-style'\n"
        "{\n"
        "GtkWidget::focus-line-width = 0\n"
        "GtkWidget::focus-padding = 0\n"
        "GtkButton::default-border = { 0, 0, 0, 0 }\n"
        "GtkButton::default-outside-border = { 0, 0, 0, 0 }\n"
        "}\n"
        "widget '*launchtaskbar*' style 'launchtaskbar-style'";

#define DRAG_ACTIVE_DELAY    1000
#define TASK_WIDTH_MAX       200
#define ALL_WORKSPACES       0xFFFFFFFF     /* 64-bit clean */
#define ICON_ONLY_EXTRA      6      /* Amount needed to have button lay out symmetrically */
#define ICON_BUTTON_TRIM 4      /* Amount needed to have button remain on panel */

/* Representative of one launch button.
 * Note that the launch parameters come from the specified desktop file, or from the configuration file.
 * This structure is also used during the "add to launchtaskbar" dialog to hold menu items. */
typedef struct {
    Plugin * plugin;            /* Back pointer to plugin */
    GtkWidget * widget;         /* Pointer to button */
    GtkWidget * image_widget;   /* Pointer to image */
    gchar * desktop_id;         /* Name of application (desktop file name less the .desktop) */
    gchar * image;              /* Image icon (from Icon entry) */
    gchar * action;             /* Action (from Exec entry) */
    gchar * exec_bin;           /* Exec bin associated to desktop file */
    gchar * tooltip;            /* Tooltip (from Name entry) */
    gchar * path;               /* Working directory requested in .desktop file */
    guchar use_terminal : 1;        /* True if Terminal=true or from configuration file */
    guchar customize_image : 1;     /* True if image icon from configuration file */
    guchar customize_action : 1;    /* True if action from configuration file */
    guchar customize_tooltip : 1;   /* True if tooltip from configuration file */
    guchar customize_path : 1;      /* True if path from configuration file */
} LaunchButton;

/* Private context for launchtaskbar plugin. */
typedef struct {
    Plugin        *plug;             /* Back pointer to Plugin */
    IconGrid      *icon_grid;        /* Icon grid managing the container */
    GSList        *buttons;          /* Launchbar buttons */
    GtkWidget     *config_dlg;       /* Configuration dialog */
    LaunchButton  *bootstrap_button; /* Bootstrapping button for empty launchtaskbar */
    GtkWidget     *p_button_add, *p_button_remove, *p_label_menu_app_exec, *p_label_def_app_exec;
} LaunchbarPlugin;

typedef struct
{
    LaunchbarPlugin  lbp;
    TaskbarPlugin    tbp;
    GtkWidget       *p_evbox_launchbar;
    GtkWidget       *p_evbox_taskbar;
    gchar           *exec_bin_mb;
    gboolean         add_mb_to_lb;
    gboolean         execute_mb;
    gboolean         found_mb;
    GKeyFile        *p_key_file_special_cases;
    
} LaunchTaskBarPlugin;

void panel_config_save(Panel * panel);  /* defined in configurator.c */

static LaunchButton *launchbar_add_button(LaunchTaskBarPlugin *ltbp, gchar *desktop_id);
static void launchbar_remove_button(LaunchTaskBarPlugin *ltbp, LaunchButton *btn);
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
static int launchtaskbar_constructor(Plugin * p, char ** fp);
static void launchtaskbar_destructor(Plugin * p);
static void launchbar_configure_add_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_remove_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_move_up_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_move_down_button(GtkButton * widget, Plugin * p);
static void launchbar_configure_response(GtkDialog * dlg, int response, Plugin * p);
static void launchbar_configure_initialize_list(Plugin * p, GtkWidget * dlg, GtkTreeView * view, gboolean from_menu);
static void launchtaskbar_configure(Plugin * p, GtkWindow * parent);
static void launchtaskbar_save_configuration(Plugin * p, FILE * fp);
static void launchtaskbar_panel_configuration_changed(Plugin * p);
static void launchbar_update_after_taskbar_class_added(LaunchTaskBarPlugin * ltbp, Task *tk);
static void launchbar_update_after_taskbar_class_removed(LaunchTaskBarPlugin * ltbp, Task *tk);

static void set_timer_on_task(Task * tk);
static gboolean task_is_visible_on_current_desktop(TaskbarPlugin * tb, Task * tk);
static void recompute_group_visibility_for_class(TaskbarPlugin * tb, TaskClass * tc);
static void recompute_group_visibility_on_current_desktop(TaskbarPlugin * tb);
static void task_draw_label(Task * tk);
static gboolean task_is_visible(TaskbarPlugin * tb, Task * tk);
static void task_button_redraw(Task * tk, TaskbarPlugin * tb);
static void taskbar_redraw(TaskbarPlugin * tb);
static gboolean accept_net_wm_state(NetWMState * nws);
static gboolean accept_net_wm_window_type(NetWMWindowType * nwwt);
static void task_free_names(Task * tk);
static void task_set_names(Task * tk, Atom source);
static void task_unlink_class(Task * tk);
static TaskClass * taskbar_enter_res_class(TaskbarPlugin * tb, char * res_class, gboolean * name_consumed);
static void task_set_class(Task * tk);
static Task * task_lookup(TaskbarPlugin * tb, Window win);
static void task_delete(TaskbarPlugin * tb, Task * tk, gboolean unlink);
static GdkPixbuf * _wnck_gdk_pixbuf_get_from_pixmap(Pixmap xpixmap, int width, int height);
static GdkPixbuf * apply_mask(GdkPixbuf * pixbuf, GdkPixbuf * mask);
static GdkPixbuf * get_wm_icon(Window task_win, int required_width, int required_height, Atom source, Atom * current_source);
static GdkPixbuf * task_update_icon(TaskbarPlugin * tb, Task * tk, Atom source);
static gboolean flash_window_timeout(Task * tk);
static void task_set_urgency(Task * tk);
static void task_clear_urgency(Task * tk);
static void task_raise_window(Task * tk, guint32 time);
static void taskbar_popup_set_position(GtkWidget * menu, gint * px, gint * py, gboolean * push_in, gpointer data);
static void task_group_menu_destroy(TaskbarPlugin * tb);
static gboolean taskbar_task_control_event(GtkWidget * widget, GdkEventButton * event, Task * tk, gboolean popup_menu);
static gboolean taskbar_button_press_event(GtkWidget * widget, GdkEventButton * event, Task * tk);
static gboolean taskbar_popup_activate_event(GtkWidget * widget, GdkEventButton * event, Task * tk);
static gboolean taskbar_button_drag_motion_timeout(Task * tk);
static gboolean taskbar_button_drag_motion(GtkWidget * widget, GdkDragContext * drag_context, gint x, gint y, guint time, Task * tk);
static void taskbar_button_drag_leave(GtkWidget * widget, GdkDragContext * drag_context, guint time, Task * tk);
static void taskbar_button_enter(GtkWidget * widget, Task * tk);
static void taskbar_button_leave(GtkWidget * widget, Task * tk);
static gboolean taskbar_button_scroll_event(GtkWidget * widget, GdkEventScroll * event, Task * tk);
static void taskbar_button_size_allocate(GtkWidget * btn, GtkAllocation * alloc, Task * tk);
static void taskbar_update_style(TaskbarPlugin * tb);
static void task_update_style(Task * tk, TaskbarPlugin * tb);
static void task_build_gui(TaskbarPlugin * tb, Task * tk);
static void taskbar_close_all_windows(GtkWidget * widget, Task * tk);
static void taskbar_net_client_list(GtkWidget * widget, TaskbarPlugin * tb);
static void taskbar_net_current_desktop(GtkWidget * widget, TaskbarPlugin * tb);
static void taskbar_net_number_of_desktops(GtkWidget * widget, TaskbarPlugin * tb);
static void taskbar_net_active_window(GtkWidget * widget, TaskbarPlugin * tb);
static gboolean task_has_urgency(Task * tk);
static void taskbar_property_notify_event(TaskbarPlugin * tb, XEvent *ev);
static GdkFilterReturn taskbar_event_filter(XEvent * xev, GdkEvent * event, TaskbarPlugin * tb);
static void menu_raise_window(GtkWidget * widget, TaskbarPlugin * tb);
static void menu_restore_window(GtkWidget * widget, TaskbarPlugin * tb);
static void menu_maximize_window(GtkWidget * widget, TaskbarPlugin * tb);
static void menu_iconify_window(GtkWidget * widget, TaskbarPlugin * tb);
static void menu_move_to_workspace(GtkWidget * widget, TaskbarPlugin * tb);
static void menu_close_window(GtkWidget * widget, TaskbarPlugin * tb);
static void on_menuitem_lock_tbp_clicked(GtkWidget * widget, TaskbarPlugin * tb);
static void on_menuitem_unlock_tbp_clicked(GtkWidget * widget, TaskbarPlugin * tb);
static void on_menuitem_new_instance_clicked(GtkWidget * widget, TaskbarPlugin * tb);
static void taskbar_make_menu(TaskbarPlugin * tb);
static void taskbar_window_manager_changed(GdkScreen * screen, TaskbarPlugin * tb);
static void taskbar_apply_configuration(Plugin * p);
static gboolean load_app_key_file(gchar *filepath, GKeyFile *p_gkeyfile);

static void f_get_exec_cmd_from_pid(GPid pid, gchar *buffer_128, const gchar *proc_file)
{
    buffer_128[0] = '\0';
    FILE *pipe;
    gchar  command[64];
    snprintf(command, 64, "cat /proc/%u/%s", pid, proc_file);
    pipe = popen(command, "r");
    if(pipe == NULL)
        g_print("ERR popen '%s'\n", command);
    else if(fgets(buffer_128, 128, pipe) == NULL)
        g_print("ERR fgets '%s'\n", command);
    else
    {
        gchar *p_char = strchr(buffer_128, '\n');
        if(p_char != NULL) *p_char = '\0';
    }
    if(pipe != NULL) pclose(pipe);
}

static gchar *f_get_clean_exec_bin(const gchar *exec_in, gchar *buffer_128)
{
    snprintf(buffer_128, 128, "%s", exec_in);
    
    gchar *p_char;
    if( (p_char = strchr(buffer_128, ' ')) != NULL )
    {
        *p_char = '\0';
    }
    p_char = strrchr(buffer_128, '/');
    if(p_char == NULL) p_char = buffer_128;
    else p_char++;
    
    return p_char;
}

static void  f_find_menu_launchbutton_recursive(MenuCacheDir *menu_dir, LaunchTaskBarPlugin *ltbp)
{
    /* Iterate over all menu items in this directory. */
    GSList * l;
    for(l = menu_cache_dir_get_children(menu_dir); l != NULL; l = l->next)
    {
        /* Get the next menu item. */
        MenuCacheItem * item = MENU_CACHE_ITEM(l->data);
        switch(menu_cache_item_get_type(item))
        {
            case MENU_CACHE_TYPE_NONE:
            case MENU_CACHE_TYPE_SEP:
                break;

            case MENU_CACHE_TYPE_APP:
            {
                /* If an application, build a LaunchButton data structure so we can identify
                 * the button in the handler.  In this application, the desktop_id is the
                 * fully qualified desktop file path.  The image and tooltip are what is displayed in the view. */
                gchar *desktop_id = menu_cache_item_get_file_path(item);

                GKeyFile *p_key_desktop = g_key_file_new();
                gboolean  loaded = load_app_key_file(desktop_id, p_key_desktop);
                gchar *exec = NULL;
                gboolean  in_terminal = TRUE;
                if(loaded)
                {
                    exec = g_key_file_get_string(p_key_desktop, DESKTOP_ENTRY, "Exec", NULL);
                    in_terminal = g_key_file_get_boolean(p_key_desktop, DESKTOP_ENTRY, "Terminal", NULL);
                    g_key_file_free(p_key_desktop);
                }
                gchar  buffer_128[128];
                gchar *p_char = f_get_clean_exec_bin(exec, buffer_128);
                if(strcmp(p_char, ltbp->exec_bin_mb) == 0)
                {
                    if(ltbp->add_mb_to_lb) launchbar_add_button(ltbp, desktop_id);
                    if(ltbp->execute_mb) lxpanel_launch_app(exec, NULL, in_terminal, menu_cache_app_get_working_dir(MENU_CACHE_APP(item)));
                    //g_print("FOUND '%s' in MB\n", p_char);
                    ltbp->found_mb = TRUE;
                }
                //else
                //{
                    //g_print("---'%s' != '%s' in MB\n", p_char, ltbp->exec_bin_mb);
                //}
                g_free(exec);
                g_free(desktop_id);
                break;
            }
            case MENU_CACHE_TYPE_DIR:
            {
                f_find_menu_launchbutton_recursive(MENU_CACHE_DIR(item), ltbp);
                break;
            }
        }
        if(ltbp->found_mb) break;
    }
}

/* Deallocate a LaunchButton. */
static void launchbutton_free(LaunchButton * btn)
{
    g_free(btn->desktop_id);
    g_free(btn->image);
    g_free(btn->action);
    g_free(btn->exec_bin);
    g_free(btn->tooltip);
    g_free(btn->path);
    g_free(btn);
}

/* Handler for "button-press-event" event from launchtaskbar button. */
static gboolean launchbutton_press_event(GtkWidget * widget, GdkEventButton * event, LaunchButton * b)
{
    /* Standard right-click handling. */
    if (plugin_button_press_event(widget, event, b->plugin))
        return TRUE;

    if (event->button == 1)    /* left button */
    {
        if (b->desktop_id == NULL)  /* The bootstrap button */
            launchtaskbar_configure(b->plugin, NULL);
        else if (b->action != NULL)
            lxpanel_launch_app(b->action, NULL, b->use_terminal, b->path);
    }
    return TRUE;
}

/* Handler for "drag-data-received" event from launchtaskbar button. */
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
    if (!b->action)
    {
        LOG(LOG_WARN, "launchtaskbar: Button '%s' has no action (%s)\n",
                b->desktop_id, b->action);
        return;
    }
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

            spawn_command_async(NULL, NULL, str);
            g_free(str);
        }
    }
}

/* Build the graphic elements for the bootstrap launchtaskbar button. */
static void launchbutton_build_bootstrap(Plugin * p)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

    if (ltbp->lbp.bootstrap_button == NULL)
    {
        /* Build a button that has the stock "Add" icon.
         * The "desktop-id" being NULL is the marker that this is the bootstrap button. */
        ltbp->lbp.bootstrap_button = g_new0(LaunchButton, 1);
        ltbp->lbp.bootstrap_button->plugin = p;

        /* Create an event box. */
        GtkWidget * event_box = gtk_event_box_new();
        gtk_container_set_border_width(GTK_CONTAINER(event_box), 0);
#if GLIB_CHECK_VERSION(2,18,0)
        gtk_widget_set_can_focus            (event_box, FALSE);
#else
        GTK_WIDGET_UNSET_FLAGS(event_box, GTK_CAN_FOCUS);
#endif
        ltbp->lbp.bootstrap_button->widget = event_box;
        g_signal_connect(event_box, "button-press-event", G_CALLBACK(launchbutton_press_event), ltbp->lbp.bootstrap_button);

        /* Create an image containing the stock "Add" icon as a child of the event box. */
        ltbp->lbp.bootstrap_button->image_widget = gtk_image_new_from_pixbuf(
            lxpanel_load_icon(GTK_STOCK_ADD, p->panel->icon_size, p->panel->icon_size, FALSE));
        gtk_misc_set_padding(GTK_MISC(ltbp->lbp.bootstrap_button->image_widget), 0, 0);
        gtk_misc_set_alignment(GTK_MISC(ltbp->lbp.bootstrap_button->image_widget), 0, 0);
        gtk_container_add(GTK_CONTAINER(event_box), ltbp->lbp.bootstrap_button->image_widget);

        /* Add the bootstrap button to the icon grid.  By policy it is empty at this point. */
        icon_grid_add(ltbp->lbp.icon_grid, event_box, TRUE);
    }
    else
        icon_grid_set_visible(ltbp->lbp.icon_grid, ltbp->lbp.bootstrap_button->widget, TRUE);
}

static LaunchButton *launchbar_exec_bin_exists(LaunchbarPlugin *lb, gchar *exec_bin)
{
    if(!exec_bin) return NULL;
    
    LaunchButton *ret_val = NULL;
    GSList* l;
    for(l = lb->buttons; l != NULL; l = l->next)
    {
        LaunchButton *btn = (LaunchButton *)l->data;
        if(strcmp(btn->exec_bin, exec_bin) == 0)
        {
            ret_val = btn;
            break;
        }
    }
    return ret_val;
}

static void launchbar_update_after_taskbar_class_added(LaunchTaskBarPlugin *ltbp, Task *tk)
{
    GPid   pid = get_net_wm_pid(tk->win);
    gchar  exec_bin_full[128];
    f_get_exec_cmd_from_pid(pid, exec_bin_full, "cmdline");
    gchar *p_char = strrchr(exec_bin_full, '/');
    if(p_char == NULL) p_char = exec_bin_full;
    else p_char++;
    if(strcmp(p_char, "python") == 0)
    {
        f_get_exec_cmd_from_pid(pid, exec_bin_full, "comm");
        p_char = strrchr(exec_bin_full, '/');
        if(p_char == NULL) p_char = exec_bin_full;
        else p_char++;
    }
    snprintf(tk->exec_bin, 128, "%s", p_char);

    if(ltbp->p_key_file_special_cases != NULL)
    {
        gchar *converted_tb_exec_bin = g_key_file_get_string(ltbp->p_key_file_special_cases, "special_cases", tk->exec_bin, NULL);
        if(converted_tb_exec_bin != NULL)
        {
            snprintf(tk->exec_bin, 128, "%s", converted_tb_exec_bin);
            g_free(converted_tb_exec_bin);
        }
    }

    LaunchButton *btn = launchbar_exec_bin_exists(&ltbp->lbp, tk->exec_bin);
    g_print("\nTB '%s' OPEN (pid=%u), in LB: %c\n",
        tk->exec_bin, pid, btn != NULL ? 'Y':'N');
}

static void launchbar_update_after_taskbar_class_removed(LaunchTaskBarPlugin *ltbp, Task *tk)
{
    LaunchButton *btn = launchbar_exec_bin_exists(&ltbp->lbp, tk->exec_bin);
    g_print("\nTB '%s' CLOSE, in LB: %c\n", tk->exec_bin, btn != NULL ? 'Y':'N');
}

static gboolean  load_app_key_file(gchar *filepath, GKeyFile *p_gkeyfile)
{
    gboolean  loaded;
    if(g_path_is_absolute(filepath))
    {
        loaded = g_key_file_load_from_file(p_gkeyfile, filepath, G_KEY_FILE_NONE, NULL);
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

/* Build the graphic elements for a launchtaskbar button.  The desktop_id field is already established. */
static void launchbutton_build_gui(Plugin * p, LaunchButton * btn)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

    if(btn->desktop_id != NULL)
    {
        /* There is a valid desktop file name.  Try to open it. */
        GKeyFile *p_key_desktop = g_key_file_new();
        gboolean  loaded = load_app_key_file(btn->desktop_id, p_key_desktop);
        if(loaded)
        {
            /* Desktop file located.  Get Icon, Name, Exec, and Terminal parameters. */
            gchar * icon = g_key_file_get_string(p_key_desktop, DESKTOP_ENTRY, "Icon", NULL);
            gchar * title = g_key_file_get_locale_string(p_key_desktop, DESKTOP_ENTRY, "Name", NULL, NULL);
            if ((btn->image == NULL) && (icon != NULL))
                btn->image = icon;

            gchar * exec = g_key_file_get_string(p_key_desktop, DESKTOP_ENTRY, "Exec", NULL);
            if( ! btn->customize_action )
            {
                gchar * exec = g_key_file_get_string(p_key_desktop, DESKTOP_ENTRY, "Exec", NULL);
                btn->action = translate_exec_to_cmd(exec, icon, title, btn->desktop_id);
                g_free(exec);
            }

            gchar  buffer_128[128];
            gchar *p_char = f_get_clean_exec_bin(exec, buffer_128);
            btn->exec_bin = strdup(p_char);
            g_free(exec);
            //g_print("\nLB '%s' FOUND\n", btn->exec_bin);

            btn->use_terminal = g_key_file_get_boolean(p_key_desktop, DESKTOP_ENTRY, "Terminal", NULL);

            if ( ! btn->customize_tooltip)
                btn->tooltip = title;
            if (btn->image != icon)
                g_free(icon);
            if (btn->tooltip != title)
                g_free(title);
        }
        g_key_file_free(p_key_desktop);
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
    icon_grid_add(ltbp->lbp.icon_grid, button, TRUE);

    /* Drag and drop support. */
    gtk_drag_dest_set(GTK_WIDGET(button),
        GTK_DEST_DEFAULT_ALL,
        target_table, G_N_ELEMENTS(target_table),
        GDK_ACTION_COPY);

    /* Connect signals. */
    g_signal_connect(button, "button-press-event", G_CALLBACK(launchbutton_press_event), (gpointer) btn);
    g_signal_connect(button, "drag_data_received", G_CALLBACK(launchbutton_drag_data_received_event), (gpointer) btn);

    /* If the list goes from null to non-null, remove the bootstrap button. */
    if ((ltbp->lbp.buttons == NULL) && (ltbp->lbp.bootstrap_button != NULL))
        icon_grid_set_visible(ltbp->lbp.icon_grid, ltbp->lbp.bootstrap_button->widget, FALSE);

    /* Append at end of list to preserve configured order. */
    ltbp->lbp.buttons = g_slist_append(ltbp->lbp.buttons, btn);

    /* Show the widget and return. */
    gtk_widget_show(button);
    plugin_widget_set_background(button, p->panel);
}

/* Read the configuration file entry for a launchtaskbar button and create it. */
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
                ERR( "launchtaskbar: illegal token %s\n", s.str);
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
                else if (g_ascii_strcasecmp(s.t[0], "path") == 0)
                {
                    btn->customize_path = TRUE;
                    btn->path = g_strdup(s.t[1]);
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
                    ERR( "launchtaskbar: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "launchtaskbar: illegal in this context %s\n", s.str);
                launchbutton_free(btn);
                return 0;
            }
        }
    }

    /* Build the structures and return. */
    launchbutton_build_gui(p, btn);
    return 1;
}

static void launchtaskbar_constructor_add_default_special_case(LaunchTaskBarPlugin *ltbp, const gchar *tk_exec, const gchar *mb_exec)
{
    g_key_file_set_value(ltbp->p_key_file_special_cases, "special_cases", tk_exec, mb_exec);
}

/* Plugin constructor. */
static int launchtaskbar_constructor(Plugin * p, char ** fp)
{
    gtk_rc_parse_string(launchbar_rc);
    gtk_rc_parse_string(taskbar_rc);

    /* Allocate plugin context and set into Plugin private data pointer. */
    LaunchTaskBarPlugin *ltbp = g_new0(LaunchTaskBarPlugin, 1);
    ltbp->lbp.plug = p;
    ltbp->tbp.plug = p;
    p->priv = ltbp;

    /* Initialize to defaults. */
    ltbp->tbp.icon_size         = p->panel->icon_size;
    ltbp->tbp.tooltips          = TRUE;
    ltbp->tbp.icons_only        = FALSE;
    ltbp->tbp.show_all_desks    = TRUE;
    ltbp->tbp.task_width_max    = TASK_WIDTH_MAX;
    ltbp->tbp.spacing           = 1;
    ltbp->tbp.use_mouse_wheel   = TRUE;
    ltbp->tbp.use_urgency_hint  = TRUE;
    ltbp->tbp.grouped_tasks     = FALSE;

    /* Special cases key file */
    ltbp->p_key_file_special_cases = g_key_file_new();
    gchar *special_cases_filepath = g_build_filename(g_get_user_config_dir(),
                                                     "lxpanel", "launchtaskbar.cfg", NULL);
    if(g_file_test(special_cases_filepath, G_FILE_TEST_EXISTS))
    {
        gboolean  loaded = load_app_key_file(special_cases_filepath,
                                             ltbp->p_key_file_special_cases);
        if(!loaded) ltbp->p_key_file_special_cases = NULL;
    }
    else
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
    p->pwid = p->panel->orientation == ORIENT_HORIZ ? gtk_hbox_new(FALSE, 5):gtk_vbox_new(FALSE, 5);
    ltbp->p_evbox_launchbar = gtk_event_box_new();
    ltbp->p_evbox_taskbar = gtk_event_box_new();
    gtk_box_pack_start(GTK_BOX(p->pwid), ltbp->p_evbox_launchbar, FALSE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(p->pwid), ltbp->p_evbox_taskbar, TRUE, TRUE, 0);
    
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 0);
    gtk_container_set_border_width(GTK_CONTAINER(ltbp->p_evbox_launchbar), 0);
    gtk_container_set_border_width(GTK_CONTAINER(ltbp->p_evbox_taskbar), 0);
#if GLIB_CHECK_VERSION(2,18,0)
    gtk_widget_set_has_window(p->pwid, FALSE);
#else
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);
#endif

    /* Allocate an icon grid manager to manage the container. */
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    ltbp->lbp.icon_grid = icon_grid_new(p->panel, ltbp->p_evbox_launchbar, bo, p->panel->icon_size, p->panel->icon_size, 3, 0, p->panel->height);

    /* Read parameters from the configuration file. */
    if (fp != NULL)
    {
        line s;
        s.len = 256;
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "launchtaskbar: illegal token %s\n", s.str);
                return FALSE;
            }
            if (s.type == LINE_BLOCK_START)
            {
                if (g_ascii_strcasecmp(s.t[0], "button") == 0)
                {
                    if ( ! launchbutton_constructor(p, fp))
                    {
                        ERR( "launchtaskbar: can't init button\n");
                        return FALSE;
                    }
                }
                else
                {
                    ERR( "launchtaskbar: unknown var %s\n", s.t[0]);
                    return FALSE;
                }
            }
            else if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "tooltips") == 0)
                    ltbp->tbp.tooltips = str2num(bool_pair, s.t[1], 1);
                else if (g_ascii_strcasecmp(s.t[0], "IconsOnly") == 0)
                    ltbp->tbp.icons_only = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "AcceptSkipPager") == 0) /* For backward compatibility */
                    ;
                else if (g_ascii_strcasecmp(s.t[0], "ShowIconified") == 0)   /* For backward compatibility */
                    ;
                else if (g_ascii_strcasecmp(s.t[0], "ShowMapped") == 0)      /* For backward compatibility */
                    ;
                else if (g_ascii_strcasecmp(s.t[0], "ShowAllDesks") == 0)
                    ltbp->tbp.show_all_desks = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "SameMonitorOnly") == 0)
                    ltbp->tbp.same_monitor_only = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "MaxTaskWidth") == 0)
                    ltbp->tbp.task_width_max = atoi(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "spacing") == 0)
                    ltbp->tbp.spacing = atoi(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "UseMouseWheel") == 0)
                    ltbp->tbp.use_mouse_wheel = str2num(bool_pair, s.t[1], 1);
                else if (g_ascii_strcasecmp(s.t[0], "UseUrgencyHint") == 0)
                    ltbp->tbp.use_urgency_hint = str2num(bool_pair, s.t[1], 1);
                else if (g_ascii_strcasecmp(s.t[0], "FlatButton") == 0)
                    ltbp->tbp.flat_button = str2num(bool_pair, s.t[1], 1);
                else if (g_ascii_strcasecmp(s.t[0], "GroupedTasks") == 0)
                    ltbp->tbp.grouped_tasks = str2num(bool_pair, s.t[1], 1);
                else
                    ERR( "taskbar: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "launchtaskbar: illegal in this context %s\n", s.str);
                return FALSE;
            }
        }
    }

    if (ltbp->lbp.buttons == NULL)
        launchbutton_build_bootstrap(p);

    /* Make container for task buttons as a child of top level widget. */
    ltbp->tbp.icon_grid = icon_grid_new(p->panel, ltbp->p_evbox_taskbar, bo, ltbp->tbp.task_width_max, ltbp->tbp.icon_size, ltbp->tbp.spacing, 0, p->panel->height);
    icon_grid_set_constrain_width(ltbp->tbp.icon_grid, TRUE);
    taskbar_update_style(&ltbp->tbp);

    /* Add GDK event filter. */
    gdk_window_add_filter(NULL, (GdkFilterFunc) taskbar_event_filter, &ltbp->tbp);

    /* Connect signal to receive mouse events on the unused portion of the taskbar. */
    g_signal_connect(ltbp->p_evbox_taskbar, "button-press-event", G_CALLBACK(plugin_button_press_event), p);

    /* Connect signals to receive root window events and initialize root window properties. */
    ltbp->tbp.number_of_desktops = get_net_number_of_desktops();
    ltbp->tbp.current_desktop = get_net_current_desktop();
    g_signal_connect(G_OBJECT(fbev), "current_desktop", G_CALLBACK(taskbar_net_current_desktop), (gpointer) &ltbp->tbp);
    g_signal_connect(G_OBJECT(fbev), "active_window", G_CALLBACK(taskbar_net_active_window), (gpointer) &ltbp->tbp);
    g_signal_connect(G_OBJECT(fbev), "number_of_desktops", G_CALLBACK(taskbar_net_number_of_desktops), (gpointer) &ltbp->tbp);
    g_signal_connect(G_OBJECT(fbev), "client_list", G_CALLBACK(taskbar_net_client_list), (gpointer) &ltbp->tbp);

    /* Make right-click menu for task buttons.
     * It is retained for the life of the taskbar and will be shown as needed.
     * Number of desktops and edge is needed for this operation. */
    taskbar_make_menu(&ltbp->tbp);

    /* Connect a signal to be notified when the window manager changes.  This causes re-evaluation of the "use_net_active" status. */
    g_signal_connect(gtk_widget_get_screen(ltbp->p_evbox_taskbar), "window-manager-changed", G_CALLBACK(taskbar_window_manager_changed), &ltbp->tbp);

    /* Fetch the client list and redraw the taskbar.  Then determine what window has focus. */
    taskbar_net_client_list(NULL, &ltbp->tbp);
    taskbar_net_active_window(NULL, &ltbp->tbp);
    
    return TRUE;
}

/* Plugin destructor. */
static void launchtaskbar_destructor(Plugin * p)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

    // TASKBAR
    /* Remove GDK event filter. */
    gdk_window_remove_filter(NULL, (GdkFilterFunc) taskbar_event_filter, &ltbp->tbp);

    /* Remove root window signal handlers. */
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_current_desktop, &ltbp->tbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_active_window, &ltbp->tbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_number_of_desktops, &ltbp->tbp);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_client_list, &ltbp->tbp);

    /* Remove "window-manager-changed" handler. */
    g_signal_handlers_disconnect_by_func(gtk_widget_get_screen(ltbp->p_evbox_taskbar), taskbar_window_manager_changed, &ltbp->tbp);

    /* Deallocate task list. */
    while (ltbp->tbp.p_task_list != NULL)
        task_delete(&ltbp->tbp, ltbp->tbp.p_task_list, TRUE);

    /* Deallocate class list. */
    while (ltbp->tbp.p_taskclass_list != NULL)
    {
        TaskClass * tc = ltbp->tbp.p_taskclass_list;
        ltbp->tbp.p_taskclass_list = tc->p_taskclass_flink;
        g_free(tc->res_class);
        g_free(tc);
    }

    /* Deallocate other memory. */
    gtk_widget_destroy(ltbp->tbp.menu);


    // LAUNCHBAR
    /* Free the launchtaskbar. */
    g_slist_foreach(ltbp->lbp.buttons, (GFunc) launchbutton_free, NULL);
    icon_grid_free(ltbp->lbp.icon_grid);

    /* Free the bootstrap button if it exists. */
    if (ltbp->lbp.bootstrap_button != NULL)
        g_free(ltbp->lbp.bootstrap_button);

    /* Ensure that the configuration dialog is dismissed. */
    if (ltbp->lbp.config_dlg != NULL)
        gtk_widget_destroy(ltbp->lbp.config_dlg);


    /* Deallocate all memory. */
    if(ltbp->p_key_file_special_cases != NULL) g_key_file_free(ltbp->p_key_file_special_cases);
    g_free(ltbp);
}

static LaunchButton *launchbar_add_button(LaunchTaskBarPlugin *ltbp, gchar *desktop_id)
{
    LaunchButton *defined_button = g_new0(LaunchButton, 1);
    defined_button->plugin = ltbp->lbp.plug;
    defined_button->desktop_id = g_strdup(desktop_id);
    launchbutton_build_gui(ltbp->lbp.plug, defined_button);
    return defined_button;
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Add" button. */
static void launchbar_configure_add_button(GtkButton * widget, Plugin * p)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;
    
    GtkTreeView * menu_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->lbp.config_dlg), "menu_view"));
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->lbp.config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(menu_view), &list, &it))
    {
        LaunchButton * btn;
        gtk_tree_model_get(list, &it, COL_BTN, &btn, -1);
        if( btn == NULL )
            return;

        /* We have located a selected button.
         * Add a launch button to the launchtaskbar and refresh the view in the configuration dialog. */
        LaunchButton *defined_button = launchbar_add_button(ltbp, btn->desktop_id);

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

static void  launchbar_remove_button(LaunchTaskBarPlugin *ltbp, LaunchButton *btn)
{
    icon_grid_remove(ltbp->lbp.icon_grid, btn->widget);
    ltbp->lbp.buttons = g_slist_remove(ltbp->lbp.buttons, btn);
    launchbutton_free(btn);
    /* Put the bootstrap button back if the list becomes empty. */
    if(ltbp->lbp.buttons == NULL)
        launchbutton_build_bootstrap(ltbp->lbp.plug);
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Remove" button. */
static void launchbar_configure_remove_button(GtkButton * widget, Plugin * p)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;
    
    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->lbp.config_dlg), "defined_view"));
    GtkTreeModel * list;
    GtkTreeIter it;
    if (gtk_tree_selection_get_selected(gtk_tree_view_get_selection(defined_view), &list, &it))
    {
        LaunchButton * btn;
        gtk_tree_model_get(list, &it, COL_BTN, &btn, -1);

        /* We have found a selected button.
         * Remove it from the icon grid, the data structure, and the view. */
        gtk_list_store_remove(GTK_LIST_STORE(list), &it);
        gtk_widget_set_visible(ltbp->lbp.p_label_def_app_exec, FALSE);

        launchbar_remove_button(ltbp, btn);
    }
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Move Up" button. */
static void launchbar_configure_move_up_button(GtkButton * widget, Plugin * p)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->lbp.config_dlg), "defined_view"));
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
                ltbp->lbp.buttons = g_slist_remove(ltbp->lbp.buttons, btn);
                ltbp->lbp.buttons = g_slist_insert(ltbp->lbp.buttons, btn, i);
                gtk_list_store_move_before(GTK_LIST_STORE(list), &it, &it2);
                icon_grid_reorder_child(ltbp->lbp.icon_grid, btn->widget, i);
            }
        }
        gtk_tree_path_free(path);
    }
}

/* Handler for "clicked" action on launchtaskbar configuration dialog "Move Down" button. */
static void launchbar_configure_move_down_button(GtkButton * widget, Plugin * p)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

    GtkTreeView * defined_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->lbp.config_dlg), "defined_view"));
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
                ltbp->lbp.buttons = g_slist_remove(ltbp->lbp.buttons, btn);
                ltbp->lbp.buttons = g_slist_insert(ltbp->lbp.buttons, btn, i + 1);
                gtk_list_store_move_after(GTK_LIST_STORE(list), &it, &it2);
                icon_grid_reorder_child( ltbp->lbp.icon_grid, btn->widget, i);
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

/* Handler for "response" signal from launchtaskbar configuration dialog. */
static void launchbar_configure_response(GtkDialog * dlg, int response, Plugin * p)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

    /* Deallocate LaunchButtons that were loaded from the menu. */
    GtkTreeView * menu_view = GTK_TREE_VIEW(g_object_get_data(G_OBJECT(ltbp->lbp.config_dlg), "menu_view"));
    GtkTreeModel * model = gtk_tree_view_get_model(menu_view);
    launchbar_configure_free_btns_in_model(model, NULL);

    /* Deallocate the configuration dialog. */
    ltbp->lbp.config_dlg = NULL;
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
                btn->path = g_strdup(menu_cache_app_get_working_dir(MENU_CACHE_APP(item)));

                GKeyFile *p_key_desktop = g_key_file_new();
                gboolean loaded = load_app_key_file(btn->desktop_id, p_key_desktop);
                btn->action = loaded ? g_key_file_get_string(p_key_desktop, DESKTOP_ENTRY, "Exec", NULL) : NULL;
                g_key_file_free(p_key_desktop);

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

static void on_menu_cache_reload(MenuCache* menu_cache, gpointer tree)
{
    MenuCacheDir * dir = menu_cache_get_root_dir(menu_cache);
    gtk_tree_store_clear(tree);
    if(dir)
        launchbar_configure_add_menu_recursive(tree, NULL, dir);
}

/* Initialize the list of existing launchtaskbar buttons when the configuration dialog is shown. */
static void launchbar_configure_initialize_list(Plugin * p, GtkWidget * dlg, GtkTreeView * view, gboolean from_menu)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

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
            gpointer id = menu_cache_add_reload_notify(menu_cache, on_menu_cache_reload, tree);
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

        /* Initialize from defined launchtaskbar buttons. */
        GSList* l;
        for (l = ltbp->lbp.buttons; l != NULL; l = l->next)
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

static void on_checkbutton_show_tooltips_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->tooltips = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->tooltips upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_checkbutton_icons_only_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->icons_only = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->icons_only upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_checkbutton_flat_buttons_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->flat_button = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->flat_button upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_checkbutton_show_all_desks_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->show_all_desks = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->show_all_desks upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_checkbutton_same_monitor_only_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->same_monitor_only = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->same_monitor_only upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_checkbutton_mouse_wheel_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->use_mouse_wheel = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->use_mouse_wheel upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_checkbutton_urgency_hint_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->use_urgency_hint = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->use_urgency_hint upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_checkbutton_grouped_tasks_toggled(GtkToggleButton *p_togglebutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->grouped_tasks = gtk_toggle_button_get_active(p_togglebutton);
    //g_print("\ntb->grouped_tasks upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_spinbutton_max_width_value_changed(GtkSpinButton *p_spinbutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->task_width_max = gtk_spin_button_get_value(p_spinbutton);
    //g_print("\ntb->task_width_max upd\n");
    taskbar_apply_configuration(tb->plug);
}

static void on_spinbutton_spacing_value_changed(GtkSpinButton *p_spinbutton, gpointer p_data)
{
    TaskbarPlugin *tb = (TaskbarPlugin *)p_data;
    tb->spacing = gtk_spin_button_get_value(p_spinbutton);
    //g_print("\ntb->spacing upd\n");
    taskbar_apply_configuration(tb->plug);
}

static gboolean on_defined_view_button_press_event(GtkWidget *p_widget, GdkEventButton *p_event, gpointer p_data)
{
    LaunchbarPlugin *lb = (LaunchbarPlugin *)p_data;
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
    LaunchbarPlugin *lb = (LaunchbarPlugin *)p_data;
    if(p_event->button == 1)
    {
        if(p_event->type == GDK_2BUTTON_PRESS)
        {
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

/* Callback when the configuration dialog is to be shown. */
static void launchtaskbar_configure(Plugin * p, GtkWindow * parent)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

    if (ltbp->lbp.config_dlg == NULL)
    {
        GtkWidget *dlg, *btn, *defined_view, *menu_view;
        GtkBuilder *builder = gtk_builder_new();

        gtk_builder_add_from_file(builder, PACKAGE_UI_DIR "/launchtaskbar.ui", NULL);
        dlg = (GtkWidget *)gtk_builder_get_object(builder, "dlg");
        panel_apply_icon(GTK_WINDOW(dlg));

        defined_view = (GtkWidget *)gtk_builder_get_object(builder, "defined_view");
        menu_view = (GtkWidget *)gtk_builder_get_object(builder, "menu_view");
        ltbp->lbp.p_label_def_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_def_app_exec");
        ltbp->lbp.p_label_menu_app_exec = (GtkWidget*)gtk_builder_get_object(builder, "label_menu_app_exec");

        /* Connect signals. */
        g_signal_connect(dlg, "response", G_CALLBACK(launchbar_configure_response), p);

        ltbp->lbp.p_button_add = (GtkWidget *)gtk_builder_get_object(builder, "button_add");
        g_signal_connect(ltbp->lbp.p_button_add, "clicked", G_CALLBACK(launchbar_configure_add_button), p);

        ltbp->lbp.p_button_remove = (GtkWidget *)gtk_builder_get_object(builder, "button_remove");
        g_signal_connect(ltbp->lbp.p_button_remove, "clicked", G_CALLBACK(launchbar_configure_remove_button), p);

        btn = (GtkWidget *)gtk_builder_get_object(builder, "button_up");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_up_button), p);

        btn = (GtkWidget *)gtk_builder_get_object(builder, "button_down");
        g_signal_connect(btn, "clicked", G_CALLBACK(launchbar_configure_move_down_button), p);

        g_signal_connect(defined_view, "button-press-event", G_CALLBACK(on_defined_view_button_press_event), &ltbp->lbp);
        g_signal_connect(defined_view, "cursor-changed", G_CALLBACK(on_defined_view_cursor_changed), &ltbp->lbp);
        g_signal_connect(menu_view, "button-press-event", G_CALLBACK(on_menu_view_button_press_event), &ltbp->lbp);
        g_signal_connect(menu_view, "cursor-changed", G_CALLBACK(on_menu_view_cursor_changed), &ltbp->lbp);

        GtkWidget *p_checkbutton_show_tooltips, *p_checkbutton_icons_only, *p_checkbutton_flat_buttons;
        GtkWidget *p_checkbutton_show_all_desks, *p_checkbutton_same_monitor_only;
        GtkWidget *p_checkbutton_mouse_wheel, *p_checkbutton_urgency_hint;
        GtkWidget *p_checkbutton_grouped_tasks, *p_spinbutton_max_width, *p_spinbutton_spacing;
        
        p_checkbutton_show_tooltips = (GtkWidget *)gtk_builder_get_object(builder, "checkbutton_show_tooltips");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p_checkbutton_show_tooltips), ltbp->tbp.tooltips);
        g_signal_connect(GTK_TOGGLE_BUTTON(p_checkbutton_show_tooltips), "toggled",
                         G_CALLBACK(on_checkbutton_show_tooltips_toggled), &ltbp->tbp);
        
        p_checkbutton_icons_only = (GtkWidget *)gtk_builder_get_object(builder, "checkbutton_icons_only");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p_checkbutton_icons_only), ltbp->tbp.icons_only);
        g_signal_connect(GTK_TOGGLE_BUTTON(p_checkbutton_icons_only), "toggled",
                         G_CALLBACK(on_checkbutton_icons_only_toggled), &ltbp->tbp);
        
        p_checkbutton_flat_buttons = (GtkWidget *)gtk_builder_get_object(builder, "checkbutton_flat_buttons");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p_checkbutton_flat_buttons), ltbp->tbp.flat_button);
        g_signal_connect(GTK_TOGGLE_BUTTON(p_checkbutton_flat_buttons), "toggled",
                         G_CALLBACK(on_checkbutton_flat_buttons_toggled), &ltbp->tbp);
        
        p_checkbutton_show_all_desks = (GtkWidget *)gtk_builder_get_object(builder, "checkbutton_show_all_desks");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p_checkbutton_show_all_desks), ltbp->tbp.show_all_desks);
        g_signal_connect(GTK_TOGGLE_BUTTON(p_checkbutton_show_all_desks), "toggled",
                         G_CALLBACK(on_checkbutton_show_all_desks_toggled), &ltbp->tbp);
        
        p_checkbutton_same_monitor_only = (GtkWidget *)gtk_builder_get_object(builder, "checkbutton_same_monitor_only");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p_checkbutton_same_monitor_only), ltbp->tbp.same_monitor_only);
        g_signal_connect(GTK_TOGGLE_BUTTON(p_checkbutton_same_monitor_only), "toggled",
                         G_CALLBACK(on_checkbutton_same_monitor_only_toggled), &ltbp->tbp);
        
        p_checkbutton_mouse_wheel = (GtkWidget *)gtk_builder_get_object(builder, "checkbutton_mouse_wheel");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p_checkbutton_mouse_wheel), ltbp->tbp.use_mouse_wheel);
        g_signal_connect(GTK_TOGGLE_BUTTON(p_checkbutton_mouse_wheel), "toggled",
                         G_CALLBACK(on_checkbutton_mouse_wheel_toggled), &ltbp->tbp);
        
        p_checkbutton_urgency_hint = (GtkWidget *)gtk_builder_get_object(builder, "checkbutton_urgency_hint");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p_checkbutton_urgency_hint), ltbp->tbp.use_urgency_hint);
        g_signal_connect(GTK_TOGGLE_BUTTON(p_checkbutton_urgency_hint), "toggled",
                         G_CALLBACK(on_checkbutton_urgency_hint_toggled), &ltbp->tbp);
        
        p_checkbutton_grouped_tasks = (GtkWidget *)gtk_builder_get_object(builder, "checkbutton_grouped_tasks");
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(p_checkbutton_grouped_tasks), ltbp->tbp.grouped_tasks);
        g_signal_connect(GTK_TOGGLE_BUTTON(p_checkbutton_grouped_tasks), "toggled",
                         G_CALLBACK(on_checkbutton_grouped_tasks_toggled), &ltbp->tbp);
        
        p_spinbutton_max_width = (GtkWidget *)gtk_builder_get_object(builder, "spinbutton_max_width");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(p_spinbutton_max_width), ltbp->tbp.task_width_max);
        g_signal_connect(GTK_SPIN_BUTTON(p_spinbutton_max_width), "value-changed",
                         G_CALLBACK(on_spinbutton_max_width_value_changed), &ltbp->tbp);
        
        p_spinbutton_spacing = (GtkWidget *)gtk_builder_get_object(builder, "spinbutton_spacing");
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(p_spinbutton_spacing), ltbp->tbp.spacing);
        g_signal_connect(GTK_SPIN_BUTTON(p_spinbutton_spacing), "value-changed",
                         G_CALLBACK(on_spinbutton_spacing_value_changed), &ltbp->tbp);

        gtk_window_present(GTK_WINDOW(dlg));
        ltbp->lbp.config_dlg = dlg;

        /* Establish a callback when the dialog completes. */
        g_object_weak_ref(G_OBJECT(dlg), (GWeakNotify) panel_config_save, p->panel);

        /* Initialize the tree view contents. */
        launchbar_configure_initialize_list(p, dlg, GTK_TREE_VIEW(defined_view), FALSE);
        launchbar_configure_initialize_list(p, dlg, GTK_TREE_VIEW(menu_view), TRUE);

        gtk_widget_set_visible(ltbp->lbp.p_label_menu_app_exec, FALSE);
        gtk_widget_set_visible(ltbp->lbp.p_label_def_app_exec, FALSE);

        g_object_unref(builder);
        return;
    }
}

/* Callback when the configuration is to be saved. */
static void launchtaskbar_save_configuration(Plugin * p, FILE * fp)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;
    
    GSList * l;
    for (l = ltbp->lbp.buttons; l != NULL; l = l->next)
    {
        LaunchButton * btn = (LaunchButton *) l->data;
        lxpanel_put_line(fp, "Button {");
        if (btn->desktop_id != NULL)
            lxpanel_put_str(fp, "id", btn->desktop_id);
        if (btn->customize_image)
            lxpanel_put_str(fp, "image", btn->image);
        if(btn->customize_tooltip)
            lxpanel_put_str(fp, "tooltip", btn->tooltip);
        if(btn->customize_path)
            lxpanel_put_str(fp, "path", btn->path);
        if (btn->customize_action)
            lxpanel_put_str(fp, "action", btn->action);
        if (btn->use_terminal)
            lxpanel_put_bool(fp, "terminal", TRUE);
        lxpanel_put_line(fp, "}");
    }
    
    lxpanel_put_bool(fp, "tooltips", ltbp->tbp.tooltips);
    lxpanel_put_bool(fp, "IconsOnly", ltbp->tbp.icons_only);
    lxpanel_put_bool(fp, "ShowAllDesks", ltbp->tbp.show_all_desks);
    lxpanel_put_bool(fp, "SameMonitorOnly", ltbp->tbp.same_monitor_only);
    lxpanel_put_bool(fp, "UseMouseWheel", ltbp->tbp.use_mouse_wheel);
    lxpanel_put_bool(fp, "UseUrgencyHint", ltbp->tbp.use_urgency_hint);
    lxpanel_put_bool(fp, "FlatButton", ltbp->tbp.flat_button);
    lxpanel_put_int(fp, "MaxTaskWidth", ltbp->tbp.task_width_max);
    lxpanel_put_int(fp, "spacing", ltbp->tbp.spacing);
    lxpanel_put_bool(fp, "GroupedTasks", ltbp->tbp.grouped_tasks);
}

/* Callback when panel configuration changes. */
static void launchtaskbar_panel_configuration_changed(Plugin * p)
{
    /* Set orientation into the icon grid. */
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;
    
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    icon_grid_set_geometry(ltbp->lbp.icon_grid, bo,
        p->panel->icon_size, p->panel->icon_size, 3, 0, p->panel->height);

    /* Reset all the images to resize them. */
    GSList * l;
    for (l = ltbp->lbp.buttons; l != NULL; l = l->next)
    {
        LaunchButton * btn = (LaunchButton *) l->data;
        fb_button_set_from_file(btn->widget, btn->image, p->panel->icon_size, p->panel->icon_size, TRUE);
    }

    /* Reset the bootstrap button. */
    if (ltbp->lbp.bootstrap_button != NULL)
        gtk_image_set_from_pixbuf(GTK_IMAGE(ltbp->lbp.bootstrap_button->image_widget),
            lxpanel_load_icon(GTK_STOCK_ADD, p->panel->icon_size, p->panel->icon_size, FALSE));
    
    taskbar_update_style(&ltbp->tbp);
    taskbar_make_menu(&ltbp->tbp);

    /* If the icon size changed, refetch all the icons. */
    if (p->panel->icon_size != ltbp->tbp.icon_size)
    {
        ltbp->tbp.icon_size = p->panel->icon_size;
        Task * tk;
        for (tk = ltbp->tbp.p_task_list; tk != NULL; tk = tk->p_task_flink_xwid)
        {
            GdkPixbuf * pixbuf = task_update_icon(&ltbp->tbp, tk, None);
            if (pixbuf != NULL)
            {
                gtk_image_set_from_pixbuf(GTK_IMAGE(tk->image), pixbuf);
                g_object_unref(pixbuf);
            }
        }
    }

    /* Redraw all the labels.  Icon size or font color may have changed. */
    taskbar_redraw(&ltbp->tbp);
}

/* Set an urgency timer on a task. */
static void set_timer_on_task(Task * tk)
{
    gint interval;
    g_return_if_fail(tk->flash_timeout == 0);
    g_object_get(gtk_widget_get_settings(tk->button), "gtk-cursor-blink-time", &interval, NULL);
    tk->flash_timeout = g_timeout_add(interval, (GSourceFunc) flash_window_timeout, tk);
}

/* Determine if a task is visible considering only its desktop placement. */
static gboolean task_is_visible_on_current_desktop(TaskbarPlugin * tb, Task * tk)
{
    return ((tk->desktop == ALL_WORKSPACES) || (tk->desktop == tb->current_desktop) || (tb->show_all_desks));
}

/* Recompute the visible task for a class when the class membership changes.
 * Also transfer the urgency state to the visible task if necessary. */
static void recompute_group_visibility_for_class(TaskbarPlugin * tb, TaskClass * tc)
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
            if (tk->urgency)
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
            flash_window_timeout(tc->p_task_visible);

            /* Set the timer, since none is set. */
            set_timer_on_task(tc->p_task_visible);
        }
        else if (flashing_task != tc->p_task_visible)
        {
            /* Reset the timer on the new representative.
             * There will be a slight hiccup on the flash cadence. */
            g_source_remove(flashing_task->flash_timeout);
            flashing_task->flash_timeout = 0;
            tc->p_task_visible->flash_state = flashing_task->flash_state;
            flashing_task->flash_state = FALSE;
            set_timer_on_task(tc->p_task_visible);
        }   
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
static void recompute_group_visibility_on_current_desktop(TaskbarPlugin * tb)
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

    panel_draw_label_text(tk->tb->plug->panel, tk->label, label, bold_style, 1,
            tk->tb->flat_button);

    g_free(label);
}

/* Determine if a task is visible. */
static gboolean task_is_visible(TaskbarPlugin * tb, Task * tk)
{
    /* Not visible due to grouping. */
    if ((tb->grouped_tasks) && (tk->p_taskclass != NULL) && (tk->p_taskclass->p_task_visible != tk))
        return FALSE;

    /* Not on same monitor */
    if (tb->same_monitor_only && tb->plug->panel->monitor != tk->monitor)
        return FALSE;

    /* Desktop placement. */
    return task_is_visible_on_current_desktop(tb, tk);
}

/* Redraw a task button. */
static void task_button_redraw(Task * tk, TaskbarPlugin * tb)
{
    if (task_is_visible(tb, tk))
    {
        task_draw_label(tk);
        icon_grid_set_visible(tb->icon_grid, tk->button, TRUE);
    }
    else
        icon_grid_set_visible(tb->icon_grid, tk->button, FALSE);
}

/* Redraw all tasks in the taskbar. */
static void taskbar_redraw(TaskbarPlugin * tb)
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
        tk->name = g_strdup(name);
        tk->name_iconified = g_strdup_printf("[%s]", name);
        g_free(name);

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
        /* Action in Launchbar after class removed */
        launchbar_update_after_taskbar_class_removed(tk->tb->plug->priv, tk);
        
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

        /* Recompute group visibility. */
        recompute_group_visibility_for_class(tk->tb, tc);
    }
}

/* Enter class with specified name. */
static TaskClass * taskbar_enter_res_class(TaskbarPlugin * tb, char * res_class, gboolean * p_name_consumed)
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
    XGetClassHint(GDK_DISPLAY(), tk->win, &ch);

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
                launchbar_update_after_taskbar_class_added(tk->tb->plug->priv, tk);
            }
        }
        XFree(ch.res_class);
    }
}

/* Look up a task in the task list. */
static Task * task_lookup(TaskbarPlugin * tb, Window win)
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
static void task_delete(TaskbarPlugin * tb, Task * tk, gboolean unlink)
{
    /* If we think this task had focus, remove that. */
    if (tb->focused == tk)
        tb->focused = NULL;

    /* If there is an urgency timeout, remove it. */
    if (tk->flash_timeout != 0) {
        g_source_remove(tk->flash_timeout);
        tk->flash_timeout = 0;
    }

    /* Deallocate structures. */
    icon_grid_remove(tb->icon_grid, tk->button);
    task_free_names(tk);
    task_unlink_class(tk);

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
static GdkPixbuf * _wnck_gdk_pixbuf_get_from_pixmap(Pixmap xpixmap, int width, int height)
{
    /* Get the drawable. */
    GdkDrawable * drawable = gdk_xid_table_lookup(xpixmap);
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
            colormap = gdk_screen_get_system_colormap(gdk_drawable_get_screen(drawable));
            g_object_ref(G_OBJECT(colormap));
        }

        /* Be sure we aren't going to fail due to visual mismatch. */
#if GTK_CHECK_VERSION(2,22,0)
        if ((colormap != NULL) && (gdk_visual_get_depth(gdk_colormap_get_visual(colormap)) != depth))
#else
        if ((colormap != NULL) && (gdk_colormap_get_visual(colormap)->depth != depth))
#endif
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
static GdkPixbuf * get_wm_icon(Window task_win, int required_width, int required_height, Atom source, Atom * current_source)
{
    /* The result. */
    GdkPixbuf * pixmap = NULL;
    Atom possible_source = None;
    int result = -1;

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
            GDK_DISPLAY(),
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
                gulong w = pdata[0];
                gulong h = pdata[1];
                gulong size = w * h;
                pdata += 2;

                /* Bounds check the icon. */
                if (pdata + size > pdata_end)
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
                int i;
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
        XWMHints * hints = XGetWMHints(GDK_DISPLAY(), task_win);
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
                GDK_DISPLAY(),
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
                GDK_DISPLAY(), xpixmap,
                &unused_win, &unused, &unused, &w, &h, &unused_2, &unused_2) ? Success : -1;
        }

        /* If we have an X pixmap and its geometry, convert it to a GDK pixmap. */
        if (result == Success) 
        {
            pixmap = _wnck_gdk_pixbuf_get_from_pixmap(xpixmap, w, h);
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
                GDK_DISPLAY(), xmask,
                &unused_win, &unused, &unused, &w, &h, &unused_2, &unused_2))
            {
                /* Convert the X mask to a GDK pixmap. */
                GdkPixbuf * mask = _wnck_gdk_pixbuf_get_from_pixmap(xmask, w, h);
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
        GdkPixbuf * ret = gdk_pixbuf_scale_simple(pixmap, required_width, required_height, GDK_INTERP_TILES);
        g_object_unref(pixmap);
        *current_source = possible_source;
        return ret;
    }
}

/* Update the icon of a task. */
static GdkPixbuf * task_update_icon(TaskbarPlugin * tb, Task * tk, Atom source)
{
    /* Get the icon from the window's hints. */
    GdkPixbuf * pixbuf = get_wm_icon(tk->win, tb->icon_size - ICON_BUTTON_TRIM, tb->icon_size - ICON_BUTTON_TRIM, source, &tk->image_source);

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
static gboolean flash_window_timeout(Task * tk)
{
    /* Set state on the button and redraw. */
    if ( ! tk->tb->flat_button)
        gtk_widget_set_state(tk->button, tk->flash_state ? GTK_STATE_SELECTED : GTK_STATE_NORMAL);
    task_draw_label(tk);

    /* Complement the flashing context. */
    tk->flash_state = ! tk->flash_state;
    return TRUE;
}

/* Set urgency notification. */
static void task_set_urgency(Task * tk)
{
    TaskbarPlugin * tb = tk->tb;
    TaskClass * tc = tk->p_taskclass;
    if ((tb->grouped_tasks) && (tc != NULL) && (tc->visible_count > 1))
        recompute_group_visibility_for_class(tk->tb, tk->p_taskclass);
    else
    {
        /* Set the flashing context and flash the window immediately. */
        tk->flash_state = TRUE;
        flash_window_timeout(tk);

        /* Set the timer if none is set. */
        if (tk->flash_timeout == 0)
            set_timer_on_task(tk);
    }
}

/* Clear urgency notification. */
static void task_clear_urgency(Task * tk)
{
    TaskbarPlugin * tb = tk->tb;
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

        /* Clear the flashing context and unflash the window immediately. */
        tk->flash_state = FALSE;
        flash_window_timeout(tk);
        tk->flash_state = FALSE;
    }
}

/* Do the proper steps to raise a window.
 * This means removing it from iconified state and bringing it to the front.
 * We also switch the active desktop and viewport if needed. */
static void task_raise_window(Task * tk, guint32 time)
{
    /* Change desktop if needed. */
    if ((tk->desktop != ALL_WORKSPACES) && (tk->desktop != tk->tb->current_desktop))
        Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, tk->desktop, 0, 0, 0, 0);

    /* Evaluate use_net_active if not yet done. */
    if ( ! tk->tb->net_active_checked)
    {
        TaskbarPlugin * tb = tk->tb;
        GdkAtom net_active_atom = gdk_x11_xatom_to_atom(a_NET_ACTIVE_WINDOW);
        tb->use_net_active = gdk_x11_screen_supports_net_wm_hint(gtk_widget_get_screen(tb->plug->pwid), net_active_atom);
        tb->net_active_checked = TRUE;
    }

    /* Raise the window.  We can use NET_ACTIVE_WINDOW if the window manager supports it.
     * Otherwise, do it the old way with XMapRaised and XSetInputFocus. */
    if (tk->tb->use_net_active)
        Xclimsg(tk->win, a_NET_ACTIVE_WINDOW, 2, time, 0, 0, 0);
    else
    {
        GdkWindow * gdkwindow = gdk_xid_table_lookup(tk->win);
        if (gdkwindow != NULL)
            gdk_window_show(gdkwindow);
        else
            XMapRaised(GDK_DISPLAY(), tk->win);

    /* There is a race condition between the X server actually executing the XMapRaised and this code executing XSetInputFocus.
     * If the window is not viewable, the XSetInputFocus will fail with BadMatch. */
    XWindowAttributes attr;
    XGetWindowAttributes(GDK_DISPLAY(), tk->win, &attr);
    if (attr.map_state == IsViewable)
            XSetInputFocus(GDK_DISPLAY(), tk->win, RevertToNone, time);
    }

    /* Change viewport if needed. */
    XWindowAttributes xwa;
    XGetWindowAttributes(GDK_DISPLAY(), tk->win, &xwa);
    Xclimsg(tk->win, a_NET_DESKTOP_VIEWPORT, xwa.x, xwa.y, 0, 0, 0);
}

/* Position-calculation callback for grouped-task and window-management popup menu. */
static void taskbar_popup_set_position(GtkWidget * menu, gint * px, gint * py, gboolean * push_in, gpointer data)
{
    Task * tk = (Task *) data;

    /* Get the allocation of the popup menu. */
    GtkRequisition popup_req;
    gtk_widget_size_request(menu, &popup_req);

    /* Determine the coordinates. */
    plugin_popup_set_position_helper(tk->tb->plug, tk->button, menu, &popup_req, px, py);
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
static void task_group_menu_destroy(TaskbarPlugin * tb)
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
    TaskbarPlugin * tb = tk->tb;
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
                    g_signal_connect(mi, "button_press_event",
                            G_CALLBACK(taskbar_popup_activate_event), (gpointer) tk_cursor);
                    gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
                }
            }
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
            /* Left button.
             * If the task is iconified, raise it.
             * If the task is not iconified and has focus, iconify it.
             * If the task is not iconified and does not have focus, raise it. */
            if (tk->iconified)
                task_raise_window(tk, event->time);
            else if ((tk->focused) || (tk == tb->focused_previous))
                XIconifyWindow(GDK_DISPLAY(), tk->win, DefaultScreen(GDK_DISPLAY()));
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
        else if (event->button == 3)
        {
            /* Right button.  Bring up the window state popup menu. */
            tk->tb->menutask = tk;
            LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)tk->tb->plug->priv;
            LaunchButton *btn = launchbar_exec_bin_exists(&ltbp->lbp, tk->exec_bin);
            //g_print("\nTB '%s' right-click, in LB: %c\n", tk->exec_bin, btn != NULL ? 'Y':'N');
            if(btn != NULL)
            {
                gtk_widget_set_visible(ltbp->tbp.p_menuitem_lock_tbp, FALSE);
                gtk_widget_set_visible(ltbp->tbp.p_menuitem_unlock_tbp, TRUE);
                gtk_widget_set_visible(ltbp->tbp.p_menuitem_new_instance, TRUE);
            }
            else
            {
                ltbp->found_mb = FALSE;
                guint32  flags;
                MenuCache *menu_cache = panel_menu_cache_new(&flags);
                if(menu_cache != NULL)
                {
                    MenuCacheDir *dir = menu_cache_get_root_dir(menu_cache);
                    if(dir)
                    {
                        ltbp->add_mb_to_lb = FALSE;
                        ltbp->execute_mb = FALSE;
                        ltbp->exec_bin_mb = tb->menutask->exec_bin;
                        f_find_menu_launchbutton_recursive(dir, ltbp);
                    }
                }
                gtk_widget_set_visible(ltbp->tbp.p_menuitem_lock_tbp, ltbp->found_mb);
                gtk_widget_set_visible(ltbp->tbp.p_menuitem_unlock_tbp, FALSE);
                gtk_widget_set_visible(ltbp->tbp.p_menuitem_new_instance, ltbp->found_mb);
            }
            gtk_menu_popup(
                GTK_MENU(tb->menu),
                NULL, NULL,
                (GtkMenuPositionFunc) taskbar_popup_set_position, (gpointer) visible_task,
                event->button, event->time);
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
    return taskbar_task_control_event(widget, event, tk, FALSE);
}

/* Handler for "activate" event from grouped-task popup menu item. */
static gboolean taskbar_popup_activate_event(GtkWidget * widget, GdkEventButton * event, Task * tk)
{
    return taskbar_task_control_event(widget, event, tk, TRUE);
}

/* Handler for "drag-motion" timeout. */
static gboolean taskbar_button_drag_motion_timeout(Task * tk)
{
    guint time = gtk_get_current_event_time();
    task_raise_window(tk, ((time != 0) ? time : CurrentTime));
    tk->tb->dnd_delay_timer = 0;
    return FALSE;
}

/* Handler for "drag-motion" event from taskbar button. */
static gboolean taskbar_button_drag_motion(GtkWidget * widget, GdkDragContext * drag_context, gint x, gint y, guint time, Task * tk)
{
    /* Prevent excessive motion notification. */
    if (tk->tb->dnd_delay_timer == 0)
        tk->tb->dnd_delay_timer = g_timeout_add(DRAG_ACTIVE_DELAY, (GSourceFunc) taskbar_button_drag_motion_timeout, tk);
    gdk_drag_status(drag_context, 0, time);
    return TRUE;
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
    TaskbarPlugin * tb = tk->tb;
    TaskClass * tc = tk->p_taskclass;
    if ((tb->use_mouse_wheel)
    && (( ! tb->grouped_tasks) || (tc == NULL) || (tc->visible_count == 1)))
    {
        if ((event->direction == GDK_SCROLL_UP) || (event->direction == GDK_SCROLL_LEFT))
            task_raise_window(tk, event->time);
        else
            XIconifyWindow(GDK_DISPLAY(), tk->win, DefaultScreen(GDK_DISPLAY()));
    }
    return TRUE;
}

/* Handler for "size-allocate" event from taskbar button. */
static void taskbar_button_size_allocate(GtkWidget * btn, GtkAllocation * alloc, Task * tk)
{
    if (GTK_WIDGET_REALIZED(btn))
    {
        /* Get the coordinates of the button. */
        int x, y;
#if GTK_CHECK_VERSION(2,22,0)
        gdk_window_get_origin(gtk_button_get_event_window(GTK_BUTTON(btn)), &x, &y);
#else
        gdk_window_get_origin(GTK_BUTTON(btn)->event_window, &x, &y);
#endif


        /* Send a NET_WM_ICON_GEOMETRY property change on the window. */
        guint32 data[4];
        data[0] = x;
        data[1] = y;
        data[2] = alloc->width;
        data[3] = alloc->height;
        XChangeProperty(GDK_DISPLAY(), tk->win,
            gdk_x11_get_xatom_by_name("_NET_WM_ICON_GEOMETRY"),
            XA_CARDINAL, 32, PropModeReplace, (guchar *) &data, 4);
    }
}

/* Update style on the taskbar when created or after a configuration change. */
static void taskbar_update_style(TaskbarPlugin * tb)
{
    GtkOrientation bo = (tb->plug->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    icon_grid_set_geometry(tb->icon_grid, bo,
        ((tb->icons_only) ? tb->icon_size + ICON_ONLY_EXTRA : tb->task_width_max),
        tb->icon_size, tb->spacing, 0, tb->plug->panel->height);
}

/* Update style on a task button when created or after a configuration change. */
static void task_update_style(Task * tk, TaskbarPlugin * tb)
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

/* Build graphic elements needed for a task button. */
static void task_build_gui(TaskbarPlugin * tb, Task * tk)
{
    /* NOTE
     * 1. the extended mask is sum of taskbar and pager needs
     * see bug [ 940441 ] pager loose track of windows
     *
     * Do not change event mask to gtk windows spawned by this gtk client
     * this breaks gtk internals */
    if ( ! FBPANEL_WIN(tk->win))
        XSelectInput(GDK_DISPLAY(), tk->win, PropertyChangeMask | StructureNotifyMask);

    /* Allocate a toggle button as the top level widget. */
    tk->button = gtk_toggle_button_new();
    gtk_container_set_border_width(GTK_CONTAINER(tk->button), 0);
    gtk_drag_dest_set(tk->button, 0, NULL, 0, 0);

    /* Connect signals to the button. */
    g_signal_connect(tk->button, "button-press-event", G_CALLBACK(taskbar_button_press_event), (gpointer) tk);
    g_signal_connect(G_OBJECT(tk->button), "drag-motion", G_CALLBACK(taskbar_button_drag_motion), (gpointer) tk);
    g_signal_connect(G_OBJECT(tk->button), "drag-leave", G_CALLBACK(taskbar_button_drag_leave), (gpointer) tk);
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
    gtk_widget_show(container);
    gtk_container_add(GTK_CONTAINER(tk->button), container);
    gtk_container_set_border_width(GTK_CONTAINER(tk->button), 0);

    /* Add the button to the taskbar. */
    icon_grid_add(tb->icon_grid, tk->button, TRUE);
#if GTK_CHECK_VERSION(2,18,0)
    gtk_widget_set_can_focus(GTK_WIDGET(tk->button),FALSE);
    gtk_widget_set_can_default(GTK_WIDGET(tk->button),FALSE);
#else
    GTK_WIDGET_UNSET_FLAGS(tk->button, GTK_CAN_FOCUS);
    GTK_WIDGET_UNSET_FLAGS(tk->button, GTK_CAN_DEFAULT);
#endif

    /* Update styles on the button. */
    task_update_style(tk, tb);

    /* Flash button for window with urgency hint. */
    if (tk->urgency)
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
    gdk_window_unref(gwin);
    return m;
}

/*****************************************************
 * handlers for NET actions                          *
 *****************************************************/

/* Handler for "client-list" event from root window listener. */
static void taskbar_net_client_list(GtkWidget * widget, TaskbarPlugin * tb)
{
    /* Get the NET_CLIENT_LIST property. */
    int client_count;
    Window * client_list = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST, XA_WINDOW, &client_count);
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
    Task * tk = tb->p_task_list;
    while (tk != NULL)
    {
        Task * tk_succ = tk->p_task_flink_xwid;
        if (tk->present_in_client_list)
        {
            tk->present_in_client_list = FALSE;
            tk_pred = tk;
        }
        else
        {
            if (tk_pred == NULL)
                tb->p_task_list = tk_succ;
                else tk_pred->p_task_flink_xwid = tk_succ;
            task_delete(tb, tk, FALSE);
        }
        tk = tk_succ;
    }

    /* Redraw the taskbar. */
    taskbar_redraw(tb);
}

/* Handler for "current-desktop" event from root window listener. */
static void taskbar_net_current_desktop(GtkWidget * widget, TaskbarPlugin * tb)
{
    /* Store the local copy of current desktops.  Redisplay the taskbar. */
    tb->current_desktop = get_net_current_desktop();
    recompute_group_visibility_on_current_desktop(tb);
    taskbar_redraw(tb);
}

/* Handler for "number-of-desktops" event from root window listener. */
static void taskbar_net_number_of_desktops(GtkWidget * widget, TaskbarPlugin * tb)
{
    /* Store the local copy of number of desktops.  Recompute the popup menu and redisplay the taskbar. */
    tb->number_of_desktops = get_net_number_of_desktops();
    taskbar_make_menu(tb);
    taskbar_redraw(tb);
}

/* Handler for "active-window" event from root window listener. */
static void taskbar_net_active_window(GtkWidget * widget, TaskbarPlugin * tb)
{
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
        if (*f == tb->plug->panel->topxwin)
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
static void taskbar_property_notify_event(TaskbarPlugin *tb, XEvent *ev)
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
                        if (tk->urgency)
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
                        task_delete(tb, tk, TRUE);
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
                        task_delete(tb, tk, TRUE);
                        taskbar_redraw(tb);
                    }
                }
                XSetErrorHandler(previous_error_handler);
            }
        }
    }
}

/* Handle ConfigureNotify events */
static void taskbar_configure_notify_event(TaskbarPlugin * tb, XConfigureEvent * ev)
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
static GdkFilterReturn taskbar_event_filter(XEvent * xev, GdkEvent * event, TaskbarPlugin * tb)
{
    if (xev->type == PropertyNotify)
        taskbar_property_notify_event(tb, xev);
    else if (xev->type == ConfigureNotify)
        taskbar_configure_notify_event(tb, &xev->xconfigure);

    return GDK_FILTER_CONTINUE;
}

/* Handler for "activate" event on Raise item of right-click menu for task buttons. */
static void menu_raise_window(GtkWidget * widget, TaskbarPlugin * tb)
{
    if ((tb->menutask->desktop != ALL_WORKSPACES) && (tb->menutask->desktop != tb->current_desktop))
        Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, tb->menutask->desktop, 0, 0, 0, 0);
    XMapRaised(GDK_DISPLAY(), tb->menutask->win);
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Restore item of right-click menu for task buttons. */
static void menu_restore_window(GtkWidget * widget, TaskbarPlugin * tb)
{
    GdkWindow * win = gdk_window_foreign_new(tb->menutask->win);
    gdk_window_unmaximize(win);
    gdk_window_unref(win);
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Maximize item of right-click menu for task buttons. */
static void menu_maximize_window(GtkWidget * widget, TaskbarPlugin * tb)
{
    GdkWindow * win = gdk_window_foreign_new(tb->menutask->win);
    gdk_window_maximize(win);
    gdk_window_unref(win);
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Iconify item of right-click menu for task buttons. */
static void menu_iconify_window(GtkWidget * widget, TaskbarPlugin * tb)
{
    XIconifyWindow(GDK_DISPLAY(), tb->menutask->win, DefaultScreen(GDK_DISPLAY()));
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Move to Workspace item of right-click menu for task buttons. */
static void menu_move_to_workspace(GtkWidget * widget, TaskbarPlugin * tb)
{
    int num = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(widget), "num"));
    Xclimsg(tb->menutask->win, a_NET_WM_DESKTOP, num, 0, 0, 0, 0);
    task_group_menu_destroy(tb);
}

/* Handler for "activate" event on Close item of right-click menu for task buttons. */
static void menu_close_window(GtkWidget * widget, TaskbarPlugin * tb)
{
    Xclimsgwm(tb->menutask->win, a_WM_PROTOCOLS, a_WM_DELETE_WINDOW);
    task_group_menu_destroy(tb);
}

static void  on_menuitem_lock_tbp_clicked(GtkWidget * widget, TaskbarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)tb->plug->priv;
    guint32    flags;
    MenuCache *menu_cache = panel_menu_cache_new(&flags);
    if(menu_cache != NULL)
    {
        MenuCacheDir *dir = menu_cache_get_root_dir(menu_cache);
        if(dir)
        {
            ltbp->add_mb_to_lb = TRUE;
            ltbp->execute_mb = FALSE;
            ltbp->found_mb = FALSE;
            ltbp->exec_bin_mb = tb->menutask->exec_bin;
            f_find_menu_launchbutton_recursive(dir, ltbp);
            panel_config_save(tb->plug->panel);
        }
    }
}

static void  on_menuitem_unlock_tbp_clicked(GtkWidget * widget, TaskbarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)tb->plug->priv;
    LaunchButton *btn = launchbar_exec_bin_exists(&ltbp->lbp, tb->menutask->exec_bin);
    if(btn != NULL)
    {
        launchbar_remove_button(ltbp, btn);
        panel_config_save(tb->plug->panel);
    }
}

static void  on_menuitem_new_instance_clicked(GtkWidget * widget, TaskbarPlugin * tb)
{
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)tb->plug->priv;
    LaunchButton *btn = launchbar_exec_bin_exists(&ltbp->lbp, tb->menutask->exec_bin);
    if( (btn != NULL) && (btn->action != NULL) )
    {
        lxpanel_launch_app(btn->action, NULL, btn->use_terminal, btn->path);
    }
    else
    {
        guint32  flags;
        MenuCache *menu_cache = panel_menu_cache_new(&flags);
        if(menu_cache != NULL)
        {
            MenuCacheDir *dir = menu_cache_get_root_dir(menu_cache);
            if(dir)
            {
                ltbp->add_mb_to_lb = FALSE;
                ltbp->execute_mb = TRUE;
                ltbp->found_mb = FALSE;
                ltbp->exec_bin_mb = tb->menutask->exec_bin;
                f_find_menu_launchbutton_recursive(dir, ltbp);
            }
        }
    }
}

/* Make right-click menu for task buttons.
 * This depends on number of desktops and edge. */
static void taskbar_make_menu(TaskbarPlugin * tb)
{
    /* Deallocate old menu if present. */
    if (tb->menu != NULL)
        gtk_widget_destroy(tb->menu);

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
        }

        /* Add a separator. */
        gtk_menu_shell_append(GTK_MENU_SHELL(workspace_menu), gtk_separator_menu_item_new());

        /* Add "move to all workspaces" item.  This causes the window to be visible no matter what desktop is active. */
        mi = gtk_menu_item_new_with_mnemonic(_("_All workspaces"));
        g_object_set_data(G_OBJECT(mi), "num", GINT_TO_POINTER(ALL_WORKSPACES));
        g_signal_connect(mi, "activate", G_CALLBACK(menu_move_to_workspace), tb);
        gtk_menu_shell_append(GTK_MENU_SHELL(workspace_menu), mi);

        /* Add Move to Workspace menu item as a submenu. */
        mi = gtk_menu_item_new_with_mnemonic(_("_Move to Workspace"));
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), workspace_menu);
    }

    /* Add Close menu item.  By popular demand, we place this menu item closest to the cursor. */
    mi = gtk_menu_item_new_with_mnemonic (_("_Close Window"));
    tb->p_menuitem_lock_tbp = gtk_menu_item_new_with_mnemonic(_("A_dd to Launcher"));
    tb->p_menuitem_unlock_tbp = gtk_menu_item_new_with_mnemonic(_("Rem_ove from Launcher"));
    tb->p_menuitem_new_instance = gtk_menu_item_new_with_mnemonic(_("_New Instance"));
    
    if (tb->plug->panel->edge != EDGE_BOTTOM)
    {
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), tb->p_menuitem_lock_tbp);
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), tb->p_menuitem_unlock_tbp);
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), tb->p_menuitem_new_instance);
        
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), mi);
    }
    else
    {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), tb->p_menuitem_lock_tbp);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), tb->p_menuitem_unlock_tbp);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), tb->p_menuitem_new_instance);
        
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    g_signal_connect(G_OBJECT(mi), "activate", (GCallback)menu_close_window, tb);
    g_signal_connect(G_OBJECT(tb->p_menuitem_lock_tbp), "activate", (GCallback)on_menuitem_lock_tbp_clicked, tb);
    g_signal_connect(G_OBJECT(tb->p_menuitem_unlock_tbp), "activate", (GCallback)on_menuitem_unlock_tbp_clicked, tb);
    g_signal_connect(G_OBJECT(tb->p_menuitem_new_instance), "activate", (GCallback)on_menuitem_new_instance_clicked, tb);

    gtk_widget_show_all(menu);
    tb->menu = menu;
}

/* Handler for "window-manager-changed" event. */
static void taskbar_window_manager_changed(GdkScreen * screen, TaskbarPlugin * tb)
{
    /* Force re-evaluation of use_net_active. */
    tb->net_active_checked = FALSE;
}

/* Callback from configuration dialog mechanism to apply the configuration. */
static void taskbar_apply_configuration(Plugin * p)
{
    Task * tk;
    LaunchTaskBarPlugin *ltbp = (LaunchTaskBarPlugin *)p->priv;

    /* Update style on taskbar. */
    taskbar_update_style(&ltbp->tbp);

    /* Update styles on each button. */
    for (tk = ltbp->tbp.p_task_list; tk != NULL; tk = tk->p_task_flink_xwid)
    {
        /* If same_monitor_only wasn't set before, the monitor information
           wasn't tracked, so update it now. */
        if (ltbp->tbp.same_monitor_only)
            tk->monitor = get_window_monitor(tk->win);

        task_update_style(tk, &ltbp->tbp);
    }

    /* Refetch the client list and redraw. */
    recompute_group_visibility_on_current_desktop(&ltbp->tbp);
    taskbar_net_client_list(NULL, &ltbp->tbp);
}

/* Plugin descriptor. */
PluginClass launchtaskbar_plugin_class = {

    PLUGINCLASS_VERSIONING,

    .type = "launchtaskbar",
    .name = N_("Application Launch and Task Bar"),
    .version = "1.0",
    .description = N_("Bar with buttons to launch application and shows all opened windows"),

    .expand_available = TRUE,
    .expand_default = TRUE,

    .constructor = launchtaskbar_constructor,
    .destructor = launchtaskbar_destructor,
    .config = launchtaskbar_configure,
    .save = launchtaskbar_save_configuration,
    .panel_configuration_changed = launchtaskbar_panel_configuration_changed
};
