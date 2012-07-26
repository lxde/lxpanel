/** pager.c -- pager module of lxpanel project
 *
 * Copyright (C) 2002-2003 Anatoly Asviyan <aanatoly@users.sf.net>
 *                         Joe MacDonald   <joe@deserted.net>
 *
 * This file is part of lxpanel.
 *
 * lxpanel is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * lxpanel is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with sawfish; see the file COPYING.   If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "icon-grid.h"

#include "dbg.h"

struct _task;
struct _desk;
struct _pager;

#define ALL_DESKTOPS   0xFFFFFFFF		/* 64-bit clean */
#define BORDER_WIDTH   2

/* Structure representing a "task", an open window. */
typedef struct _task {
    struct _task * task_flink;			/* Forward link of task list */
    Window win;					/* X window ID */
    int x;					/* Geometry as reported by X server */
    int y;
    guint w;
    guint h;
    int stacking;				/* Stacking order as reported by NET_WM_CLIENT_STACKING */
    int desktop;				/* Desktop that contains task */
    int ws;					/* WM_STATE value */
    NetWMState nws;				/* NET_WM_STATE value */
    NetWMWindowType nwwt;			/* NET_WM_WINDOW_TYPE value */
    guint focused : 1;				/* True if window has focus */
    guint present_in_client_list : 1;		/* State during WM_CLIENT_LIST processing to detect deletions */
} PagerTask;

/* Structure representing a desktop. */
typedef struct _desk {
    struct _pager * pg;				/* Back pointer to plugin context */
    GtkWidget * da;				/* Drawing area */
    cairo_surface_t * pixmap;			/* Pixmap to be drawn on drawing area */
    int desktop_number;				/* Desktop number */
    gboolean dirty;				/* True if needs to be recomputed */
    gfloat scale_x;				/* Horizontal scale factor */
    gfloat scale_y;				/* Vertical scale factor */
} PagerDesk;

/* Private context for pager plugin. */
typedef struct _pager {
    Plugin * plugin;				/* Back pointer to plugin */
    IconGrid * icon_grid;			/* Container widget */
    int desk_extent;				/* Extent of desks vector */
    PagerDesk * * desks;			/* Vector of desktop structures */
    guint number_of_desktops;			/* Number of desktops, from NET_WM_NUMBER_OF_DESKTOPS */
    guint current_desktop;			/* Current desktop, from NET_WM_CURRENT_DESKTOP */
    gfloat aspect_ratio;			/* Aspect ratio of screen image */
    int client_count;				/* Count of tasks in stacking order */
    PagerTask * * tasks_in_stacking_order;	/* Vector of tasks in stacking order */
    PagerTask * task_list;			/* Tasks in window ID order */
    PagerTask * focused_task;			/* Task that has focus */
} PagerPlugin;

static gboolean task_is_visible(PagerTask * tk);
static PagerTask * task_lookup(PagerPlugin * pg, Window win);
static void task_delete(PagerPlugin * pg, PagerTask * tk, gboolean unlink);
static void task_get_geometry(PagerTask * tk);
static void task_update_pixmap(PagerTask * tk, PagerDesk * d);
static void desk_set_dirty(PagerDesk * d);
static void desk_set_dirty_all(PagerPlugin * pg);
static void desk_set_dirty_by_win(PagerPlugin * pg, PagerTask * tk);
static gboolean desk_configure_event(GtkWidget * widget, GdkEventConfigure * event, PagerDesk * d);
static gboolean desk_expose_event(GtkWidget * widget, GdkEventExpose * event, PagerDesk * d);
static gboolean desk_scroll_event(GtkWidget * widget, GdkEventScroll * event, PagerDesk * d);
static gboolean desk_button_press_event(GtkWidget * widget, GdkEventButton * event, PagerDesk * d);
static void desk_new(PagerPlugin * pg, int desktop_number);
static void desk_free(PagerPlugin * pg, int desktop_number);
static void pager_property_notify_event(PagerPlugin * p, XEvent * ev);
static void pager_configure_notify_event(PagerPlugin * pg, XEvent * ev);
static GdkFilterReturn pager_event_filter(XEvent * xev, GdkEvent * event, PagerPlugin * pg);
static void pager_net_active_window(FbEv * ev, PagerPlugin * pg);
static void pager_net_desktop_names(FbEv * ev, PagerPlugin * pg);
static void pager_net_number_of_desktops(FbEv * ev, PagerPlugin * pg);
static void pager_net_client_list_stacking(FbEv * ev, PagerPlugin * pg);
static int pager_constructor(Plugin * plug, char ** fp);
static void pager_destructor(Plugin * p);
static void pager_panel_configuration_changed(Plugin * p);

