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

#include "dbg.h"

static int separator_constructor(Plugin * p, char ** fp);
static void separator_destructor(Plugin * p);
static void separator_panel_configuration_changed(Plugin * p);

/* Plugin constructor. */
static int separator_constructor(Plugin * p, char ** fp)
{
    /* Load parameters from the configuration file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            ERR( "separator: illegal in this context %s\n", s.str);
            return 0;
        }
    }

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();
#if GTK_CHECK_VERSION(2,18,0)
    gtk_widget_set_has_window(p->pwid, FALSE);
#else
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);
#endif
    gtk_widget_add_events(p->pwid, GDK_BUTTON_PRESS_MASK);
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 1);

    /* Allocate separator as a child of top level. */
    GtkWidget * sep = p->panel->my_separator_new();
    gtk_container_add(GTK_CONTAINER(p->pwid), sep);

    /* Connect signals. */
    g_signal_connect(p->pwid, "button-press-event", G_CALLBACK(plugin_button_press_event), p);

    /* Show the widget and return. */
    gtk_widget_show_all(p->pwid);
    return 1;
}

/* Plugin destructor. */
static void separator_destructor(Plugin * p)
{
}

/* Callback when panel configuration changes. */
static void separator_panel_configuration_changed(Plugin * p)
{
    /* Determine if the orientation changed in a way that requires action. */
    GtkWidget * sep = gtk_bin_get_child(GTK_BIN(p->pwid));
    if (GTK_IS_VSEPARATOR(sep))
    {
        if (p->panel->orientation == GTK_ORIENTATION_HORIZONTAL)
            return;
    }
    else
    {
        if (p->panel->orientation == GTK_ORIENTATION_VERTICAL)
            return;
    }

    /* If the orientation changed, recreate the separator. */
    gtk_widget_destroy(sep);
    sep = p->panel->my_separator_new();
    gtk_widget_show(sep);
    gtk_container_add(GTK_CONTAINER(p->pwid), sep);
}

/* Plugin descriptor. */
PluginClass separator_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "separator",
    name : N_("Separator"),
    version: "1.0",
    description : N_("Add a separator to the panel"),

    constructor : separator_constructor,
    destructor  : separator_destructor,
    config : NULL,
    save : NULL,
    panel_configuration_changed : separator_panel_configuration_changed
};
