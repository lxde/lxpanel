#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

//#define DEBUG
#include "dbg.h"

typedef struct {
    GtkWidget  *main;
    int         dno;            // current desktop nomer
    int         dnum;           // number of desktops
    char      **dnames;         // desktop names
    int         dnames_num;     // number of desktop names
    char      **lnames;         // label names
    char       *fmt;
} deskno;



static  void
clicked(GtkWidget *widget, deskno *dc)
{
    system("xfce-setting-show workspaces");
}

static  void
update_dno(GtkWidget *widget, deskno *dc)
{
    ENTER;
    dc->dno = fb_ev_current_desktop(fbev);
    gtk_button_set_label(GTK_BUTTON(dc->main), dc->lnames[dc->dno]);

    RET();
}



static  void
update_all(GtkWidget *widget, deskno *dc)
{
    int i;

    ENTER;
    dc->dnum = fb_ev_number_of_desktops(fbev);
    if (dc->dnames)
        g_strfreev (dc->dnames);
    if (dc->lnames)
        g_strfreev (dc->lnames);
    dc->dnames = get_utf8_property_list(GDK_ROOT_WINDOW(), a_NET_DESKTOP_NAMES, &(dc->dnames_num));
    dc->lnames = g_new0 (gchar*, dc->dnum + 1);
    for (i = 0; i < MIN(dc->dnum, dc->dnames_num); i++) {
        dc->lnames[i] = g_strdup(dc->dnames[i]);
    }
    for (; i < dc->dnum; i++) {
        dc->lnames[i] = g_strdup_printf("%d", i + 1);
    }
    update_dno(widget, dc);
    RET();
}


static gboolean
scroll (GtkWidget *widget, GdkEventScroll *event, deskno *dc)
{
    int dno;

    ENTER;
    dno = dc->dno + ((event->direction == GDK_SCROLL_UP) ? (-1) : (+1));
    if (dno < 0)
        dno = dc->dnum - 1;
    else if (dno == dc->dnum)
        dno = 0;
    Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, dno, 0, 0, 0, 0);
    RET(TRUE);

}

static int
deskno_constructor(Plugin *p, char** fp)
{
    deskno *dc;
    ENTER;
    dc = g_new0(deskno, 1);
    g_return_val_if_fail(dc != NULL, 0);
    p->priv = dc;

    dc->main = gtk_button_new_with_label("w");
    gtk_button_set_relief(GTK_BUTTON(dc->main),GTK_RELIEF_NONE);
    gtk_container_set_border_width(GTK_CONTAINER(dc->main), 0);
    //gtk_button_set_alignment(GTK_BUTTON(dc->main), 0, 0.5);
    g_signal_connect(G_OBJECT(dc->main), "clicked", G_CALLBACK (clicked), (gpointer) dc);
    g_signal_connect(G_OBJECT(dc->main), "scroll-event", G_CALLBACK(scroll), (gpointer) dc);

    update_all(dc->main, dc);

    p->pwid = dc->main;
    gtk_widget_show_all(p->pwid);

    g_signal_connect (G_OBJECT (fbev), "current_desktop", G_CALLBACK (update_dno), (gpointer) dc);
    g_signal_connect (G_OBJECT (fbev), "desktop_names", G_CALLBACK (update_all), (gpointer) dc);
    g_signal_connect (G_OBJECT (fbev), "number_of_desktops", G_CALLBACK (update_all), (gpointer) dc);

    RET(1);
}


static void
deskno_destructor(Plugin *p)
{
  deskno *dc = (deskno *)p->priv;

  ENTER;
  dc = (deskno *) p->priv;
  //g_signal_handlers_disconnect_by_func(G_OBJECT (fbev), name_update, dc);
  g_free(dc);
  RET();
}

PluginClass deskno2_plugin_class = {
    fname: NULL,
    count: 0,

    type : "deskno2",
    name : N_("Desktop No / Workspace Name"),
    version: "0.6",
    description : N_("Display workspace number, by cmeury@users.sf.net"),

    constructor : deskno_constructor,
    destructor  : deskno_destructor,
    config : NULL,
    save : NULL
};