/*****************************************************************
 * Task Management Routines                                      *
 *****************************************************************/

/* Determine if a task is visible. */
static gboolean task_is_visible(PagerTask * tk)
{
    return ( ! ((tk->nws.hidden) || (tk->nws.skip_pager) || (tk->nwwt.dock) || (tk->nwwt.desktop)));
}

/* Look up a task in the task list. */
static PagerTask * task_lookup(PagerPlugin * pg, Window win)
{
    PagerTask * tk;
    for (tk = pg->task_list; tk != NULL; tk = tk->task_flink)
        {
        if (tk->win == win)
	    return tk;
        if (tk->win > win)
            break;
        }
    return NULL;
}

/* Delete a task and optionally unlink it from the task list. */
static void task_delete(PagerPlugin * pg, PagerTask * tk, gboolean unlink)
{
    /* If we think this task had focus, remove that. */
    if (pg->focused_task == tk)
        pg->focused_task = NULL;

    /* If requested, unlink the task from the task list.
     * If not requested, the caller will do this. */
    if (unlink)
    {
        if (pg->task_list == tk)
            pg->task_list = tk->task_flink;
        else
        {
            /* Locate the task and its predecessor in the list and then remove it.  For safety, ensure it is found. */
            PagerTask * tk_pred = NULL;
            PagerTask * tk_cursor;
            for (
              tk_cursor = pg->task_list;
              ((tk_cursor != NULL) && (tk_cursor != tk));
              tk_pred = tk_cursor, tk_cursor = tk_cursor->task_flink) ;
            if (tk_cursor == tk)
                tk_pred->task_flink = tk->task_flink;
        }
    }

    /* Deallocate the task structure. */
    g_free(tk);
}

/* Get the geometry of a task window in screen coordinates. */
static void task_get_geometry(PagerTask * tk)
{
    /* Install an error handler that ignores BadWindow and BadDrawable.
     * We frequently get a ConfigureNotify event on deleted windows. */
    XErrorHandler previous_error_handler = XSetErrorHandler(panel_handle_x_error_swallow_BadWindow_BadDrawable);

    XWindowAttributes win_attributes;
    if (XGetWindowAttributes(GDK_DISPLAY(), tk->win, &win_attributes))
    {
        Window unused_win;
        int rx, ry;
        XTranslateCoordinates(GDK_DISPLAY(), tk->win, win_attributes.root,
              - win_attributes.border_width,
              - win_attributes.border_width,
              &rx, &ry, &unused_win);
        tk->x = rx;
        tk->y = ry;
        tk->w = win_attributes.width;
        tk->h = win_attributes.height;
    }
    else
    {
        Window unused_win;
        guint unused;
        if ( ! XGetGeometry(GDK_DISPLAY(), tk->win,
            &unused_win, &tk->x, &tk->y, &tk->w, &tk->h, &unused, &unused))
        {
            tk->x = tk->y = tk->w = tk->h = 2;
        }
    }

    XSetErrorHandler(previous_error_handler);
}

