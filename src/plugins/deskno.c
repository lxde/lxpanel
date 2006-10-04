// reused dclock.c and variables from pager.c
// 11/23/04 by cmeury

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

// #define DEBUG
#include "dbg.h"

typedef struct {
    GtkWidget *main;
    GtkWidget *namew;
    GtkTooltips *tip;
} deskno;

static  void
clicked( GtkWidget *widget, gpointer data)
{
    int desknum = get_net_current_desktop();
    int desks = get_net_number_of_desktops();
    int newdesk;

    ENTER;
    if(desknum == (desks - 1))
	newdesk = 0;
    else
	newdesk = desknum + 1;
    g_assert(data != NULL);
    Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, newdesk, 0, 0, 0, 0);
    RET();
}



static gint
name_update(GtkWidget *widget, deskno *dc)
{
    char buffer [15];
    int n;
    int desknum = get_net_current_desktop() + 1;

    ENTER;
    n = sprintf(buffer, "<b>%d</b>", desknum);
    gtk_label_set_markup (GTK_LABEL(dc->namew), buffer) ;
    RET(TRUE);
}


static int
deskno_constructor(plugin *p)
{
    deskno *dc;
    GtkWidget *button;
    
    ENTER;
    dc = g_new0(deskno, 1);
    g_return_val_if_fail(dc != NULL, 0);
    p->priv = dc;
    
    dc->main = gtk_event_box_new();
    dc->tip  = gtk_tooltips_new();
    button = gtk_button_new();
    gtk_button_set_relief(GTK_BUTTON(button),GTK_RELIEF_NONE);
    g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (clicked), (gpointer) dc);
    dc->namew = gtk_label_new("ww");
    gtk_container_add(GTK_CONTAINER(button), dc->namew);
    gtk_container_add(GTK_CONTAINER(p->pwid), button);
    gtk_widget_show_all(p->pwid);
    name_update(button, dc);
    g_signal_connect (G_OBJECT (fbev), "current_desktop", G_CALLBACK (name_update), (gpointer) dc);
    RET(1);


}


static void
deskno_destructor(plugin *p)
{
  deskno *dc = (deskno *)p->priv;

  ENTER;
  dc = (deskno *) p->priv;
  g_signal_handlers_disconnect_by_func(G_OBJECT (fbev), name_update, dc); 
  g_free(dc);
  RET();
}

plugin_class deskno_plugin_class = {
    fname: NULL,
    count: 0,

    type : "deskno",
    name : "Desktop No / Workspace Name",
    version: "0.6",
    description : "Display workspace number, by cmeury@users.sf.net",

    constructor : deskno_constructor,
    destructor  : deskno_destructor,
};
