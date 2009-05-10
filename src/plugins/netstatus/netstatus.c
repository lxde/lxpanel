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

#include <gtk/gtk.h>
#include <stdlib.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#include "netstatus-icon.h"
#include "netstatus-dialog.h"

typedef struct {
    char *iface;
    char *config_tool;
    GtkWidget *mainw;
    GtkWidget *dlg;
} netstatus;


static void
netstatus_destructor(Plugin *p)
{
    netstatus *ns = (netstatus *)p->priv;

    ENTER;
    /* The widget is destroyed in plugin_stop().
    gtk_widget_destroy(ns->mainw);
    */
    g_free( ns->iface );
    g_free( ns->config_tool );
    g_free(ns);
    RET();
}

static void on_response( GtkDialog* dlg, gint response, netstatus *ns )
{
    const char* iface;
    switch( response )
    {
        case GTK_RESPONSE_CLOSE:
        case GTK_RESPONSE_DELETE_EVENT:
        case GTK_RESPONSE_NONE:
        iface = netstatus_dialog_get_iface_name((GtkWidget*)dlg);
        if( iface )
        {
            g_free(ns->iface);
            ns->iface = g_strdup(iface);
            gtk_widget_destroy( GTK_WIDGET(dlg) );
            ns->dlg = NULL;
        }
    }
}

static void on_button_press( GtkWidget* widget, GdkEventButton* evt, Plugin* p )
{
    NetstatusIface* iface;
    netstatus *ns = (netstatus*)p->priv;

    if( evt->button == 1 ) /*  Left click*/
    {
        if( ! ns->dlg )
        {
            iface = netstatus_icon_get_iface( NETSTATUS_ICON(widget) );
            ns->dlg = netstatus_dialog_new(iface);

            /* fix background */
            gtk_widget_set_style(ns->dlg, p->panel->defstyle);

            netstatus_dialog_set_configuration_tool( ns->dlg, ns->config_tool );
            g_signal_connect( ns->dlg, "response", G_CALLBACK(on_response), ns );
        }
        gtk_window_present( GTK_WINDOW(ns->dlg) );
    }
}

static int
netstatus_constructor(Plugin *p, char** fp)
{
    netstatus *ns;
    line s;
    NetstatusIface* iface;

    ENTER;
    s.len = 256;
    ns = g_new0(netstatus, 1);
    g_return_val_if_fail(ns != NULL, 0);
    p->priv = ns;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "netstatus: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "iface"))
                    ns->iface = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "configtool"))
                    ns->config_tool = g_strdup(s.t[1]);
                else {
                    ERR( "netstatus: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "netstatus: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }
    else
    {
        ns->iface = g_strdup("eth0");
        ns->config_tool = g_strdup("network-admin --configure %i");
    }

    iface = netstatus_iface_new(ns->iface);
    ns->mainw = netstatus_icon_new( iface );
    netstatus_icon_set_show_signal(ns->mainw, TRUE);
    gtk_widget_add_events( ns->mainw, GDK_BUTTON_PRESS_MASK );
    g_object_unref( iface );
    g_signal_connect( ns->mainw, "button-press-event",
                      G_CALLBACK(on_button_press), p );
    gtk_widget_set_size_request( ns->mainw, 26, 24 );

    gtk_widget_show_all(ns->mainw);

    p->pwid = ns->mainw;

    RET(1);

 error:
    netstatus_destructor(p);
    RET(0);
}

static void apply_config(Plugin* p)
{
    netstatus *ns = (netstatus *)p->priv;
    NetstatusIface* iface;

    iface = netstatus_iface_new(ns->iface);
    netstatus_icon_set_iface(ns->mainw, iface);
}

static void netstatus_config( Plugin* p, GtkWindow* parent  )
{
    GtkWidget* dlg;
    netstatus *ns = (netstatus*)p->priv;
    dlg = create_generic_config_dlg(
                _(p->class->name),
                GTK_WIDGET(parent),
                (GSourceFunc) apply_config, p,
                _("Interface to monitor"), &ns->iface, CONF_TYPE_STR,
                _("Config tool"), &ns->config_tool, CONF_TYPE_STR,
                NULL );
    gtk_window_present( GTK_WINDOW(dlg) );
}

static void save_config( Plugin* p, FILE* fp )
{
    netstatus *ns = (netstatus*)p->priv;
    lxpanel_put_str( fp, "iface", ns->iface );
    lxpanel_put_str( fp, "configtool", ns->config_tool );
}

PluginClass netstatus_plugin_class = {
    fname: NULL,
    count: 0,

    type : "netstatus",
    name : N_("Net Status Monitor"),
    version: "1.0",
    description : N_("Monitor network status"),

    constructor : netstatus_constructor,
    destructor  : netstatus_destructor,
    config : netstatus_config,
    save : save_config
};