/* Draw the representation of a task's window on the backing pixmap. */
static void task_update_pixmap(PagerTask * tk, PagerDesk * d)
{
    if ((d->pixmap != NULL) && (task_is_visible(tk)))
    {
        if ((tk->desktop == ALL_DESKTOPS) || (tk->desktop == d->desktop_number))
        {
            /* Scale the representation of the window to the drawing area. */
            gfloat x = (gfloat) tk->x * d->scale_x;
            gfloat y = (gfloat) tk->y * d->scale_y;
            gfloat w = (gfloat) tk->w * d->scale_x;
            gfloat h = ((tk->nws.shaded) ? 3 : (gfloat) tk->h * d->scale_y);
            if ((w >= 3) && (h >= 3))
            {
                /* Draw the window representation and a border. */
                GtkWidget * widget = GTK_WIDGET(d->da);
                GtkStyle * style = gtk_widget_get_style(widget);
                GdkColor * color;

                cairo_t * cr = cairo_create(d->pixmap);
                cairo_set_line_width (cr, 1.0);
		cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
                cairo_rectangle(cr, x + 0.5, y + 0.5, w - 0.5, h - 0.5);

                color =
                    (d->pg->focused_task == tk) ? &style->bg[GTK_STATE_SELECTED] : &style->bg[GTK_STATE_NORMAL];
                cairo_set_source_rgb(cr, (double)color->red/65535, (double)color->green/65535, (double)color->blue/65535);
                cairo_fill_preserve(cr);

                color =
                    (d->pg->focused_task == tk) ? &style->fg[GTK_STATE_SELECTED] : &style->fg[GTK_STATE_NORMAL];
                cairo_set_source_rgb(cr, (double)color->red/65535, (double)color->green/65535, (double)color->blue/65535);
                cairo_stroke(cr);

		check_cairo_status(cr);
                cairo_destroy(cr);
            }
        }
    }
}

/*****************************************************************
 * Desk Functions                                                *
 *****************************************************************/

/* Mark a specified desktop for redraw. */
static void desk_set_dirty(PagerDesk * d)
{
    d->dirty = TRUE;
    gtk_widget_queue_draw(d->da);
}

/* Mark all desktops for redraw. */
static void desk_set_dirty_all(PagerPlugin * pg)
{
    int i;
    for (i = 0; i < pg->number_of_desktops; i++)
        desk_set_dirty(pg->desks[i]);
}

/* Mark the desktop on which a specified window resides for redraw. */
static void desk_set_dirty_by_win(PagerPlugin * pg, PagerTask * tk)
{
    if (task_is_visible(tk))
    {
        if (tk->desktop < pg->number_of_desktops)
            desk_set_dirty(pg->desks[tk->desktop]);
        else
            desk_set_dirty_all(pg);
    }
}

/* Handler for configure_event on drawing area. */
static gboolean desk_configure_event(GtkWidget * widget, GdkEventConfigure * event, PagerDesk * d)
{
    /* Allocate pixmap and statistics buffer without border pixels. */
#if GTK_CHECK_VERSION(2,18,0)
    GtkAllocation *allocation = g_new0 (GtkAllocation, 1);
    gtk_widget_get_allocation(GTK_WIDGET(widget), allocation);
    int new_pixmap_width = allocation->width;
    int new_pixmap_height = allocation->height;
#else
    int new_pixmap_width = widget->allocation.width;
    int new_pixmap_height = widget->allocation.height;
#endif
    if ((new_pixmap_width > 0) && (new_pixmap_height > 0))
    {
        /* Allocate a new pixmap of the allocated size. */
        if (d->pixmap != NULL)
            cairo_surface_destroy(d->pixmap);
        d->pixmap = cairo_image_surface_create(CAIRO_FORMAT_RGB24, new_pixmap_width, new_pixmap_height);
        cairo_t *cr = cairo_create(d->pixmap);
        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(cr);
	check_cairo_status(cr);
        cairo_destroy(cr);
	check_cairo_surface_status(&d->pixmap);

        /* Compute the horizontal and vertical scale factors, and mark the desktop for redraw. */
#if GTK_CHECK_VERSION(2,18,0)
        d->scale_y = (gfloat) allocation->height / (gfloat) gdk_screen_height();
        d->scale_x = (gfloat) allocation->width  / (gfloat) gdk_screen_width();
#else
        d->scale_y = (gfloat) allocation->height / (gfloat) gdk_screen_height();
        d->scale_x = (gfloat) allocation->width  / (gfloat) gdk_screen_width();
#endif
        desk_set_dirty(d);
     }

    /* Resize to optimal size. */
    gtk_widget_set_size_request(widget,
        (d->pg->plugin->panel->icon_size - BORDER_WIDTH * 2) * d->pg->aspect_ratio,
        d->pg->plugin->panel->icon_size - BORDER_WIDTH * 2);
#if GTK_CHECK_VERSION(2,18,0)
    g_free (allocation);
#endif
    return FALSE;
}

