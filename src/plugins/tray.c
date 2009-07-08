/**
 * System tray plugin to lxpanel
 *
 * Copyright (c) 2008 LxDE Developers, see the file AUTHORS for details.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

/** Contains code adapted from na-tray-manager.c
 * Copyright (C) 2002 Anders Carlsson <andersca@gnu.org>
 * Copyright (C) 2003-2006 Vincent Untz */

#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "bg.h"
#include "icon-grid.h"

/* Standards reference:  http://standards.freedesktop.org/systemtray-spec/ */

/* Protocol constants. */
#define SYSTEM_TRAY_REQUEST_DOCK    0
#define SYSTEM_TRAY_BEGIN_MESSAGE   1
#define SYSTEM_TRAY_CANCEL_MESSAGE  2

#define SYSTEM_TRAY_ORIENTATION_HORZ 0
#define SYSTEM_TRAY_ORIENTATION_VERT 1

//#define DEBUG
#include "dbg.h"

struct _balloon_message;
struct _tray_client;
struct _tray_plugin;

/* Representative of a balloon message. */
typedef struct _balloon_message {
    struct _balloon_message * flink;		/* Forward link */
    Window window;				/* X window ID */
    long timeout;				/* Time in milliseconds to display message; 0 if no timeout */
    long length;				/* Message string length */
    long id;					/* Client supplied unique message ID */
    long remaining_length;			/* Remaining length expected of incomplete message */
    char * string;				/* Message string */
} BalloonMessage;

/* Representative of a tray client. */
typedef struct _tray_client {
    struct _tray_client * client_flink;		/* Forward link to next task in X window ID order */
    struct _tray_plugin * tr;			/* Back pointer to tray plugin */
    Window window;				/* X window ID */
    GtkWidget * socket;				/* Socket */
    gboolean request_dock_finished;		/* True if request dock finished */
} TrayClient;

/* Private context for system tray plugin. */
typedef struct _tray_plugin {
    Plugin * plugin;				/* Back pointer to Plugin */
    IconGrid * icon_grid;			/* Icon grid to manage tray presentation */
    TrayClient * client_list;			/* List of tray clients */
    BalloonMessage * incomplete_messages;	/* List of balloon messages for which we are awaiting data */
    BalloonMessage * messages;			/* List of balloon messages actively being displayed or waiting to be displayed */
    GtkWidget * balloon_message_popup;		/* Popup showing balloon message */
    guint balloon_message_timer;		/* Timer controlling balloon message */
    GtkWidget * invisible;			/* Invisible window that holds manager selection */
    GdkAtom selection_atom;			/* Atom for _NET_SYSTEM_TRAY_S%d */
} TrayPlugin;

static TrayClient * client_lookup(TrayPlugin * tr, Window win);
static void client_delete(TrayPlugin * tr, TrayClient * tc);
static void balloon_message_free(BalloonMessage * message);
static void balloon_message_advance(TrayPlugin * tr, gboolean destroy_timer, gboolean display_next);
static gboolean balloon_message_activate_event(GtkWidget * widget, GdkEventButton * event, TrayPlugin * tr);
static gboolean balloon_message_timeout(TrayPlugin * tr);
static void balloon_message_display(TrayPlugin * tr, BalloonMessage * msg);
static void balloon_message_queue(TrayPlugin * tr, BalloonMessage * msg);
static void balloon_incomplete_message_remove(TrayPlugin * tr, Window window, gboolean all_ids, long id);
static void balloon_message_remove(TrayPlugin * tr, Window window, gboolean all_ids, long id);
static GdkFilterReturn balloon_message_data_event(GdkXEvent * xev, GdkEvent * event, gpointer data);
static void balloon_message_begin_event(TrayPlugin * tr, XClientMessageEvent * xevent);
static void balloon_message_cancel_event(TrayPlugin * tr, XClientMessageEvent * xevent);
static gboolean trayclient_plug_removed(GtkWidget * socket, TrayPlugin * tr);
static void trayclient_realized(GtkWidget * widget, gpointer user_data);
static gboolean trayclient_expose_event(GtkWidget * widget, GdkEventExpose * event, gpointer user_data);
static void trayclient_request_dock(TrayPlugin * tr, XClientMessageEvent * xevent);
static void tray_unmanage_selection(TrayPlugin * tr);
static int tray_constructor(Plugin * p, char ** fp);
static void tray_destructor(Plugin * p);
static void tray_panel_configuration_changed(Plugin * p);

