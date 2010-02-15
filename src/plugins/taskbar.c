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
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "icon.xpm"
#include "gtkbar.h"
#include "icon-grid.h"

/*
 * 2006.09.10 modified by Hong Jen Yee (PCMan) pcman.tw (AT) gmail.com
 * Following features are added:
 * 1. Add XUrgencyHint support. (Flashing task bar buttons, can be disabled)
 * 2. Raise window when files get dragged over taskbar buttons.
 * 3. Add Restore & Maximize menu items to popup menu of task bar buttons.
 */

#include "dbg.h"

struct _taskbar;
struct _task_class;
struct _task;

/* Structure representing a class.  This comes from WM_CLASS, and should identify windows that come from an application. */
typedef struct _task_class {
    struct _task_class * res_class_flink;	/* Forward link */
    char * res_class;				/* Class name */
    struct _task * res_class_head;		/* Head of list of tasks with this class */
    struct _task * visible_task;		/* Task that is visible in current desktop, if any */
    char * visible_name;			/* Name that will be visible for grouped tasks */
    int visible_count;				/* Count of tasks that are visible in current desktop */
} TaskClass;

/* Structure representing a "task", an open window. */
typedef struct _task {
    struct _task * task_flink;			/* Forward link to next task in X window ID order */
    struct _taskbar * tb;			/* Back pointer to taskbar */
    Window win;					/* X window ID */
    char * name;				/* Taskbar label when normal, from WM_NAME or NET_WM_NAME */
    char * name_iconified;			/* Taskbar label when iconified */
    Atom name_source;				/* Atom that is the source of taskbar label */
    TaskClass * res_class;			/* Class, from WM_CLASS */
    struct _task * res_class_flink;		/* Forward link to task in same class */
    GtkWidget * button;				/* Button representing task in taskbar */
    GtkWidget * image;				/* Icon for task, child of button */
    Atom image_source;				/* Atom that is the source of taskbar icon */
    GtkWidget * label;				/* Label for task, child of button */
    int desktop;				/* Desktop that contains task, needed to switch to it on Raise */
    guint flash_timeout;			/* Timer for urgency notification */
    unsigned int focused : 1;			/* True if window has focus */
    unsigned int iconified : 1;			/* True if window is iconified, from WM_STATE */
    unsigned int urgency : 1;			/* True if window has an urgency hint, from WM_HINTS */
    unsigned int flash_state : 1;		/* One-bit counter to flash taskbar */
    unsigned int entered_state : 1;		/* True if cursor is inside taskbar button */
    unsigned int present_in_client_list : 1;	/* State during WM_CLIENT_LIST processing to detect deletions */
} Task;

/* Private context for taskbar plugin. */
typedef struct _taskbar {
    Plugin * plug;				/* Back pointer to Plugin */
    Task * task_list;				/* List of tasks to be displayed in taskbar */
    TaskClass * res_class_list;			/* Window class list */
    IconGrid * icon_grid;			/* Manager for taskbar buttons */
    GtkWidget * menu;				/* Popup menu for task control (Close, Raise, etc.) */
    GtkWidget * group_menu;			/* Popup menu for grouping selection */
    GdkPixbuf * fallback_pixbuf;		/* Fallback task icon when none is available */
    int number_of_desktops;			/* Number of desktops, from NET_WM_NUMBER_OF_DESKTOPS */
    int current_desktop;			/* Current desktop, from NET_WM_CURRENT_DESKTOP */
    Task * focused;				/* Task that has focus */
    Task * focused_previous;			/* Task that had focus just before panel got it */
    Task * menutask;				/* Task for which popup menu is open */
    guint dnd_delay_timer;			/* Timer for drag and drop delay */
    int icon_size;				/* Size of task icons */
    gboolean show_all_desks;			/* User preference: show windows from all desktops */
    gboolean tooltips;				/* User preference: show tooltips */
    gboolean icons_only;			/* User preference: show icons only, omit name */
    gboolean use_mouse_wheel;			/* User preference: scroll wheel does iconify and raise */
    gboolean use_urgency_hint;			/* User preference: windows with urgency will flash */
    gboolean flat_button;			/* User preference: taskbar buttons have visible background */
    gboolean grouped_tasks;			/* User preference: windows from same task are grouped onto a single button */
    int task_width_max;				/* Maximum width of a taskbar button in horizontal orientation */
    int spacing;				/* Spacing between taskbar buttons */
    gboolean use_net_active;			/* NET_WM_ACTIVE_WINDOW is supported by the window manager */
    gboolean net_active_checked;		/* True if use_net_active is valid */
} TaskbarPlugin;