/* Handler for expose_event on drawing area. */
static gboolean desk_expose_event(GtkWidget * widget, GdkEventExpose * event, PagerDesk * d)
{
    GtkStyle * style = gtk_widget_get_style(widget);

    if (d->pixmap != NULL)
    {
        cairo_t *cr = gdk_cairo_create(gtk_widget_get_window(widget));
        gdk_cairo_region(cr, event->region);
        cairo_clip(cr);
        /* Recompute the pixmap if needed. */
        if (d->dirty)
        {
            d->dirty = FALSE;
            PagerPlugin * pg = d->pg;

            /* Erase the pixmap. */
            if (d->pixmap != NULL)
            {
                //GtkWidget * widget = GTK_WIDGET(d->da);
                cairo_t *cr0 = cairo_create(d->pixmap);
                gdk_cairo_set_source_color(cr0,
                    ((d->desktop_number == d->pg->current_desktop)
                        ? &style->dark[GTK_STATE_SELECTED]
                        : &style->dark[GTK_STATE_NORMAL]));
                cairo_paint(cr0);
		check_cairo_status(cr0);
                cairo_destroy(cr0);
            }

            /* Draw tasks onto the pixmap. */
            int j;
            for (j = 0; j < pg->client_count; j++)
                task_update_pixmap(pg->tasks_in_stacking_order[j], d);
        }

        /* Draw the requested part of the pixmap onto the drawing area. */
        GtkAllocation allocation;
        gtk_widget_get_allocation(GTK_WIDGET(widget), &allocation);
        gdk_cairo_set_source_color(cr,
             &style->fg[GTK_WIDGET_STATE(widget)]);
        cairo_set_source_surface(cr, d->pixmap, 0, 0);
        cairo_paint(cr);
	check_cairo_status(cr);
        cairo_destroy(cr);
    }
    return FALSE;
}

/* Handler for "scroll-event" on drawing area. */
static gboolean desk_scroll_event(GtkWidget * widget, GdkEventScroll * event, PagerDesk * d)
{
    /* Compute the new desktop from the scroll direction, wrapping at either extreme. */
    int current_desktop = d->pg->current_desktop;
    if ((event->direction == GDK_SCROLL_UP) || (event->direction == GDK_SCROLL_RIGHT))
    {
        current_desktop += 1;
        if (current_desktop >= d->pg->number_of_desktops)
            current_desktop = 0;
    }
    else
    {
        current_desktop -= 1;
        if (current_desktop < 0)
            current_desktop = d->pg->number_of_desktops - 1;
    }

    /* Ask the window manager to make the new desktop current. */
    Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, current_desktop, 0, 0, 0, 0);
    return TRUE;
}

/* Handler for "button-press-event" on drawing area. */
static gboolean desk_button_press_event(GtkWidget * widget, GdkEventButton * event, PagerDesk * d)
{
    /* Standard right-click handling. */
    if (plugin_button_press_event(widget, event, d->pg->plugin))
        return TRUE;

    /* Ask the window manager to make the new desktop current. */
    Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, d->desktop_number, 0, 0, 0, 0);
    return TRUE;
}