/* Look up a client in the client list. */
static TrayClient * client_lookup(TrayPlugin * tr, Window window)
{
    TrayClient * tc;
    for (tc = tr->client_list; tc != NULL; tc = tc->client_flink)
        {
        if (tc->window == window)
	    return tc;
        if (tc->window > window)
            break;
        }
    return NULL;
}

/* Delete a client. */
static void client_delete(TrayPlugin * tr, TrayClient * tc)
{
    if (tr->client_list == tc)
        tr->client_list = tc->client_flink;
    else
    {
        /* Locate the task and its predecessor in the list and then remove it.  For safety, ensure it is found. */
        TrayClient * tc_pred = NULL;
        TrayClient * tc_cursor;
        for (
          tc_cursor = tr->client_list;
          ((tc_cursor != NULL) && (tc_cursor != tc));
          tc_pred = tc_cursor, tc_cursor = tc_cursor->client_flink) ;
          if (tc_cursor == tc)
              tc_pred->client_flink = tc->client_flink;
    }

    /* Deallocate the client structure. */
    g_free(tc);
}

/*** Balloon message display ***/

/* Free a balloon message structure. */
static void balloon_message_free(BalloonMessage * message)
{
    g_free(message->string);
    g_free(message);
}

/* General code to deactivate a message and optionally display the next.
 * This is used in three scenarios: balloon clicked, timeout expired, destructor. */
static void balloon_message_advance(TrayPlugin * tr, gboolean destroy_timer, gboolean display_next)
{
    /* Remove the message from the queue. */
    BalloonMessage * msg = tr->messages;
    tr->messages = msg->flink;

    /* Cancel the timer, if set.  This is not done when the timer has expired. */
    if ((destroy_timer) && (tr->balloon_message_timer != 0))
        g_source_remove(tr->balloon_message_timer);
    tr->balloon_message_timer = 0;

    /* Destroy the widget. */
    if (tr->balloon_message_popup != NULL)
        gtk_widget_destroy(tr->balloon_message_popup);
    tr->balloon_message_popup = NULL;

    /* Free the message. */
    balloon_message_free(msg);

    /* If there is another message waiting in the queue, display it.  This is not done in the destructor. */
    if ((display_next) && (tr->messages != NULL))
        balloon_message_display(tr, tr->messages);
}

/* Handler for "button-press-event" from balloon message popup menu item. */
static gboolean balloon_message_activate_event(GtkWidget * widget, GdkEventButton * event, TrayPlugin * tr)
{
    balloon_message_advance(tr, TRUE, TRUE);
    return TRUE;
}

/* Timer expiration for balloon message. */
static gboolean balloon_message_timeout(TrayPlugin * tr)
{
    balloon_message_advance(tr, FALSE, TRUE);
    return FALSE;
}