static gchar *taskbar_rc = "style 'taskbar-style'\n"
"{\n"
"GtkWidget::focus-padding=0\n" /* FIXME: seem to fix #2821771, not sure if this is ok. */
"GtkWidget::focus-line-width=0\n"
"GtkWidget::focus-padding=0\n"
"GtkButton::default-border={0,0,0,0}\n"
"GtkButton::default-outside-border={0,0,0,0}\n"
"GtkButton::inner-border={0,0,0,0}\n" /* added in gtk+ 2.10 */
"}\n"
"widget '*.taskbar.*' style 'taskbar-style'";

#define DRAG_ACTIVE_DELAY    1000
#define TASK_WIDTH_MAX       200
#define TASK_PADDING         4
#define ALL_WORKSPACES       (-1)
#define ICON_ONLY_EXTRA      6		/* Amount needed to have button lay out symmetrically */
#define BUTTON_HEIGHT_EXTRA  4          /* Amount needed to have button not clip icon */

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
static void taskbar_button_enter(GtkWidget * widget, Task * tk);
static void taskbar_button_leave(GtkWidget * widget, Task * tk);
static gboolean taskbar_button_scroll_event(GtkWidget * widget, GdkEventScroll * event, Task * tk);
static void taskbar_button_size_allocate(GtkWidget * btn, GtkAllocation * alloc, Task * tk);
static void taskbar_update_style(TaskbarPlugin * tb);
static void task_update_style(Task * tk, TaskbarPlugin * tb);
static void task_build_gui(TaskbarPlugin * tb, Task * tk);
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
static void taskbar_make_menu(TaskbarPlugin * tb);
static void taskbar_window_manager_changed(GdkScreen * screen, TaskbarPlugin * tb);
static void taskbar_build_gui(Plugin * p);
static int taskbar_constructor(Plugin * p, char ** fp);
static void taskbar_destructor(Plugin * p);
static void taskbar_apply_configuration(Plugin * p);
static void taskbar_configure(Plugin * p, GtkWindow * parent);
static void taskbar_save_configuration(Plugin * p, FILE * fp);
static void taskbar_panel_configuration_changed(Plugin * p);

/* Set an urgency timer on a task. */
static void set_timer_on_task(Task * tk)
{
    gint interval;
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
    tc->visible_task = NULL;
    tc->visible_name = NULL;
    Task * flashing_task = NULL;
    gboolean class_has_urgency = FALSE;
    Task * tk;
    for (tk = tc->res_class_head; tk != NULL; tk = tk->res_class_flink)
    {
        if (task_is_visible_on_current_desktop(tb, tk))
        {
            /* Count visible tasks and make the first visible task the one that is used for display. */
            if (tc->visible_count == 0)
                tc->visible_task = tk;
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
            tc->visible_task->flash_state = TRUE;
            flash_window_timeout(tc->visible_task);

            /* Set the timer, since none is set. */
            set_timer_on_task(tc->visible_task);
        }
        else if (flashing_task != tc->visible_task)
        {
            /* Reset the timer on the new representative.
             * There will be a slight hiccup on the flash cadence. */
            g_source_remove(flashing_task->flash_timeout);
            flashing_task->flash_timeout = 0;
            tc->visible_task->flash_state = flashing_task->flash_state;
            flashing_task->flash_state = FALSE;
            set_timer_on_task(tc->visible_task);
        }   
    }
    else
    {
        /* No task has urgency.  Cancel the timer if one is set. */
        if (flashing_task != NULL)
        {
            g_source_remove(flashing_task->flash_timeout);
            flashing_task->flash_state = FALSE;
        }
    }
}

