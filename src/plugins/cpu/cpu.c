/*
 * CPU usage plugin to lxpanel
 *
 * Copyright (c) 2008 LxDE Developers, see the file AUTHORS for details.
 * Copyright (C) 2004 by Alexandre Pereira da Silva <alexandre.pereira@poli.usp.br>
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
/*A little bug fixed by Mykola <mykola@2ka.mipt.ru>:) */


#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <sys/sysinfo.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#include "plugin.h"
#include "panel.h"
#include "misc.h"

#define KILOBYTE 1024
#define MAX_WGSIZE 100

#define BORDER_SIZE 2

#include "dbg.h"
typedef unsigned long tick;

struct cpu_stat {
    tick u, n, s, i;
};


typedef struct {
    GdkGC *gc_cpu;
    GdkColor *ccpu;
    GtkWidget *da;
    GtkWidget *evbox;
    GdkPixmap *pixmap;
    /* GtkTooltips *tip; */

    int timer;
    tick *stats_cpu;
    unsigned int ini_stats;
    int Wwg;
    int Hwg;
    struct cpu_stat cpu_anterior;
} cpu_t;


static int
cpu_update(cpu_t *c)
{
    int cpu_u=0, cpu_s=0, cpu_n=0, cpu_i=100;
    unsigned int i;
    struct cpu_stat cpu, cpu_r;
    FILE *stat;
    float total;

    ENTER;
    if(!c->pixmap)
        RET(TRUE);

    stat = fopen("/proc/stat", "r");
    if(!stat)
        RET(TRUE);
    fscanf(stat, "cpu %lu %lu %lu %lu", &cpu.u, &cpu.n, &cpu.s, &cpu.i);
    fclose(stat);

    cpu_r.u = cpu.u - c->cpu_anterior.u;
    cpu_r.n = cpu.n - c->cpu_anterior.n;
    cpu_r.s = cpu.s - c->cpu_anterior.s;
    cpu_r.i = cpu.i - c->cpu_anterior.i;

    total = cpu_r.u + cpu_r.n + cpu_r.s + cpu_r.i;
    cpu_u = cpu_r.u * (c->Hwg-BORDER_SIZE * 2) / total;
    cpu_s = cpu_r.n * (c->Hwg-BORDER_SIZE * 2) / total;
    cpu_n = cpu_r.s * (c->Hwg-BORDER_SIZE * 2) / total;
    cpu_i = cpu_r.i * (c->Hwg-BORDER_SIZE * 2) / total;

    c->cpu_anterior = cpu;

    c->stats_cpu[c->ini_stats++] = cpu_u + cpu_s + cpu_n;
    c->ini_stats %= c->Wwg;

    gdk_draw_rectangle(c->pixmap, c->da->style->black_gc, TRUE, 0, 0, c->Wwg - BORDER_SIZE * 2, c->Hwg - BORDER_SIZE * 2);
    for (i = 0; i < (c->Wwg - BORDER_SIZE); i++) {
    int val;

    val = c->stats_cpu[(i + c->ini_stats) % (c->Wwg - BORDER_SIZE * 2) ];
        if (val)
            gdk_draw_line(c->pixmap, c->gc_cpu, i, (c->Hwg - BORDER_SIZE * 2), i, (c->Hwg - BORDER_SIZE * 2) - val);
    }
    gtk_widget_queue_draw(c->da);
    RET(TRUE);
}

static gint
configure_event(GtkWidget *widget, GdkEventConfigure *event, cpu_t *c)
{
    ENTER;
    if (c->pixmap)
        g_object_unref(c->pixmap);
    c->Wwg = widget->allocation.width;
    c->Hwg = widget->allocation.height;
    if (c->stats_cpu)
        g_free(c->stats_cpu);
    c->stats_cpu = g_new0( typeof(*c->stats_cpu), c->Wwg);
    /* set pixmap size */
    c->pixmap = gdk_pixmap_new (widget->window,
          widget->allocation.width-BORDER_SIZE * 2,
          widget->allocation.height-BORDER_SIZE * 2,
          -1);
    gdk_draw_rectangle (c->pixmap,
          widget->style->black_gc,
          TRUE,
          0, 0,
          widget->allocation.width-BORDER_SIZE * 2,
          widget->allocation.height-BORDER_SIZE * 2); 

   RET(TRUE);
}


static gint
expose_event(GtkWidget *widget, GdkEventExpose *event, cpu_t *c)
{
    ENTER;
    gdk_draw_drawable (widget->window,
          c->da->style->black_gc,
          c->pixmap,
          event->area.x, event->area.y,
          event->area.x+BORDER_SIZE, event->area.y+BORDER_SIZE,
          event->area.width, event->area.height);

    RET(FALSE);
}

static gboolean  on_button_press(GtkWidget* w, GdkEventButton* evt, Plugin* plugin)
{
    if( evt->button == 3 )  /* right button */
    {
        GtkMenu* popup = lxpanel_get_panel_menu( plugin->panel, plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, evt->button, evt->time );
        return TRUE;
    }
    return FALSE;
}

static int
cpu_constructor(Plugin *p, char **fp)
{
    cpu_t *c;

    ENTER;
    c = g_new0(cpu_t, 1);
    p->priv = c;

    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( p->pwid, GTK_NO_WINDOW );

    c->da = gtk_drawing_area_new();
    gtk_widget_set_size_request(c->da, 40, 20);
    gtk_widget_add_events( c->da, GDK_BUTTON_PRESS_MASK );

    gtk_widget_show(c->da);

    c->gc_cpu = gdk_gc_new(p->panel->topgwin->window);

    c->ccpu = (GdkColor *)malloc(sizeof(GdkColor));
    gdk_color_parse("green",  c->ccpu);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(p->panel->topgwin->window),  c->ccpu, FALSE, TRUE);
    gdk_gc_set_foreground(c->gc_cpu,  c->ccpu);

    gtk_container_add(GTK_CONTAINER(p->pwid), c->da);
    gtk_container_set_border_width (GTK_CONTAINER (p->pwid), 1);

    g_signal_connect (G_OBJECT (c->da),"configure_event",
          G_CALLBACK (configure_event), (gpointer) c);
    g_signal_connect (G_OBJECT (c->da), "expose_event",
          G_CALLBACK (expose_event), (gpointer) c);
    g_signal_connect( c->da, "button-press-event",
          G_CALLBACK(on_button_press), p );

    c->timer = g_timeout_add(1500, (GSourceFunc) cpu_update, (gpointer) c);
    RET(1);
}

static void
cpu_destructor(Plugin *p)
{
    cpu_t *c = (cpu_t *) p->priv;

    ENTER;
    g_object_unref(c->pixmap);
    g_object_unref(c->gc_cpu);
    g_free(c->stats_cpu);
    g_free(c->ccpu);
    g_source_remove(c->timer);
    /* g_object_unref( c->tip ); */
    g_free(p->priv);
    RET();
}


PluginClass cpu_plugin_class = {
    fname: NULL,
    count: 0,

    type : "cpu",
    name : N_("CPU Usage Monitor"),
    version: "1.0",
    description : N_("Display CPU usage"),

    constructor : cpu_constructor,
    destructor  : cpu_destructor,
    config : NULL,
    save : NULL
};