/* Allocate the structure and the graphic elements representing a desktop. */
static void desk_new(PagerPlugin * pg, int desktop_number)
{
    
    /* Allocate and initialize structure. */
    PagerDesk * d = pg->desks[desktop_number] = g_new0(PagerDesk, 1);
    d->pg = pg;
    d->desktop_number = desktop_number;

    /* Allocate drawing area. */
    d->da = gtk_drawing_area_new();

    icon_grid_add(pg->icon_grid, d->da, TRUE);
    gtk_widget_add_events (d->da, GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK);

    /* Connect signals. */
    g_signal_connect(G_OBJECT(d->da), "expose_event", G_CALLBACK(desk_expose_event), (gpointer) d);
    g_signal_connect(G_OBJECT(d->da), "configure_event", G_CALLBACK(desk_configure_event), (gpointer) d);
    g_signal_connect(G_OBJECT(d->da), "scroll-event", G_CALLBACK(desk_scroll_event), (gpointer) d);
    g_signal_connect(G_OBJECT(d->da), "button_press_event", G_CALLBACK(desk_button_press_event), (gpointer) d);

    /* Show the widget. */
    gtk_widget_show(d->da);
}

/* Free the structure representing a desktop. */
static void desk_free(PagerPlugin * pg, int desktop_number)
{
    PagerDesk * d = pg->desks[desktop_number];

    g_signal_handlers_disconnect_by_func(G_OBJECT(d->da), desk_expose_event, d);
    g_signal_handlers_disconnect_by_func(G_OBJECT(d->da), desk_configure_event, d);
    g_signal_handlers_disconnect_by_func(G_OBJECT(d->da), desk_scroll_event, d);
    g_signal_handlers_disconnect_by_func(G_OBJECT(d->da), desk_button_press_event, d);

    icon_grid_remove(pg->icon_grid, d->da);

    if (d->pixmap != NULL)
        cairo_surface_destroy(d->pixmap);

    g_free(d);
}

/*****************************************************************
 * Pager Functions                                               *
 *****************************************************************/

/* Handle PropertyNotify event.
 * http://tronche.com/gui/x/icccm/
 * http://standards.freedesktop.org/wm-spec/wm-spec-1.4.html */
static void pager_property_notify_event(PagerPlugin * pg, XEvent * ev)
{
    /* State may be PropertyNewValue, PropertyDeleted. */
    if (((XPropertyEvent*) ev)->state == PropertyNewValue)
    {
        Atom at = ev->xproperty.atom;
        Window win = ev->xproperty.window;
        if (win != GDK_ROOT_WINDOW())
        {
            /* Look up task structure by X window handle. */
            PagerTask * tk = task_lookup(pg, win);
            if (tk != NULL)
            {
                /* Install an error handler that ignores BadWindow.
                 * We frequently get a PropertyNotify event on deleted windows. */
                XErrorHandler previous_error_handler = XSetErrorHandler(panel_handle_x_error_swallow_BadWindow_BadDrawable);

                /* Dispatch on atom. */
                if (at == a_WM_STATE)   
                {
                    /* Window changed state. */
                    tk->ws = get_wm_state(tk->win);
                    desk_set_dirty_by_win(pg, tk);
                }
                else if (at == a_NET_WM_STATE)
                {
                    /* Window changed EWMH state. */
                    get_net_wm_state(tk->win, &tk->nws);
                    desk_set_dirty_by_win(pg, tk);
                }
                else if (at == a_NET_WM_DESKTOP)
                {
                    /* Window changed desktop.
                     * Mark both old and new desktops for redraw. */
                    desk_set_dirty_by_win(pg, tk);
                    tk->desktop = get_net_wm_desktop(tk->win);
                    desk_set_dirty_by_win(pg, tk);

                XSetErrorHandler(previous_error_handler);
                }
            }
        }
    }
}

/* Handle ConfigureNotify event. */
static void pager_configure_notify_event(PagerPlugin * pg, XEvent * ev)
{
    Window win = ev->xconfigure.window;
    PagerTask * tk = task_lookup(pg, win);
    if (tk != NULL)
    {
        task_get_geometry(tk);
        desk_set_dirty_by_win(pg, tk);
    }
}

/* GDK event filter. */
static GdkFilterReturn pager_event_filter(XEvent * xev, GdkEvent * event, PagerPlugin * pg)
{
    /* Look for PropertyNotify and ConfigureNotify events and update state. */
    if (xev->type == PropertyNotify)
        pager_property_notify_event(pg, xev);
    else if (xev->type == ConfigureNotify)
        pager_configure_notify_event(pg, xev);
    return GDK_FILTER_CONTINUE;
}