/* Recompute the visible task for all classes when the desktop changes. */
static void recompute_group_visibility_on_current_desktop(TaskbarPlugin * tb)
{
    TaskClass * tc;
    for (tc = tb->res_class_list; tc != NULL; tc = tc->res_class_flink)
    {
        recompute_group_visibility_for_class(tb, tc);
    }
}

/* Draw the label and tooltip on a taskbar button. */
static void task_draw_label(Task * tk)
{
    TaskClass * tc = tk->res_class;
    gboolean bold_style = (((tk->entered_state) && (tk->tb->flat_button)) || tk->flash_state);
    if ((tk->tb->grouped_tasks) && (tc != NULL) && (tc->visible_task == tk) && (tc->visible_count > 1))
	{
        char * label = g_strdup_printf("(%d) %s", tc->visible_count, tc->visible_name);
        gtk_widget_set_tooltip_text(tk->button, label);
        panel_draw_label_text(tk->tb->plug->panel, tk->label, label, bold_style, tk->tb->flat_button);
        g_free(label);
	}
    else
    {
        char * name = tk->iconified ? tk->name_iconified : tk->name;
        if (tk->tb->tooltips)
            gtk_widget_set_tooltip_text(tk->button, name);
        panel_draw_label_text(tk->tb->plug->panel, tk->label, name, bold_style, tk->tb->flat_button);
    }
}

/* Determine if a task is visible. */
static gboolean task_is_visible(TaskbarPlugin * tb, Task * tk)
{
    /* Not visible due to grouping. */
    if ((tb->grouped_tasks) && (tk->res_class != NULL) && (tk->res_class->visible_task != tk))
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
    for (tk = tb->task_list; tk != NULL; tk = tk->task_flink)
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
    TaskClass * tc = tk->res_class;
    if (tc != NULL)
    {
        /* Remove from per-class task list. */
        if (tc->res_class_head == tk)
        {
            /* Removing the head of the list.  This causes a new task to be the visible task, so we redraw. */
            tc->res_class_head = tk->res_class_flink;
            if (tc->res_class_head != NULL)
                task_button_redraw(tc->res_class_head, tk->tb);
        }
        else
        {
            /* Locate the task and its predecessor in the list and then remove it.  For safety, ensure it is found. */
            Task * tk_pred = NULL;
            Task * tk_cursor;
            for (
              tk_cursor = tc->res_class_head;
              ((tk_cursor != NULL) && (tk_cursor != tk));
              tk_pred = tk_cursor, tk_cursor = tk_cursor->res_class_flink) ;
            if (tk_cursor == tk)
                tk_pred->res_class_flink = tk->res_class_flink;
        }

        /* Recompute group visibility. */
        recompute_group_visibility_for_class(tk->tb, tc);
    }
}

/* Enter class with specified name. */
static TaskClass * taskbar_enter_res_class(TaskbarPlugin * tb, char * res_class, gboolean * name_consumed)
    {
    /* Find existing entry or insertion point. */
    *name_consumed = FALSE;
    TaskClass * tc_pred = NULL;
    TaskClass * tc;
    for (tc = tb->res_class_list; tc != NULL; tc_pred = tc, tc = tc->res_class_flink)
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
    *name_consumed = TRUE;
    if (tc_pred == NULL)
    {
        tc->res_class_flink = tb->res_class_list;
        tb->res_class_list = tc;
    }
    else
    {
        tc->res_class_flink = tc_pred->res_class_flink;
	tc_pred->res_class_flink = tc;
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
        gboolean name_consumed;
        TaskClass * tc = taskbar_enter_res_class(tk->tb, res_class, &name_consumed);
        if ( ! name_consumed) g_free(res_class);

        /* If the task changed class, update data structures. */
        TaskClass * old_tc = tk->res_class;
        if (old_tc != tc)
        {
            /* Unlink from previous class, if any. */
            task_unlink_class(tk);

            /* Add to end of per-class task list.  Do this to keep the popup menu in order of creation. */
            if (tc->res_class_head == NULL)
                tc->res_class_head = tk;
            else
            {
                Task * tk_pred;
                for (tk_pred = tc->res_class_head; tk_pred->res_class_flink != NULL; tk_pred = tk_pred->res_class_flink) ;
                tk_pred->res_class_flink = tk;
                task_button_redraw(tk, tk->tb);
            }
            tk->res_class = tc;

            /* Recompute group visibility. */
            recompute_group_visibility_for_class(tk->tb, tc);
        }
        XFree(ch.res_class);
    }
}

