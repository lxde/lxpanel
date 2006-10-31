#include "panel.h"
#include "misc.h"
#include "plugin.h"


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
    GtkWidget* eb = (GtkEventBox*)p->priv;
    gtk_widget_destroy( eb );
    RET();
}

static void orientation_changed( plugin* p )
{
    GtkWidget* eb = (GtkEventBox*)p->priv;
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
    name : "Separator",
    version: "1.0",
    description : "Simple separator plugin",

    constructor : separator_constructor,
    destructor  : separator_destructor,
    config : NULL,
    save : NULL,
    orientation : orientation_changed
};