/* Create the graphic elements to display a balloon message. */
static void balloon_message_display(TrayPlugin * tr, BalloonMessage * msg)
{
    /* Create a window and an item containing the text. */
    tr->balloon_message_popup = gtk_window_new(GTK_WINDOW_POPUP);
    GtkWidget * balloon_text = gtk_label_new(msg->string);
    gtk_label_set_line_wrap(GTK_LABEL (balloon_text), TRUE);
    gtk_misc_set_alignment(GTK_MISC(balloon_text), 0.5, 0.5);
    gtk_widget_show(balloon_text);
    gtk_container_add(GTK_CONTAINER(tr->balloon_message_popup), balloon_text);
    gtk_container_set_border_width(GTK_CONTAINER(tr->balloon_message_popup), 4);

    /* Connect signals.  Clicking the popup dismisses it and displays the next message, if any. */
    gtk_widget_add_events(tr->balloon_message_popup, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(tr->balloon_message_popup, "button_press_event", G_CALLBACK(balloon_message_activate_event), (gpointer) tr);

    /* Get the allocation of the popup menu. */
    GtkRequisition popup_req;
    gtk_widget_size_request(GTK_WIDGET(tr->balloon_message_popup), &popup_req);

    /* Compute the desired position in screen coordinates near the tray plugin. */
    int x;
    int y;
    plugin_popup_set_position_helper(tr->plugin, tr->plugin->pwid, tr->balloon_message_popup, &popup_req, &x, &y);

    /* Push onscreen. */
    int screen_width = gdk_screen_width();
    int screen_height = gdk_screen_height();
    if ((x + popup_req.width) > screen_width)
        x -= (x + popup_req.width) - screen_width;
    if ((y + popup_req.height) > screen_height)
    y -= (y + popup_req.height) - screen_height;

    /* Show the popup. */
    gtk_window_move(GTK_WINDOW(tr->balloon_message_popup), x, y);
    gtk_widget_show(tr->balloon_message_popup);

    /* Set a timer, if the client specified one.  Both are in units of milliseconds. */
    if (msg->timeout != 0)
        tr->balloon_message_timer = g_timeout_add(msg->timeout, (GSourceFunc) balloon_message_timeout, tr);
}

/* Add a balloon message to the tail of the message queue.  If it is the only element, display it immediately. */
static void balloon_message_queue(TrayPlugin * tr, BalloonMessage * msg)
{
    if (tr->messages == NULL)
    {
        tr->messages = msg;
        balloon_message_display(tr, msg);
    }
    else
    {
        BalloonMessage * msg_pred;
        for (msg_pred = tr->messages; ((msg_pred != NULL) && (msg_pred->flink != NULL)); msg_pred = msg_pred->flink) ;
        if (msg_pred != NULL)
            msg_pred->flink = msg;
    }
}

/* Remove an incomplete message from the queue, selected by window and optionally also client's ID.
 * Used in two scenarios: client issues CANCEL (ID significant), client plug removed (ID don't care). */
static void balloon_incomplete_message_remove(TrayPlugin * tr, Window window, gboolean all_ids, long id)
{
    BalloonMessage * msg_pred = NULL;
    BalloonMessage * msg = tr->incomplete_messages;
    while (msg != NULL)
    {
        /* Establish successor in case of deletion. */
        BalloonMessage * msg_succ = msg->flink;

        if ((msg->window == window) && ((all_ids) || (msg->id == id)))
        {
            /* Found a message matching the criteria.  Unlink and free it. */
            if (msg_pred == NULL)
                tr->incomplete_messages = msg->flink;
            else
                msg_pred->flink = msg->flink;
            balloon_message_free(msg);
        }
        else
            msg_pred = msg;

        /* Advance to successor. */
        msg = msg_succ;
    }
}

/* Remove a message from the message queue, selected by window and optionally also client's ID.
 * Used in two scenarios: client issues CANCEL (ID significant), client plug removed (ID don't care). */
static void balloon_message_remove(TrayPlugin * tr, Window window, gboolean all_ids, long id)
{
    BalloonMessage * msg_pred = NULL;
    BalloonMessage * msg_head = tr->messages;
    BalloonMessage * msg = msg_head;
    while (msg != NULL)
    {
        /* Establish successor in case of deletion. */
        BalloonMessage * msg_succ = msg->flink;

        if ((msg->window == window) && ((all_ids) || (msg->id == id)))
        {
            /* Found a message matching the criteria. */
            if (msg_pred == NULL)
            {
                /* The message is at the queue head, so is being displayed.  Stop the display. */
                tr->messages = msg->flink;
                if (tr->balloon_message_timer != 0)
                {
                    g_source_remove(tr->balloon_message_timer);
                    tr->balloon_message_timer = 0;
                }
                if (tr->balloon_message_popup != NULL)
                {
                    gtk_widget_destroy(tr->balloon_message_popup);
                    tr->balloon_message_popup = NULL;
                }
            }
            else
                msg_pred->flink = msg->flink;

            /* Free the message. */
            balloon_message_free(msg);
        }
        else
            msg_pred = msg;

        /* Advance to successor. */
        msg = msg_succ;
    }

    /* If there is a new message head, display it now. */
    if ((tr->messages != msg_head) && (tr->messages != NULL))
        balloon_message_display(tr, tr->messages);
}

/*** Event interfaces ***/

/* Handle a balloon message _NET_SYSTEM_TRAY_MESSAGE_DATA event. */
static GdkFilterReturn balloon_message_data_event(GdkXEvent * xev, GdkEvent * event, gpointer data)
{
  int                  len;

    XClientMessageEvent * xevent = (XClientMessageEvent *) xev;
    TrayPlugin * tr = data;

    /* Look up the pending message in the list. */
    BalloonMessage * msg_pred = NULL;
    BalloonMessage * msg;
    for (msg = tr->incomplete_messages; msg != NULL; msg_pred = msg, msg = msg->flink)
    {
        if (xevent->window == msg->window)
        {
            /* Append the message segment to the message. */
            int length = MIN(msg->remaining_length, 20);
            memcpy((msg->string + msg->length - msg->remaining_length), &xevent->data, length);
            msg->remaining_length -= length;

            /* If the message has been completely collected, display it. */
            if (msg->remaining_length == 0)
            {
                /* Unlink the message from the structure. */
                if (msg_pred == NULL)
                    tr->incomplete_messages = msg->flink;
                else
                    msg_pred->flink = msg->flink;

                TrayClient * client = client_lookup(tr, msg->window);
                if (client != NULL)
                    balloon_message_queue(tr, msg);
                else
                    balloon_message_free(msg);
            }
            break;
        }
    }
    return GDK_FILTER_REMOVE;
}

/* Handle a balloon message SYSTEM_TRAY_BEGIN_MESSAGE event. */
static void balloon_message_begin_event(TrayPlugin * tr, XClientMessageEvent * xevent)
{
    TrayClient * client = client_lookup(tr, xevent->window);
    if (client != NULL)
    {
        /* Check if the message ID already exists. */
        balloon_incomplete_message_remove(tr, xevent->window, FALSE, xevent->data.l[4]);

        /* Allocate a BalloonMessage structure describing the message. */
        BalloonMessage * msg = g_new0(BalloonMessage, 1);
        msg->window = xevent->window;
        msg->timeout = xevent->data.l[2];
        msg->length = xevent->data.l[3];
        msg->id = xevent->data.l[4];
        msg->remaining_length = msg->length;
        msg->string = g_new0(char, msg->length + 1);

        /* Message length of 0 indicates that no follow-on messages will be sent. */
        if (msg->length == 0)
            balloon_message_queue(tr, msg);
        else
        {
            /* Add the new message to the queue to await its message text. */
            msg->flink = tr->incomplete_messages;
            tr->incomplete_messages = msg;
        }
    }
}

/* Handle a balloon message SYSTEM_TRAY_CANCEL_MESSAGE event. */
static void balloon_message_cancel_event(TrayPlugin * tr, XClientMessageEvent * xevent)
{
    /* Remove any incomplete messages on this window with the specified ID. */
    balloon_incomplete_message_remove(tr, xevent->window, TRUE, 0);

    /* Remove any displaying or waiting messages on this window with the specified ID. */
    TrayClient * client = client_lookup(tr, xevent->window);
    if (client != NULL)
        balloon_message_remove(tr, xevent->window, FALSE, xevent->data.l[2]);
}

/* Handler for "plug-removed" event on socket widget. */
static gboolean trayclient_plug_removed(GtkWidget * socket, TrayPlugin * tr)
{
    Window win = (Window) g_object_get_data(G_OBJECT(socket), "client-window");
    g_object_set_data(G_OBJECT(socket), "client-window", NULL);

    TrayClient * tc = client_lookup(tr, win);
    if (tc != NULL)
    {
        balloon_incomplete_message_remove(tr, win, TRUE, 0);
        balloon_message_remove(tr, win, TRUE, 0);
        client_delete(tr, tc);
    }
    icon_grid_remove(tr->icon_grid, socket);

    /* This destroys the socket. */
    return FALSE;
}

/* Handler for "realized" event on socket widget. */
static void trayclient_realized(GtkWidget * widget, gpointer user_data)
{
   if ( ! GTK_WIDGET_NO_WINDOW(widget))
        gdk_window_set_back_pixmap(widget->window, NULL, TRUE);
}

/* Handler for "expose_event" event on socket widget. */
static gboolean trayclient_expose_event(GtkWidget * widget, GdkEventExpose * event, gpointer user_data)
{
    gdk_window_clear_area(widget->window, event->area.x, event->area.y, event->area.width, event->area.height);
    return FALSE;
}

/* Handler for request dock message. */
static void trayclient_request_dock(TrayPlugin * tr, XClientMessageEvent * xevent)
{
    /* Search for the window in the client list.  Set up context to do an insert right away if needed. */
    TrayClient * tc_pred = NULL;
    TrayClient * tc_cursor;
    for (tc_cursor = tr->client_list; tc_cursor != NULL; tc_pred = tc_cursor, tc_cursor = tc_cursor->client_flink)
    {
        if (tc_cursor->window == xevent->data.l[2])
            return;		/* We already got this notification earlier, ignore this one. */
        if (tc_cursor->window > xevent->data.l[2])
            break;
    }

    /* Allocate and initialize new client structure. */
    TrayClient * tc = g_new0(TrayClient, 1);
    tc->window = xevent->data.l[2];
    tc->tr = tr;

    /* Allocate a socket.  This is the tray side of the Xembed connection. */
    tc->socket = gtk_socket_new();
    g_object_set_data(G_OBJECT(tc->socket), "client-window", GINT_TO_POINTER(xevent->data.l[2]));

    /* Link the client structure into the client list. */
    if (tc_pred == NULL)
    {
        tc->client_flink = tr->client_list;
        tr->client_list = tc;
    }
    else
    {
        tc->client_flink = tc_pred->client_flink;
        tc_pred->client_flink = tc;
    }

    gtk_widget_set_app_paintable(tc->socket, TRUE);
    gtk_widget_set_double_buffered(tc->socket, FALSE);
    g_signal_connect(tc->socket, "realize", G_CALLBACK(trayclient_realized), NULL);
    g_signal_connect(tc->socket, "expose_event", G_CALLBACK(trayclient_expose_event), NULL);
    g_signal_connect(tc->socket, "plug_removed", G_CALLBACK(trayclient_plug_removed), tc->tr);

    icon_grid_add(tr->icon_grid, tc->socket, TRUE);
    
    /* Connect the socket to the plug. */
    gtk_socket_add_id(GTK_SOCKET(tc->socket), tc->window);
}

/* GDK event filter. */
static GdkFilterReturn tray_window_filter(GdkXEvent * xev, GdkEvent * event, gpointer data)
{
    XEvent * xevent = (GdkXEvent *) xev;
    TrayPlugin * tr = data;

    if (xevent->type == ClientMessage)
    {
        /* We handle the REQUEST_DOCK client message here.
         * See also tray_client_message_opcode_filter. */
        if ((xevent->xclient.message_type == a_NET_SYSTEM_TRAY_OPCODE)
        && (xevent->xclient.data.l[1] == SYSTEM_TRAY_REQUEST_DOCK))
        {
            trayclient_request_dock(tr, (XClientMessageEvent *) xevent);
            return GDK_FILTER_REMOVE;
        }
    }

    else if (xevent->type == SelectionClear)
    {
        /* We lost the selection, which should not happen. */
        tray_unmanage_selection(tr);
    }

  return GDK_FILTER_CONTINUE;
}

/* Filter for _NET_SYSTEM_TRAY_OPCODE (SYSTEM_TRAY_BEGIN_MESSAGE and SYSTEM_TRAY_CANCEL_MESSAGE). */
static GdkFilterReturn tray_client_message_opcode_filter(GdkXEvent * xev, GdkEvent * event, gpointer data)
{
    XClientMessageEvent * xevent  = (XClientMessageEvent *) xev;
    TrayPlugin * tr = data;

    /* Dispatch on the message. */
    switch (xevent->data.l[1])
    {
        case SYSTEM_TRAY_REQUEST_DOCK:
            /* Ignore this one since we don't know on which window this was received
             * and so we can't know for which screen this is.  It will be handled
             * in tray_window_filter since we also receive it there. */
            break;

        case SYSTEM_TRAY_BEGIN_MESSAGE:
            balloon_message_begin_event(tr, xevent);
            return GDK_FILTER_REMOVE;

        case SYSTEM_TRAY_CANCEL_MESSAGE:
            balloon_message_cancel_event(tr, xevent);
            return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
}

/* Delete the selection on the invisible window. */
static void tray_unmanage_selection(TrayPlugin * tr)
{
    if (tr->invisible != NULL)
    {
        GtkWidget * invisible = tr->invisible;
        GdkDisplay * display = gtk_widget_get_display(invisible);
        if (gdk_selection_owner_get_for_display(display, tr->selection_atom) == invisible->window)
        {
            guint32 timestamp = gdk_x11_get_server_time(invisible->window);
            gdk_selection_owner_set_for_display(
                display,
                NULL,
                tr->selection_atom,
                timestamp,
                TRUE);
        }

        /* Remove the event filters.
         * At this time, there is no way to remove the client message filters. */
        gdk_window_remove_filter(invisible->window, tray_window_filter, tr);

        /*Destroy the invisible window. */
        tr->invisible = NULL;
        gtk_widget_destroy(invisible);
        g_object_unref(G_OBJECT(invisible));
    }
}

/* Plugin constructor. */
static int tray_constructor(Plugin * p, char ** fp)
{
    /* Read configuration from file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            ERR("tray: illegal in this context %s\n", s.str);
            return 0;
        }
    }

    /* Allocate plugin context and set into Plugin private data pointer. */
    TrayPlugin * tr = g_new0(TrayPlugin, 1);
    p->priv = tr;
    tr->plugin = p;

    /* Get the screen and display. */
    GdkScreen * screen = gtk_widget_get_screen(GTK_WIDGET(p->panel->topgwin));
    Screen * xscreen = GDK_SCREEN_XSCREEN(screen);
    GdkDisplay * display = gdk_screen_get_display(screen);

    /* Create the selection atom.  This has the screen number in it, so cannot be done ahead of time. */
    char * selection_atom_name = g_strdup_printf("_NET_SYSTEM_TRAY_S%d", gdk_screen_get_number(screen));
    Atom selection_atom = gdk_x11_get_xatom_by_name_for_display(display, selection_atom_name);
    tr->selection_atom = gdk_atom_intern(selection_atom_name, FALSE);
    g_free(selection_atom_name);

    /* If the selection is already owned, there is another tray running. */
    if (XGetSelectionOwner(GDK_DISPLAY_XDISPLAY(display), selection_atom) != None)
    {
        ERR("tray: another systray already running\n");
        return 1;
    }

    /* Create an invisible window to hold the selection. */
    GtkWidget * invisible = gtk_invisible_new_for_screen(screen);
    gtk_widget_realize(invisible);
    gtk_widget_add_events(invisible, GDK_PROPERTY_CHANGE_MASK | GDK_STRUCTURE_MASK);

    /* Try to claim the _NET_SYSTEM_TRAY_Sn selection. */
    guint32 timestamp = gdk_x11_get_server_time(invisible->window);
    if (gdk_selection_owner_set_for_display(
        display,
        invisible->window,
        tr->selection_atom,
        timestamp,
        TRUE))
    {
        /* Send MANAGER client event (ICCCM). */
        XClientMessageEvent xev;
        xev.type = ClientMessage;
        xev.window = RootWindowOfScreen(xscreen);
        xev.message_type = a_MANAGER;
        xev.format = 32;
        xev.data.l[0] = timestamp;
        xev.data.l[1] = selection_atom;
        xev.data.l[2] = GDK_WINDOW_XWINDOW(invisible->window);
        xev.data.l[3] = 0;    /* manager specific data */
        xev.data.l[4] = 0;    /* manager specific data */
        XSendEvent(GDK_DISPLAY_XDISPLAY(display), RootWindowOfScreen(xscreen), False, StructureNotifyMask, (XEvent *) &xev);
        
        /* Set the orientation property.
         * We always set "horizontal" since even vertical panels are designed to use a lot of width. */
        gulong data = SYSTEM_TRAY_ORIENTATION_HORZ;
        XChangeProperty(
            GDK_DISPLAY_XDISPLAY(display),
            GDK_WINDOW_XWINDOW(invisible->window),
            a_NET_SYSTEM_TRAY_ORIENTATION,
            XA_CARDINAL, 32,
            PropModeReplace,
            (guchar *) &data, 1);

        /* Add GDK window filter to handle SYSTEM_TRAY_REQUEST_DOCK and SelectionClear. */
        gdk_window_add_filter(invisible->window, tray_window_filter, tr);

        /* Add filter for _NET_SYSTEM_TRAY_OPCODE (SYSTEM_TRAY_BEGIN_MESSAGE and SYSTEM_TRAY_CANCEL_MESSAGE). */
        GdkAtom opcode_atom = gdk_atom_intern("_NET_SYSTEM_TRAY_OPCODE", FALSE);
        gdk_display_add_client_message_filter(display, opcode_atom, tray_client_message_opcode_filter, tr);

        /* Add filter for _NET_SYSTEM_TRAY_MESSAGE_DATA. */
        GdkAtom message_data_atom = gdk_atom_intern("_NET_SYSTEM_TRAY_MESSAGE_DATA", FALSE);
        gdk_display_add_client_message_filter(display, message_data_atom, balloon_message_data_event, tr);

        tr->invisible = invisible;
        g_object_ref(G_OBJECT(tr->invisible));
    }
    else
    {
        gtk_widget_destroy(invisible);
        g_printerr("tray: System tray didn't get the system tray manager selection\n");
        return 0;
    }
        
    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);
    gtk_widget_set_name(p->pwid, "tray");	/* this hack is required for plugin_widget_set_background() */
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 1);

    /* Create an icon grid to manage the container. */
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    tr->icon_grid = icon_grid_new(p->panel, p->pwid, bo, PANEL_ICON_SIZE, PANEL_ICON_SIZE, 3, 0, p->panel->height);
    return 1;
}