/* Look up a task in the task list. */
static Task * task_lookup(TaskbarPlugin * tb, Window win)
{
    Task * tk;
    for (tk = tb->task_list; tk != NULL; tk = tk->task_flink)
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

    /* Deallocate structures. */
    icon_grid_remove(tb->icon_grid, tk->button);
    task_free_names(tk);
    task_unlink_class(tk);

    /* If there is an urgency timeout, remove it. */
    if (tk->flash_timeout != 0)
        g_source_remove(tk->flash_timeout);

    /* If requested, unlink the task from the task list.
     * If not requested, the caller will do this. */
    if (unlink)
    {
        if (tb->task_list == tk)
            tb->task_list = tk->task_flink;
        else
        {
            /* Locate the task and its predecessor in the list and then remove it.  For safety, ensure it is found. */
            Task * tk_pred = NULL;
            Task * tk_cursor;
            for (
              tk_cursor = tb->task_list;
              ((tk_cursor != NULL) && (tk_cursor != tk));
              tk_pred = tk_cursor, tk_cursor = tk_cursor->task_flink) ;
            if (tk_cursor == tk)
                tk_pred->task_flink = tk->task_flink;
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
        if ((colormap != NULL) && (gdk_colormap_get_visual(colormap)->depth != depth))
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
            d[3] = ((s[0] == 0) ? 0 : 255);	/* 0 = transparent, 255 = opaque */
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
                    TRUE, 8,	/* has_alpha, bits_per_sample */
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
    GdkPixbuf * pixbuf = get_wm_icon(tk->win, tb->icon_size, tb->icon_size, source, &tk->image_source);

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
    if (tk->tb->flat_button)
        task_draw_label(tk);
    else
    {
        gtk_widget_set_state(tk->button, tk->flash_state ? GTK_STATE_SELECTED : GTK_STATE_NORMAL);
        gtk_widget_queue_draw(tk->button);
    }

    /* Complement the flashing context. */
    tk->flash_state = ! tk->flash_state;
    return TRUE;
}

/* Set urgency notification. */
static void task_set_urgency(Task * tk)
{
    if (( ! tk->tb->grouped_tasks) || (tk->res_class == NULL))
    {
        /* Set the flashing context and flash the window immediately. */
        tk->flash_state = TRUE;
        flash_window_timeout(tk);

        /* Set the timer if none is set. */
        if (tk->flash_timeout == 0)
            set_timer_on_task(tk);
    }
    else
        recompute_group_visibility_for_class(tk->tb, tk->res_class);
}

/* Clear urgency notification. */
static void task_clear_urgency(Task * tk)
{
    if (( ! tk->tb->grouped_tasks) || (tk->res_class == NULL))
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
    else
        recompute_group_visibility_for_class(tk->tb, tk->res_class);
}

/* Do the proper steps to raise a window.
 * This means removing it from iconified state and bringing it to the front.
 * We also switch the active desktop and viewport if needed. */
static void task_raise_window(Task * tk, guint32 time)
{
    /* Change desktop if needed. */
    if ((tk->desktop != -1) && (tk->desktop != tk->tb->current_desktop))
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
    TaskClass * tc = tk->res_class;
    if ((tb->grouped_tasks) && (tc != NULL) && (tc->visible_count > 1) && (GTK_IS_BUTTON(widget)))
    {
        /* If this is a grouped-task representative, meaning that there is a class with at least two windows,
         * bring up a popup menu listing all the class members. */
        GtkWidget * menu = gtk_menu_new();
        Task * tk_cursor;
        for (tk_cursor = tc->res_class_head; tk_cursor != NULL; tk_cursor = tk_cursor->res_class_flink)
        {
            if (task_is_visible_on_current_desktop(tb, tk_cursor))
            {
                /* The menu item has the name, or the iconified name, and the icon of the application window. */
                GtkWidget * mi = gtk_image_menu_item_new_with_label(((tk_cursor->iconified) ? tk_cursor->name_iconified : tk_cursor->name));
                GtkWidget * im = gtk_image_new_from_pixbuf(gtk_image_get_pixbuf(GTK_IMAGE(tk_cursor->image)));
                gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), im);
                g_signal_connect(mi, "button_press_event", G_CALLBACK(taskbar_popup_activate_event), (gpointer) tk_cursor);
	        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
            }
        }

        /* Show the menu.  Set context so we can find the menu later to dismiss it.
         * Use a position-calculation callback to get the menu nicely positioned with respect to the button. */
        gtk_widget_show_all(menu);
        tb->group_menu = menu;
        gtk_menu_popup(GTK_MENU(menu), NULL, NULL, (GtkMenuPositionFunc) taskbar_popup_set_position, (gpointer) tk, event->button, event->time);
    }
    else
    {
        /* Not a grouped-task representative, or entered from the grouped-task popup menu. */
        Task * visible_task = (((tk->res_class == NULL) || ( ! tk->tb->grouped_tasks)) ? tk : tk->res_class->visible_task);
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
                2,		/* a_NET_WM_STATE_TOGGLE */
                a_NET_WM_STATE_SHADED,
                0, 0, 0);
        }
        else if (event->button == 3)
        {
            /* Right button.  Bring up the window state popup menu. */
            tk->tb->menutask = tk;
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
    TaskClass * tc = tk->res_class;
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
        gdk_window_get_origin(GTK_BUTTON(btn)->event_window, &x, &y);

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
        ((tb->icons_only) ? tb->icon_size + ICON_ONLY_EXTRA : tb->task_width_max), tb->icon_size + BUTTON_HEIGHT_EXTRA,
        tb->spacing, 0, tb->plug->panel->height);
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
    g_signal_connect(tk->button, "button_press_event", G_CALLBACK(taskbar_button_press_event), (gpointer) tk);
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
    GTK_WIDGET_UNSET_FLAGS(tk->button, GTK_CAN_FOCUS);
    GTK_WIDGET_UNSET_FLAGS(tk->button, GTK_CAN_DEFAULT);

    /* Update styles on the button. */
    task_update_style(tk, tb);

    /* Flash button for window with urgency hint. */
    if (tk->urgency)
        task_set_urgency(tk);
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
            for (tk_cursor = tb->task_list; tk_cursor != NULL; tk_pred = tk_cursor, tk_cursor = tk_cursor->task_flink)
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
                    if (tb->use_urgency_hint)
                        tk->urgency = task_has_urgency(tk);
                    task_build_gui(tb, tk);
                    task_set_names(tk, None);
                    task_set_class(tk);

                    /* Link the task structure into the task list. */
                    if (tk_pred == NULL)
                    {
                        tk->task_flink = tb->task_list;
                        tb->task_list = tk;
                    }
                    else
                    {
                        tk->task_flink = tk_pred->task_flink;
                        tk_pred->task_flink = tk;
                    }
                }
            }
        }
        XFree(client_list);
    }

    /* Remove windows from the task list that are not present in the NET_CLIENT_LIST. */
    Task * tk_pred = NULL;
    Task * tk = tb->task_list;
    while (tk != NULL)
    {
        Task * tk_succ = tk->task_flink;
        if (tk->present_in_client_list)
        {
            tk->present_in_client_list = FALSE;
            tk_pred = tk;
        }
        else
        {
            if (tk_pred == NULL)
                tb->task_list = tk_succ;
                else tk_pred->task_flink = tk_succ;
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
                    if (tk->res_class != NULL)
                    {
                        /* A change to the window name may change the visible name of the class. */
                        recompute_group_visibility_for_class(tb, tk->res_class);
                        if (tk->res_class->visible_task != NULL)
                            task_draw_label(tk->res_class->visible_task);
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

/* GDK event filter. */
static GdkFilterReturn taskbar_event_filter(XEvent * xev, GdkEvent * event, TaskbarPlugin * tb)
{
    /* Look for PropertyNotify events and update state. */
    if (xev->type == PropertyNotify)
        taskbar_property_notify_event(tb, xev);
    return GDK_FILTER_CONTINUE;
}

/* Handler for "activate" event on Raise item of right-click menu for task buttons. */
static void menu_raise_window(GtkWidget * widget, TaskbarPlugin * tb)
{
    if ((tb->menutask->desktop != -1) && (tb->menutask->desktop != tb->current_desktop))
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
    if (tb->plug->panel->edge != EDGE_BOTTOM)
    {
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), mi);
    }
    else
    {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
    }
    g_signal_connect(G_OBJECT(mi), "activate", (GCallback)menu_close_window, tb);

    gtk_widget_show_all(menu);
    tb->menu = menu;
}

/* Handler for "window-manager-changed" event. */
static void taskbar_window_manager_changed(GdkScreen * screen, TaskbarPlugin * tb)
{
    /* Force re-evaluation of use_net_active. */
    tb->net_active_checked = FALSE;
}

/* Build graphic elements needed for the taskbar. */
static void taskbar_build_gui(Plugin * p)
{
    TaskbarPlugin * tb = (TaskbarPlugin *) p->priv;

    /* Set up style for taskbar. */
    gtk_rc_parse_string(taskbar_rc);

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 0);
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);
    gtk_widget_set_name(p->pwid, "taskbar");

    /* Make container for task buttons as a child of top level widget. */
    GtkOrientation bo = (tb->plug->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    tb->icon_grid = icon_grid_new(p->panel, p->pwid, bo, tb->task_width_max, tb->icon_size, tb->spacing, 0, p->panel->height);
    icon_grid_set_constrain_width(tb->icon_grid, TRUE);
    taskbar_update_style(tb);

    /* Add GDK event filter. */
    gdk_window_add_filter(NULL, (GdkFilterFunc) taskbar_event_filter, tb);

    /* Connect signal to receive mouse events on the unused portion of the taskbar. */
    g_signal_connect(p->pwid, "button-press-event", G_CALLBACK(plugin_button_press_event), p);

    /* Connect signals to receive root window events and initialize root window properties. */
    tb->number_of_desktops = get_net_number_of_desktops();
    tb->current_desktop = get_net_current_desktop();
    g_signal_connect(G_OBJECT(fbev), "current_desktop", G_CALLBACK(taskbar_net_current_desktop), (gpointer) tb);
    g_signal_connect(G_OBJECT(fbev), "active_window", G_CALLBACK(taskbar_net_active_window), (gpointer) tb);
    g_signal_connect(G_OBJECT(fbev), "number_of_desktops", G_CALLBACK(taskbar_net_number_of_desktops), (gpointer) tb);
    g_signal_connect(G_OBJECT(fbev), "client_list", G_CALLBACK(taskbar_net_client_list), (gpointer) tb);

    /* Make right-click menu for task buttons.
     * It is retained for the life of the taskbar and will be shown as needed.
     * Number of desktops and edge is needed for this operation. */
    taskbar_make_menu(tb);

    /* Connect a signal to be notified when the window manager changes.  This causes re-evaluation of the "use_net_active" status. */
    g_signal_connect(gtk_widget_get_screen(p->pwid), "window-manager-changed", G_CALLBACK(taskbar_window_manager_changed), tb);
}