/*****************************************************************
 * Netwm/WM Interclient Communication                            *
 *****************************************************************/

/* Handler for "active-window" event from root window listener. */
static void pager_net_active_window(FbEv * ev, PagerPlugin * pg)
{
    Window * focused_window = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_ACTIVE_WINDOW, XA_WINDOW, 0);
    if (focused_window != NULL)
    {
        PagerTask * tk = task_lookup(pg, *focused_window);
        if (tk != pg->focused_task)
        {
            /* Focused task changed.  Redraw both old and new. */
            if (pg->focused_task != NULL)
                desk_set_dirty_by_win(pg, pg->focused_task);
            pg->focused_task = tk;
            if (tk != NULL)
                desk_set_dirty_by_win(pg, tk);
        }
        XFree(focused_window);
    }
    else
    {
        /* Focused task disappeared.  Redraw old. */
        if (pg->focused_task != NULL)
        {
            desk_set_dirty_by_win(pg, pg->focused_task);
            pg->focused_task = NULL;
        }
    }
}

/* Handler for desktop_name event from window manager. */
static void pager_net_desktop_names(FbEv * fbev, PagerPlugin * pg)
{
    /* Get the NET_DESKTOP_NAMES property. */
    int number_of_desktop_names;
    char * * desktop_names;
    desktop_names = get_utf8_property_list(GDK_ROOT_WINDOW(), a_NET_DESKTOP_NAMES, &number_of_desktop_names);

    /* Loop to copy the desktop names to the vector of labels.
     * If there are more desktops than labels, label the extras with a decimal number. */
    int i;
    for (i = 0; ((desktop_names != NULL) && (i < MIN(pg->number_of_desktops, number_of_desktop_names))); i++)
        gtk_widget_set_tooltip_text(pg->desks[i]->da, desktop_names[i]);
    for ( ; i < pg->number_of_desktops; i++)
    {
        char temp[10];
        sprintf(temp, "%d", i + 1);
        gtk_widget_set_tooltip_text(pg->desks[i]->da, temp);
    }

    /* Free the property. */
    if (desktop_names != NULL)
        g_strfreev(desktop_names);
}

/* Handler for "current-desktop" event from root window listener. */
static void pager_net_current_desktop(FbEv * ev, PagerPlugin * pg)
{
    desk_set_dirty(pg->desks[pg->current_desktop]);
    pg->current_desktop = get_net_current_desktop();
    if (pg->current_desktop >= pg->number_of_desktops)
        pg->current_desktop = 0;
    desk_set_dirty(pg->desks[pg->current_desktop]);
}


/* Handler for "number-of-desktops" event from root window listener.
 * Also used to initialize plugin. */
static void pager_net_number_of_desktops(FbEv * ev, PagerPlugin * pg)
{
    /* Get existing values. */
    int number_of_desktops = pg->number_of_desktops;

    /* Get the correct number of desktops. */
    pg->number_of_desktops = get_net_number_of_desktops();
    if (pg->number_of_desktops < 1)
        pg->number_of_desktops = 1;

    /* Reallocate the structure if necessary. */
    if (pg->number_of_desktops > pg->desk_extent)
    {
        PagerDesk * * new_desks = g_new(PagerDesk *, pg->number_of_desktops);
        if (pg->desks != NULL)
        {
            memcpy(new_desks, pg->desks, pg->desk_extent * sizeof(PagerDesk *));
            g_free(pg->desks);
        }
        pg->desks = new_desks;
        pg->desk_extent = pg->number_of_desktops;
    }

    /* Reconcile the current desktop number. */
    pg->current_desktop = get_net_current_desktop();
    if (pg->current_desktop >= pg->number_of_desktops)
        pg->current_desktop = 0;

    /* Reconcile the old and new number of desktops. */
    int difference = pg->number_of_desktops - number_of_desktops;
    if (difference != 0)
    {
        if (difference < 0)
        {
            /* If desktops were deleted, then delete their maps also. */
            int i;
            for (i = pg->number_of_desktops; i < number_of_desktops; i++)
                desk_free(pg, i);
        }
        else
        {
            /* If desktops were added, then create their maps also. */
            int i;
            for (i = number_of_desktops; i < pg->number_of_desktops; i++)
                desk_new(pg, i);
        }
    }

    /* Refresh the client list. */
    pager_net_client_list_stacking(NULL, pg);
}