/* Plugin destructor. */
static void tray_destructor(Plugin * p)
{
    TrayPlugin * tr = (TrayPlugin *) p->priv;

    /* Make sure we drop the manager selection. */
    tray_unmanage_selection(tr);

    /* Deallocate incomplete messages. */
    while (tr->incomplete_messages != NULL)
    {
        BalloonMessage * msg_succ = tr->incomplete_messages->flink;
        balloon_message_free(tr->incomplete_messages);
        tr->incomplete_messages = msg_succ;
    }

    /* Terminate message display and deallocate messages. */
    while (tr->messages != NULL)
        balloon_message_advance(tr, TRUE, FALSE);

    /* Deallocate client list. */
    while (tr->client_list != NULL)
        client_delete(tr, tr->client_list);

    /* Deallocate memory. */
    icon_grid_free(tr->icon_grid);
    g_free(tr);
}

/* Callback when panel configuration changes. */
static void tray_panel_configuration_changed(Plugin * p)
{
    /* Set orientation into the icon grid. */
    TrayPlugin * tr = (TrayPlugin *) p->priv;
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    icon_grid_set_orientation(tr->icon_grid, bo, p->panel->height);
}

/* Plugin descriptor. */
PluginClass tray_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "tray",
    name : N_("System Tray"),
    version: "1.0",
    description : N_("System tray"),

    /* Set a flag to identify the system tray.  It is special in that only one per system can exist. */
    one_per_system : TRUE,

    constructor : tray_constructor,
    destructor  : tray_destructor,
    config : NULL,
    save : NULL,
    panel_configuration_changed : tray_panel_configuration_changed
};