/* Plugin constructor. */
static int taskbar_constructor(Plugin * p, char ** fp)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    TaskbarPlugin * tb = g_new0(TaskbarPlugin, 1);
    tb->plug = p;
    p->priv = tb;

    /* Initialize to defaults. */
    tb->icon_size         = p->panel->icon_size;
    tb->tooltips          = TRUE;
    tb->icons_only        = FALSE;
    tb->show_all_desks    = FALSE;
    tb->task_width_max    = TASK_WIDTH_MAX;
    tb->spacing           = 1;
    tb->use_mouse_wheel   = TRUE;
    tb->use_urgency_hint  = TRUE;
    tb->grouped_tasks     = FALSE;

    /* Process configuration file. */
    line s;
    s.len = 256;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "taskbar: illegal token %s\n", s.str);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "tooltips") == 0)
                    tb->tooltips = str2num(bool_pair, s.t[1], 1);
                else if (g_ascii_strcasecmp(s.t[0], "IconsOnly") == 0)
                    tb->icons_only = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "AcceptSkipPager") == 0)		/* For backward compatibility */
                    ;
                else if (g_ascii_strcasecmp(s.t[0], "ShowIconified") == 0)		/* For backward compatibility */
                    ;
                else if (g_ascii_strcasecmp(s.t[0], "ShowMapped") == 0)			/* For backward compatibility */
                    ;
                else if (g_ascii_strcasecmp(s.t[0], "ShowAllDesks") == 0)
                    tb->show_all_desks = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "MaxTaskWidth") == 0)
                    tb->task_width_max = atoi(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "spacing") == 0)
                    tb->spacing = atoi(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "UseMouseWheel") == 0)
                    tb->use_mouse_wheel = str2num(bool_pair, s.t[1], 1);
                else if (g_ascii_strcasecmp(s.t[0], "UseUrgencyHint") == 0)
                    tb->use_urgency_hint = str2num(bool_pair, s.t[1], 1);
                else if (g_ascii_strcasecmp(s.t[0], "FlatButton") == 0)
                    tb->flat_button = str2num(bool_pair, s.t[1], 1);
                else if (g_ascii_strcasecmp(s.t[0], "GroupedTasks") == 0)
                    tb->grouped_tasks = str2num(bool_pair, s.t[1], 1);
                else
                    ERR( "taskbar: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "taskbar: illegal in this context %s\n", s.str);
                return 0;
            }
        }
    }

    /* Build the graphic elements. */
    taskbar_build_gui(p);

    /* Fetch the client list and redraw the taskbar.  Then determine what window has focus. */
    taskbar_net_client_list(NULL, tb);
    taskbar_net_active_window(NULL, tb);
    return 1;
}

