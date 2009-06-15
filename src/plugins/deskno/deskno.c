/**
 * Desktop number plugin to lxpanel
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

// reused dclock.c and variables from pager.c
// 11/23/04 by cmeury

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

/* Private context for desktop number plugin. */
typedef struct {
    Panel *panel;			/* Back pointer to Panel */
    GtkWidget *label;			/* Label */
} DesknoPlugin;

static gboolean name_update(GtkWidget * widget, DesknoPlugin * dc);
static gboolean clicked(GtkWidget * widget, GdkEventButton * event, Plugin * p);
static int deskno_constructor(Plugin * p, char ** fp);
static void deskno_destructor(Plugin * p);

/* Handler for current_desktop event from window manager. */
static gboolean name_update(GtkWidget * widget, DesknoPlugin * dc)
{
    /* Compute and redraw the desktop number. */
    char buffer[128];
    g_snprintf(buffer, sizeof(buffer), "<span color=\"#%06x\"><b>%d</b></span>",
        ((dc->panel->usefontcolor) ? gcolor2rgb24(&dc->panel->gfontcolor) : 0), get_net_current_desktop() + 1);
    gtk_label_set_markup(GTK_LABEL(dc->label), buffer);
    return TRUE;
}

/* Handler for button-press-event on top level widget. */
static gboolean clicked(GtkWidget * widget, GdkEventButton * event, Plugin * p)
{
    /* Standard left-click handling. */
    if (plugin_button_press_event(widget, event, p))
        return TRUE;

    /* Right-click goes to next desktop, wrapping around to first. */
    int desknum = get_net_current_desktop();
    int desks = get_net_number_of_desktops();
    int newdesk = desknum + 1;
    if (newdesk >= desks)
        newdesk = 0;

    /* Ask the window manager to make the new desktop current. */
    Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, newdesk, 0, 0, 0, 0);
    return TRUE;
}

/* Plugin constructor. */
static int deskno_constructor(Plugin * p, char ** fp)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    DesknoPlugin * dc = g_new0(DesknoPlugin, 1);
    g_return_val_if_fail(dc != NULL, 0);
    p->priv = dc;
    dc->panel = p->panel;

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();
    gtk_container_set_border_width(GTK_CONTAINER (p->pwid), 1);

    /* Allocate label widget and add to top level. */
    dc->label = gtk_label_new(NULL);
    gtk_container_add(GTK_CONTAINER(p->pwid), dc->label);

    /* Connect signals.  Note use of window manager event object. */
    g_signal_connect(p->pwid, "button_press_event", G_CALLBACK(clicked), p);
    g_signal_connect(G_OBJECT(fbev), "current_desktop", G_CALLBACK(name_update), (gpointer) dc);

    /* Initialize value and show the widget. */
    name_update(NULL, dc);
    gtk_widget_show_all(p->pwid);
    return 1;
}

/* Plugin destructor. */
static void deskno_destructor(Plugin * p)
{
    DesknoPlugin * dc = (DesknoPlugin *) p->priv;

    /* Disconnect signal from window manager event object. */
    g_signal_handlers_disconnect_by_func(G_OBJECT(fbev), name_update, dc);
    g_free(dc);
}

/* Plugin descriptor. */
PluginClass deskno_plugin_class = {

    type : "deskno",
    name : N_("Desktop No / Workspace Name"),
    version: "0.6",
    description : N_("Display workspace number, by cmeury@users.sf.net"),

    constructor : deskno_constructor,
    destructor  : deskno_destructor,
    config : NULL,
    save : NULL
};
