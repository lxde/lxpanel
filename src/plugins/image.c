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
    GtkWidget *mainw;
    char* config_data;
} image;

static void
image_destructor(Plugin *p)
{
    image *img = (image *)p->priv;

    ENTER;
    gtk_widget_destroy(img->mainw);
    g_free( img->config_data );
    g_free(img);
    RET();
}

static int
image_constructor(Plugin *p, char **fp)
{
    gchar *tooltip, *fname;
    image *img;
    GdkPixbuf *gp, *gps;
    GtkWidget *wid;
    GError *err = NULL;
    char *config_start, *config_end;

    line s;

    s.len = 256;
    ENTER;
    img = g_new0(image, 1);
    g_return_val_if_fail(img != NULL, 0);
    p->priv = img;
    tooltip = fname = 0;
    if( fp ) {
        config_start = *fp;
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "image: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "image"))
                    fname = expand_tilda(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "tooltip"))
                    tooltip = g_strdup(s.t[1]);
                else {
                    ERR( "image: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "image: illegal in this context %s\n", s.str);
                goto error;
            }
        }
        config_end = *fp - 1;
        while( *config_end != '}' && config_end > config_start ) {
            --config_end;
        }
        if( *config_end == '}' )
            --config_end;
        img->config_data = g_strndup( config_start,
                                      (config_end-config_start) );
    }
    else {
        config_start = config_end = NULL;
    }
    img->mainw = gtk_event_box_new();
    gtk_widget_show(img->mainw);
    //g_signal_connect(G_OBJECT(img->mainw), "expose_event",
    //      G_CALLBACK(gtk_widget_queue_draw), NULL);
    gp = gdk_pixbuf_new_from_file(fname, &err);
    if (!gp) {
        g_warning("image: can't read image %s\n", fname);
        wid = gtk_label_new("?");
    } else {
        float ratio;
        ratio = (p->panel->orientation == ORIENT_HORIZ) ?
            (float) (p->panel->ah - 2) / (float) gdk_pixbuf_get_height(gp)
            : (float) (p->panel->aw - 2) / (float) gdk_pixbuf_get_width(gp);
        gps =  gdk_pixbuf_scale_simple (gp,
              ratio * ((float) gdk_pixbuf_get_width(gp)),
              ratio * ((float) gdk_pixbuf_get_height(gp)),
              GDK_INTERP_HYPER);
        wid = gtk_image_new_from_pixbuf(gps);
        g_object_unref(gp);
        g_object_unref(gps);

    }
    gtk_widget_show(wid);
    gtk_container_add(GTK_CONTAINER(img->mainw), wid);
    gtk_container_set_border_width(GTK_CONTAINER(img->mainw), 0);
    g_free(fname);

    if (tooltip) {
        gtk_widget_set_tooltip_text(img->mainw, tooltip);
        g_free(tooltip);
    }
    RET(1);

 error:
    g_free(fname);
    g_free(tooltip);
    image_destructor(p);
    RET(0);
}

static void save_config( Plugin* p, FILE* fp )
{
    image *img = (image *)p->priv;
    if( img->config_data ) {
        char** lines = g_strsplit( img->config_data, "\n", 0 );
        char** line;
        for( line = lines; *line; ++line ) {
            g_strstrip( *line );
            if( **line )
                lxpanel_put_line( fp, *line );
        }
        g_strfreev( lines );
    }
}

PluginClass image_plugin_class = {
    fname: NULL,
    count: 0,

    type : "image",
    name : "image",
    version: "1.0",
    description : N_("Display Image and Tooltip"),

    constructor : image_constructor,
    destructor  : image_destructor,
    config : NULL,
    save : save_config
};