/* Plugin destructor. */
static void taskbar_destructor(Plugin * p)
{
    TaskbarPlugin * tb = (TaskbarPlugin *) p->priv;

    /* Remove GDK event filter. */
    gdk_window_remove_filter(NULL, (GdkFilterFunc) taskbar_event_filter, tb);

    /* Remove root window signal handlers. */
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_current_desktop, tb);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_active_window, tb);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_number_of_desktops, tb);
    g_signal_handlers_disconnect_by_func(fbev, taskbar_net_client_list, tb);

    /* Remove "window-manager-changed" handler. */
    g_signal_handlers_disconnect_by_func(gtk_widget_get_screen(p->pwid), taskbar_window_manager_changed, tb);

    /* Deallocate task list. */
    while (tb->task_list != NULL)
        task_delete(tb, tb->task_list, TRUE);

    /* Deallocate class list. */
    while (tb->res_class_list != NULL)
    {
        TaskClass * tc = tb->res_class_list;
        tb->res_class_list = tc->res_class_flink;
        g_free(tc->res_class);
        g_free(tc);
    }

    /* Deallocate other memory. */
    gtk_widget_destroy(tb->menu);
    g_free(tb);
}

/* Callback from configuration dialog mechanism to apply the configuration. */
static void taskbar_apply_configuration(Plugin * p)
{
    TaskbarPlugin * tb = (TaskbarPlugin *) p->priv;

    /* Update style on taskbar. */
    taskbar_update_style(tb);

    /* Update styles on each button. */
    Task * tk;
    for (tk = tb->task_list; tk != NULL; tk = tk->task_flink)
        task_update_style(tk, tb);

    /* Refetch the client list and redraw. */
    recompute_group_visibility_on_current_desktop(tb);
    taskbar_net_client_list(NULL, tb);
}