/* Handler for "net-client-list-stacking" event from root window listener. */
static void pager_net_client_list_stacking(FbEv * ev, PagerPlugin * pg)
{
    /* Get the NET_CLIENT_LIST_STACKING property. */
    Window * client_list = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST_STACKING, XA_WINDOW, &pg->client_count);
    g_free(pg->tasks_in_stacking_order);
    /* g_new returns NULL if if n_structs == 0 */
    pg->tasks_in_stacking_order = g_new(PagerTask *, pg->client_count);

    if (client_list != NULL)
    {
        /* Loop over client list, correlating it with task list.
         * Also generate a vector of task pointers in stacking order. */
        int i;
        for (i = 0; i < pg->client_count; i++)
        {
            /* Search for the window in the task list.  Set up context to do an insert right away if needed. */
            PagerTask * tk_pred = NULL;
            PagerTask * tk_cursor;
            PagerTask * tk = NULL;
            for (tk_cursor = pg->task_list; tk_cursor != NULL; tk_pred = tk_cursor, tk_cursor = tk_cursor->task_flink)
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
            {
                tk->present_in_client_list = TRUE;

                /* If the stacking position changed, redraw the desktop. */
                if (tk->stacking != i)
                {
                    tk->stacking = i;
                    desk_set_dirty_by_win(pg, tk);
                }
            }

            /* Task is not in task list. */
            else
            {
                /* Allocate and initialize new task structure. */
                tk = g_new0(PagerTask, 1);
                tk->present_in_client_list = TRUE;
                tk->win = client_list[i];
                tk->ws = get_wm_state(tk->win);
                tk->desktop = get_net_wm_desktop(tk->win);
                get_net_wm_state(tk->win, &tk->nws);
                get_net_wm_window_type(tk->win, &tk->nwwt);
                task_get_geometry(tk);
                if ( ! FBPANEL_WIN(tk->win))
                    XSelectInput(GDK_DISPLAY(), tk->win, PropertyChangeMask | StructureNotifyMask);
                desk_set_dirty_by_win(pg, tk);

                /* Link the task structure into the task list. */
                if (tk_pred == NULL)
                {
                    tk->task_flink = pg->task_list;
                    pg->task_list = tk;
                }
                else
                {
                    tk->task_flink = tk_pred->task_flink;
                    tk_pred->task_flink = tk;
                }
            }
            pg->tasks_in_stacking_order[i] = tk;
        }
        XFree(client_list);
    }

    /* Remove windows from the task list that are not present in the NET_CLIENT_LIST_STACKING. */
    PagerTask * tk_pred = NULL;
    PagerTask * tk = pg->task_list;
    while (tk != NULL)
    {
        PagerTask * tk_succ = tk->task_flink;
        if (tk->present_in_client_list)
        {
            tk->present_in_client_list = FALSE;
            tk_pred = tk;
        }
        else
        {
            if (tk_pred == NULL)
                pg->task_list = tk_succ;
                else tk_pred->task_flink = tk_succ;
            task_delete(pg, tk, FALSE);
        }
        tk = tk_succ;
    }
}

/* Plugin constructor. */
static int pager_constructor(Plugin * plug, char ** fp)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    PagerPlugin * pg = g_new0(PagerPlugin, 1);
    plug->priv = pg;
    pg->plugin = plug;

    /* Compute aspect ratio of screen image. */
    pg->aspect_ratio = (gfloat) gdk_screen_width() / (gfloat) gdk_screen_height();

    /* Allocate top level widget and set into Plugin widget pointer. */
    plug->pwid = gtk_event_box_new();
