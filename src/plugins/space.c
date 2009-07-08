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

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

/* Private context for space plugin. */
typedef struct {
    int size;				/* Size of spacer */
} SpacePlugin;

static int space_constructor(Plugin * p, char ** fp);
static void space_destructor(Plugin * p);
static void space_apply_configuration(Plugin * p);
static void space_configure(Plugin * p, GtkWindow * parent);
static void space_save_configuration(Plugin * p, FILE * fp);

/* Plugin constructor. */
static int space_constructor(Plugin * p, char ** fp)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    SpacePlugin * sp = g_new0(SpacePlugin, 1);
    p->priv = sp;

    /* Load parameters from the configuration file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "space: illegal token %s\n", s.str);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "size") == 0)
                    sp->size = atoi(s.t[1]);
                else
                    ERR( "space: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "space: illegal in this context %s\n", s.str);
                return 0;
            }
        }
    }

    /* Default the size parameter. */
    if (sp->size == 0)
        sp->size = 2;

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);
    gtk_widget_add_events(p->pwid, GDK_BUTTON_PRESS_MASK);
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 0);

    /* Connect signals. */
    g_signal_connect(p->pwid, "button-press-event", G_CALLBACK(plugin_button_press_event), p);

    /* Apply the configuration and show the widget. */
    space_apply_configuration(p);
    gtk_widget_show(p->pwid);
    return 1;
}

/* Plugin destructor. */
static void space_destructor(Plugin * p)
{
    SpacePlugin * sp = (SpacePlugin *) p->priv;
    g_free(sp);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static void space_apply_configuration(Plugin * p)
{
    SpacePlugin * sp = (SpacePlugin *) p->priv;

    /* Apply settings. */
    if (p->panel->orientation == ORIENT_HORIZ)
        gtk_widget_set_size_request(p->pwid, sp->size, 2);
    else
        gtk_widget_set_size_request(p->pwid, 2, sp->size);
}

/* Callback when the configuration dialog is to be shown. */
static void space_configure(Plugin * p, GtkWindow * parent)
{
    SpacePlugin * sp = (SpacePlugin *) p->priv;
    GtkWidget * dlg = create_generic_config_dlg(
        _(p->class->name),
        GTK_WIDGET(parent),
        (GSourceFunc) space_apply_configuration, (gpointer) p,
        _("Size"), &sp->size, CONF_TYPE_INT,  NULL);
    gtk_widget_set_size_request(GTK_WIDGET(dlg), 200, -1);	/* Improve geometry */
    gtk_window_present(GTK_WINDOW(dlg));
}

/* Callback when the configuration is to be saved. */
static void space_save_configuration(Plugin * p, FILE * fp)
{
    SpacePlugin * sp = (SpacePlugin *) p->priv;
    lxpanel_put_int(fp, "Size", sp->size);
}

/* Plugin descriptor. */
PluginClass space_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "space",
    name : N_("Spacer"),
    version: "1.0",
    description : N_("Allocate space"),

    /* Stretch is available but not default for this plugin. */
    expand_available : TRUE,

    constructor : space_constructor,
    destructor  : space_destructor,
    config : space_configure,
    save : space_save_configuration
};