/* Display the configuration dialog. */
static void taskbar_configure(Plugin * p, GtkWindow * parent)
{
    TaskbarPlugin * tb = (TaskbarPlugin *) p->priv;
    GtkWidget* dlg = create_generic_config_dlg(
        _(p->class->name),
        GTK_WIDGET(parent),
        (GSourceFunc) taskbar_apply_configuration, (gpointer) p,
        _("Show tooltips"), &tb->tooltips, CONF_TYPE_BOOL,
        _("Icons only"), &tb->icons_only, CONF_TYPE_BOOL,
        _("Flat buttons"), &tb->flat_button, CONF_TYPE_BOOL,
        _("Show windows from all desktops"), &tb->show_all_desks, CONF_TYPE_BOOL,
        _("Use mouse wheel"), &tb->use_mouse_wheel, CONF_TYPE_BOOL,
        _("Flash when there is any window requiring attention"), &tb->use_urgency_hint, CONF_TYPE_BOOL,
        _("Combine multiple application windows into a single button"), &tb->grouped_tasks, CONF_TYPE_BOOL,
        _("Maximum width of task button"), &tb->task_width_max, CONF_TYPE_INT,
        _("Spacing"), &tb->spacing, CONF_TYPE_INT,
        NULL);
    gtk_window_present(GTK_WINDOW(dlg));
}

