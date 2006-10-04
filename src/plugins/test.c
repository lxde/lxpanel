
/*
 *  test - test module. its purpose to continuously change its size by
 *  allocating and destroying widgets. It helps in debuging panels's
 *  geometry engine (panel.c )
 */
    


#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "panel.h"
#include "misc.h"
#include "plugin.h"

//#define DEBUG
#include "dbg.h"


#define WID_NUM 80

typedef struct {
    GtkWidget *main;
    int count;
    int delta;
    int timer;
    GtkWidget *wid[WID_NUM];
} test;

//static dclock me;




static gint
clock_update(gpointer data )
{
    test *dc = (test *)data;
     
    ENTER;
    if (dc->count >= WID_NUM-1)
        dc->delta = -1;
    else if (dc->count <= 0)
        dc->delta = 1;
    if (dc->delta == 1) {
        dc->wid[dc->count] = gtk_button_new_with_label("  wwwww  ");
        gtk_widget_show( dc->wid[dc->count] );
        gtk_box_pack_start(GTK_BOX(dc->main), dc->wid[dc->count], TRUE, FALSE, 0);
     } else
        gtk_widget_destroy(dc->wid[dc->count]);
    dc->count += dc->delta;
    RET(TRUE);
}


static int
test_constructor(plugin *p)
{
    test *dc;
    line s;
    
    ENTER;
    dc = g_new0(test, 1);
    g_return_val_if_fail(dc != NULL, 0);
    p->priv = dc;
    dc->delta = 1;
    s.len = 256;
    while (get_line(p->fp, &s) != LINE_BLOCK_END) {
        ERR( "test: illegal in this context %s\n", s.str);
    }
    dc->main = p->panel->my_box_new(TRUE, 1);
    gtk_widget_show(dc->main);
    gtk_container_add(GTK_CONTAINER(p->pwid), dc->main);

    dc->timer = g_timeout_add(200, clock_update, (gpointer)dc);

    RET(1);
}


static void
test_destructor(plugin *p)
{
  test *dc = (test *)p->priv;

  ENTER;
  dc = (test *) p->priv;
  if (dc->timer)
      g_source_remove(dc->timer);
  gtk_widget_destroy(dc->main);
  RET();
}

plugin_class test_plugin_class = {
    fname: NULL,
    count: 0,

    type : "test",
    name : "Digital Clock",
    version: "1.0",
    description : "Dispaly Digital clock and Tooltip",

    constructor : test_constructor,
    destructor  : test_destructor,
};