#if GTK_CHECK_VERSION(2,18,0)
    gtk_widget_set_has_window(plug->pwid,FALSE);
#else
    GTK_WIDGET_SET_FLAGS(plug->pwid, GTK_NO_WINDOW);
#endif
    gtk_container_set_border_width(GTK_CONTAINER(plug->pwid), 0);

    /* Create an icon grid manager to manage the drawing areas within the container. */
    GtkOrientation bo = (plug->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    pg->icon_grid = icon_grid_new(plug->panel, plug->pwid, bo,
        (plug->panel->icon_size - BORDER_WIDTH * 2) * pg->aspect_ratio,
        plug->panel->icon_size - BORDER_WIDTH * 2,
        1, BORDER_WIDTH,
        plug->panel->height);

    /* Add GDK event filter. */
    gdk_window_add_filter(NULL, (GdkFilterFunc) pager_event_filter, pg);

    /* Connect signals to receive root window events and initialize root window properties. */
    g_signal_connect(G_OBJECT(fbev), "current_desktop", G_CALLBACK(pager_net_current_desktop), (gpointer) pg);
    g_signal_connect(G_OBJECT(fbev), "active_window", G_CALLBACK(pager_net_active_window), (gpointer) pg);
    g_signal_connect(G_OBJECT(fbev), "desktop_names", G_CALLBACK(pager_net_desktop_names), (gpointer) pg);
    g_signal_connect(G_OBJECT(fbev), "number_of_desktops", G_CALLBACK(pager_net_number_of_desktops), (gpointer) pg);
    g_signal_connect(G_OBJECT(fbev), "client_list_stacking", G_CALLBACK(pager_net_client_list_stacking), (gpointer) pg);

    /* Allocate per-desktop structures. */
    pager_net_number_of_desktops(fbev, pg);
    pager_net_desktop_names(fbev, pg);
    return 1;
}

/* Plugin destructor. */
static void pager_destructor(Plugin * p)
{
    PagerPlugin * pg = (PagerPlugin *) p->priv;

    /* Remove GDK event filter. */
    gdk_window_remove_filter(NULL, (GdkFilterFunc) pager_event_filter, pg);

    /* Remove root window signal handlers. */
    g_signal_handlers_disconnect_by_func(G_OBJECT(fbev), pager_net_current_desktop, pg);
    g_signal_handlers_disconnect_by_func(G_OBJECT(fbev), pager_net_active_window, pg);
    g_signal_handlers_disconnect_by_func(G_OBJECT(fbev), pager_net_number_of_desktops, pg);
    g_signal_handlers_disconnect_by_func(G_OBJECT(fbev), pager_net_client_list_stacking, pg);

    /* Deallocate desktop structures. */
    int i;
    for (i = 0; i < pg->number_of_desktops; i += 1)
        desk_free(pg, i);

    /* Deallocate task list. */
    while (pg->task_list != NULL)
        task_delete(pg, pg->task_list, TRUE);

    /* Deallocate all memory. */
    icon_grid_free(pg->icon_grid);
    g_free(pg->tasks_in_stacking_order);
    g_free(pg);
}

/* Callback when panel configuration changes. */
static void pager_panel_configuration_changed(Plugin * p)
{
    /* Reset the icon grid orientation. */
    PagerPlugin * pg = (PagerPlugin *) p->priv;
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    icon_grid_set_geometry(pg->icon_grid, bo,
        (p->panel->icon_size - BORDER_WIDTH * 2) * pg->aspect_ratio,
        p->panel->icon_size - BORDER_WIDTH * 2,
        1, BORDER_WIDTH,
        p->panel->height); 
}

/* Plugin descriptor. */
PluginClass pager_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "pager",
    name : N_("Desktop Pager"),
    version: "1.0",
    description : N_("Simple pager plugin"),

    constructor : pager_constructor,
    destructor  : pager_destructor,
    config : NULL,
    save : NULL,
    panel_configuration_changed : pager_panel_configuration_changed
};
