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

typedef struct {
    int size;
} space;

static void
space_destructor(Plugin *p)
{
    space *sp = (space *)p->priv;

    ENTER;
    /* The widget is destroyed in plugin_stop().
    gtk_widget_destroy(sp->mainw);
    */
    g_free(sp);
    RET();
}

static gboolean on_btn_press( GtkWidget* w, GdkEventButton* evt, Plugin* plugin )
{
    if( evt->button == 3 )
    {
        GtkMenu* popup = lxpanel_get_panel_menu( plugin->panel, plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, evt->button, evt->time );
    }
    return TRUE;
}

static int
space_constructor(Plugin *p, char** fp)
{
    space *sp;
    line s;
    int w, h;

    ENTER;
    s.len = 256;
    sp = g_new0(space, 1);
    g_return_val_if_fail(sp != NULL, 0);
    p->priv = sp;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "space: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "size"))
                    sp->size = atoi(s.t[1]);
                else {
                    ERR( "space: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "space: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }
    if (!sp->size)
        sp->size = 2;

    if (p->panel->orientation == ORIENT_HORIZ) {
        h = 2;
        w = sp->size;
    } else {
        w = 2;
        h = sp->size;
    }

    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( p->pwid, GTK_NO_WINDOW);
    gtk_widget_add_events( p->pwid, GDK_BUTTON_PRESS_MASK );
    g_signal_connect( p->pwid, "button-press-event", G_CALLBACK( on_btn_press ), p );
    gtk_widget_show( p->pwid );
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 0);
    gtk_widget_set_size_request(p->pwid, w, h);

    RET(1);

 error:
    space_destructor(p);
    RET(0);
}

static void apply_config( Plugin* p )
{
    int w, h;
    space *sp = (space *)p->priv;
    /* Apply settings */
    if (p->panel->orientation == ORIENT_HORIZ) {
        h = 2;
        w = sp->size;
    } else {
        w = 2;
        h = sp->size;
    }
    gtk_widget_set_size_request(p->pwid, w, h);
}

static void space_config(Plugin* p, GtkWindow* parent )
{
    GtkWidget* dlg;
    space *sp = (space *)p->priv;
    dlg =  create_generic_config_dlg( _(p->class->name),
                                     GTK_WIDGET(parent),
                                     (GSourceFunc) apply_config, (gpointer) p,
                                     _("Size"), &sp->size, G_TYPE_INT,  NULL );
    gtk_window_present( GTK_WINDOW(dlg) );
}

static void save_config( Plugin* p, FILE* fp )
{
    space *sp = (space *)p->priv;
    lxpanel_put_int( fp, "Size", sp->size );
}

PluginClass space_plugin_class = {
    fname: NULL,
    count: 0,

    type : "space",
    name : N_("<Space>"),
    version: "1.0",
    description : N_("Allocate space"),

    constructor : space_constructor,
    destructor  : space_destructor,
    config : space_config,
    save : save_config
};
