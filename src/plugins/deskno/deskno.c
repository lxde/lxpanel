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
    Panel * panel;			/* Back pointer to Panel */
    GtkWidget * label;			/* The label */
    int number_of_desktops;		/* Number of desktops */
    char * * desktop_labels;		/* Vector of desktop labels */
    gboolean bold;			/* User preference: True if bold font */
    gboolean wm_labels;			/* User preference: True to display window manager labels */
} DesknoPlugin;

static gboolean deskno_name_update(GtkWidget * widget, DesknoPlugin * dc);
static void deskno_redraw(GtkWidget * widget, DesknoPlugin * dc);
static gboolean deskno_button_press_event(GtkWidget * widget, GdkEventButton * event, Plugin * p);
static int deskno_constructor(Plugin * p, char ** fp);
static void deskno_destructor(Plugin * p);
static void deskno_apply_configuration(Plugin * p);
static void deskno_configure(Plugin * p, GtkWindow * parent);
static void deskno_save_configuration(Plugin * p, FILE * fp);
static void deskno_panel_configuration_changed(Plugin * p);

/* Handler for current_desktop event from window manager. */
static gboolean deskno_name_update(GtkWidget * widget, DesknoPlugin * dc)
{
    /* Compute and redraw the desktop number. */
    int desktop_number = get_net_current_desktop();
    if (desktop_number < dc->number_of_desktops)
        panel_draw_label_text(dc->panel, dc->label, dc->desktop_labels[desktop_number], dc->bold, TRUE);
    return TRUE;
}

/* Handler for desktop_name and number_of_desktops events from window manager.
 * Also used on a configuration change to get a full redraw. */
static void deskno_redraw(GtkWidget * widget, DesknoPlugin * dc)
{
    /* Get the NET_DESKTOP_NAMES property. */
    dc->number_of_desktops = get_net_number_of_desktops();
    int number_of_desktop_names;
    char * * desktop_names;
    desktop_names = get_utf8_property_list(GDK_ROOT_WINDOW(), a_NET_DESKTOP_NAMES, &number_of_desktop_names);

    /* Reallocate the vector of labels. */
    if (dc->desktop_labels != NULL)
        g_strfreev(dc->desktop_labels);
    dc->desktop_labels = g_new0(gchar *, dc->number_of_desktops + 1);

    /* Loop to copy the desktop names to the vector of labels.
     * If there are more desktops than labels, label the extras with a decimal number. */
    int i = 0;
    if (dc->wm_labels)
        for ( ; ((desktop_names != NULL) && (i < MIN(dc->number_of_desktops, number_of_desktop_names))); i++)
            dc->desktop_labels[i] = g_strdup(desktop_names[i]);
    for ( ; i < dc->number_of_desktops; i++)
        dc->desktop_labels[i] = g_strdup_printf("%d", i + 1);

    /* Free the property. */
    if (desktop_names != NULL)
        g_strfreev(desktop_names);

    /* Redraw the label. */
    deskno_name_update(widget, dc);
}

/* Handler for button-press-event on top level widget. */
static gboolean deskno_button_press_event(GtkWidget * widget, GdkEventButton * event, Plugin * p)
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

    /* Default parameters. */
    dc->wm_labels = TRUE;

    /* Load parameters from the configuration file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "deskno: illegal token %s\n", s.str);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "BoldFont") == 0)
                    dc->bold = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "WMLabels") == 0)
                    dc->wm_labels = str2num(bool_pair, s.t[1], 0);
                else
                    ERR( "deskno: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "deskno: illegal in this context %s\n", s.str);
                return 0;
            }
        }
    }

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();
    gtk_container_set_border_width(GTK_CONTAINER (p->pwid), 1);

    /* Allocate label widget and add to top level. */
    dc->label = gtk_label_new(NULL);
    gtk_container_add(GTK_CONTAINER(p->pwid), dc->label);

    /* Connect signals.  Note use of window manager event object. */
    g_signal_connect(p->pwid, "button_press_event", G_CALLBACK(deskno_button_press_event), p);
    g_signal_connect(G_OBJECT(fbev), "current_desktop", G_CALLBACK(deskno_name_update), (gpointer) dc);
    g_signal_connect(G_OBJECT(fbev), "desktop_names", G_CALLBACK(deskno_redraw), (gpointer) dc);
    g_signal_connect(G_OBJECT(fbev), "number_of_desktops", G_CALLBACK(deskno_redraw), (gpointer) dc);

    /* Initialize value and show the widget. */
    deskno_redraw(NULL, dc);
    gtk_widget_show_all(p->pwid);
    return 1;
}

/* Plugin destructor. */
static void deskno_destructor(Plugin * p)
{
    DesknoPlugin * dc = (DesknoPlugin *) p->priv;

    /* Disconnect signal from window manager event object. */
    g_signal_handlers_disconnect_by_func(G_OBJECT(fbev), deskno_name_update, dc);

    /* Deallocate all memory. */
    if (dc->desktop_labels != NULL)
        g_strfreev(dc->desktop_labels);
    g_free(dc);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static void deskno_apply_configuration(Plugin * p)
{
    DesknoPlugin * dc = (DesknoPlugin *) p->priv;
    deskno_redraw(NULL, dc);
}

/* Callback when the configuration dialog is to be shown. */
static void deskno_configure(Plugin * p, GtkWindow * parent)
{
    DesknoPlugin * dc = (DesknoPlugin *) p->priv;
    GtkWidget * dlg = create_generic_config_dlg(
        _(p->class->name),
        GTK_WIDGET(parent),
        (GSourceFunc) deskno_apply_configuration, (gpointer) p,
        _("Bold font"), &dc->bold, CONF_TYPE_BOOL,
        _("Display desktop names"), &dc->wm_labels, CONF_TYPE_BOOL,
        NULL);
    gtk_widget_set_size_request(GTK_WIDGET(dlg), 400, -1);	/* Improve geometry */
    gtk_window_present(GTK_WINDOW(dlg));
}

/* Callback when the configuration is to be saved. */
static void deskno_save_configuration(Plugin * p, FILE * fp)
{
    DesknoPlugin * dc = (DesknoPlugin *) p->priv;
    lxpanel_put_int(fp, "BoldFont", dc->bold);
    lxpanel_put_int(fp, "WMLabels", dc->wm_labels);
}

/* Callback when panel configuration changes. */
static void deskno_panel_configuration_changed(Plugin * p)
{
    DesknoPlugin * dc = (DesknoPlugin *) p->priv;
    deskno_name_update(NULL, dc);
}

/* Plugin descriptor. */
PluginClass deskno_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "deskno",
    name : N_("Desktop Number / Workspace Name"),
    version: "0.6",
    description : N_("Display workspace number, by cmeury@users.sf.net"),

    constructor : deskno_constructor,
    destructor  : deskno_destructor,
    config : deskno_configure,
    save : deskno_save_configuration,
    panel_configuration_changed : deskno_panel_configuration_changed

};
