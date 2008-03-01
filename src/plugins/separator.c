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

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include <glib/gi18n.h>

//#define DEBUG
#include "dbg.h"


static int
separator_constructor(plugin *p, char **fp)
{
    GtkWidget *sep, *eb;
    line s;

    ENTER;
    s.len = 256;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            ERR( "separator: illegal in this context %s\n", s.str);
            RET(0);
        }
    }
    eb = gtk_event_box_new();
    gtk_container_set_border_width(GTK_CONTAINER(eb), 1);
    gtk_widget_show(eb);
    /*
    g_signal_connect(G_OBJECT(eb), "expose_event",
          G_CALLBACK(gtk_widget_queue_draw), NULL);
    */
    sep = p->panel->my_separator_new();
    gtk_widget_show(sep);
    gtk_container_add (GTK_CONTAINER (eb), sep);
    gtk_container_add(GTK_CONTAINER(p->pwid), eb);
    p->priv = eb; /* just to alloc smth */

    RET(1);
}

static void
separator_destructor(plugin *p)
{
    ENTER;
    GtkWidget* eb = GTK_WIDGET((GtkEventBox*)p->priv);
    gtk_widget_destroy( eb );
    RET();
}

static void orientation_changed( plugin* p )
{
    GtkWidget* eb = GTK_WIDGET((GtkEventBox*)p->priv);
    GtkWidget* sep = gtk_bin_get_child( GTK_BIN(eb) );
    if( GTK_IS_VSEPARATOR(sep) ) {
        if( p->panel->orientation == GTK_ORIENTATION_HORIZONTAL )
            return;
    }
    else {
        if( p->panel->orientation == GTK_ORIENTATION_VERTICAL )
            return;
    }
    gtk_widget_destroy( sep );
    sep = p->panel->my_separator_new();
    gtk_widget_show(sep);
    gtk_container_add (GTK_CONTAINER (eb), sep);
}

plugin_class separator_plugin_class = {
    fname: NULL,
    count: 0,

    type : "separator",
    name : N_("Separator"),
    version: "1.0",
    description : N_("Add a separator to the panel"),

    constructor : separator_constructor,
    destructor  : separator_destructor,
    config : NULL,
    save : NULL,
    orientation : orientation_changed
};