/* Save the configuration to the configuration file. */
static void taskbar_save_configuration(Plugin * p, FILE * fp)
{
    TaskbarPlugin * tb = (TaskbarPlugin *) p->priv;
    lxpanel_put_bool(fp, "tooltips", tb->tooltips);
    lxpanel_put_bool(fp, "IconsOnly", tb->icons_only);
    lxpanel_put_bool(fp, "ShowAllDesks", tb->show_all_desks);
    lxpanel_put_bool(fp, "UseMouseWheel", tb->use_mouse_wheel);
    lxpanel_put_bool(fp, "UseUrgencyHint", tb->use_urgency_hint);
    lxpanel_put_bool(fp, "FlatButton", tb->flat_button);
    lxpanel_put_int(fp, "MaxTaskWidth", tb->task_width_max);
    lxpanel_put_int(fp, "spacing", tb->spacing);
    lxpanel_put_bool(fp, "GroupedTasks", tb->grouped_tasks);
}

/* Callback when panel configuration changes. */
static void taskbar_panel_configuration_changed(Plugin * p)
{
    TaskbarPlugin * tb = (TaskbarPlugin *) p->priv;
    taskbar_update_style(tb);
    taskbar_make_menu(tb);
    GtkOrientation bo = (tb->plug->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    icon_grid_set_geometry(tb->icon_grid, bo,
        ((tb->icons_only) ? tb->plug->panel->icon_size + ICON_ONLY_EXTRA : tb->task_width_max), tb->plug->panel->icon_size + BUTTON_HEIGHT_EXTRA,
        tb->spacing, 0, tb->plug->panel->height);

    /* If the icon size changed, refetch all the icons. */
    if (tb->plug->panel->icon_size != tb->icon_size)
    {
        tb->icon_size = tb->plug->panel->icon_size;
        Task * tk;
        for (tk = tb->task_list; tk != NULL; tk = tk->task_flink)
        {
            GdkPixbuf * pixbuf = task_update_icon(tb, tk, None);
            if (pixbuf != NULL)
            {
                gtk_image_set_from_pixbuf(GTK_IMAGE(tk->image), pixbuf);
                g_object_unref(pixbuf);
            }
        }
    }

    /* Redraw all the labels.  Icon size or font color may have changed. */
    taskbar_redraw(tb);
}

/* Plugin descriptor. */
PluginClass taskbar_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "taskbar",
    name : N_("Task Bar (Window List)"),
    version: "1.0",
    description : N_("Taskbar shows all opened windows and allow to iconify them, shade or get focus"),

    /* Stretch is available and default for this plugin. */
    expand_available : TRUE,
    expand_default : TRUE,

    constructor : taskbar_constructor,
    destructor  : taskbar_destructor,
    config : taskbar_configure,
    save : taskbar_save_configuration,
    panel_configuration_changed : taskbar_panel_configuration_changed

};
