#include <stdlib.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

//#define DEBUG
#include "dbg.h"


typedef struct {
    GdkPixmap *pix;
    GdkBitmap *mask;
    GtkTooltips *tips;
    GtkWidget *mainw;
} image;



static void
image_destructor(plugin *p)
{
    image *img = (image *)p->priv;

    ENTER;
    gtk_widget_destroy(img->mainw);
    if (img->mask)
        g_object_unref(img->mask);
    if (img->pix)
        g_object_unref(img->pix);
    //
    g_free(img);
    RET();
}




static int
image_constructor(plugin *p)
{
    gchar *tooltip, *fname;
    image *img;
    GdkPixbuf *gp, *gps;
    GtkWidget *wid;
    GError *err = NULL;
    
    line s;

    s.len = 256;
    ENTER;
    img = g_new0(image, 1);
    g_return_val_if_fail(img != NULL, 0);
    img->tips = gtk_tooltips_new();
    p->priv = img;
    tooltip = fname = 0;
    while (get_line(p->fp, &s) != LINE_BLOCK_END) {
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
        gdk_pixbuf_render_pixmap_and_mask(gps, &img->pix, &img->mask, 127);
        gdk_pixbuf_unref(gp);
        gdk_pixbuf_unref(gps);
        wid = gtk_image_new_from_pixmap(img->pix, img->mask);

    }
    gtk_widget_show(wid);
    gtk_container_add(GTK_CONTAINER(img->mainw), wid);
    gtk_container_set_border_width(GTK_CONTAINER(img->mainw), 0);
    g_free(fname);
    gtk_container_add(GTK_CONTAINER(p->pwid), img->mainw);
    if (tooltip) {
        gtk_tooltips_set_tip(GTK_TOOLTIPS (img->tips), img->mainw, tooltip, NULL);
        g_free(tooltip);
    }
    RET(1);

 error:
    g_free(fname);
    g_free(tooltip);
    image_destructor(p);
    RET(0);
}


plugin_class image_plugin_class = {
    fname: NULL,
    count: 0,

    type : "image",
    name : "image",
    version: "1.0",
    description : "Dispaly Image and Tooltip",

    constructor : image_constructor,
    destructor  : image_destructor,
};
