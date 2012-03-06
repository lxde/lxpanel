/**
 * Plugin for the lxpanel.
 *
 * Displays several monitors in the panel.
 *
 * A lot of code in this plugin comes from the CPU plugin (that only displays a
 * CPU monitor), that is distributed under the following terms :
 *
 * <terms>
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
 * </terms>
 *
 */

/*
 * HOWTO : Add your own monitor for the resource "foo".
 *
 * 1) Write the foo_update() function, that fills in the stats.
 * 2) Write the foo_tooltip_update() function, that updates your tooltip. This
 *    is optional, but recommended.
 * 3) Add a #define FOO_POSITION, and increment N_MONITORS.
 * 4) Add :
 *     - the default color of your plugin ("default_colors" table)
 *     - the update function ("update_functions" table)
 *     - the tooltip update function ("tooltip_update" table)
 * 5) Configuration :
 *     - edit the monitors_config() function so that a "Display FOO usage"
 *     checkbox appears in the prefs dialog.
 *     - edit the monitors_save() function so that a "DisplayFOO" string appears
 *     in the config file ("~/.config/lxpanel/<profile>/config")
 *     - edit the monitors_config() function so that a "FOO color" entry appears
 *       in the prefs dialog.
 *     - edit the monitors_save() function so that a "FOOColor" string appears
 *       in the config file.
 *     - edit the monitors_constructor() function so that options are correctly
 *     aplied. Adding something like :
 *     
 *     else if (g_ascii_strcasecmp(s.t[0], "DisplayFOO") == 0)
 *         mp->displayed_monitors[FOO_POSITION] = atoi(s.t[1])
 *     else if (g_ascii_strcasecmp(s.t[0], "FOOColor") == 0)
 *         colors[FOO_POSITION] = g_strndup(s.t[1], COLOR_SIZE-1);       
 *
 *     should be enough.
 * 6) Enjoy.
 */

/*
 * FIXME : known BUGS : 
 *     - when removing a monitor and re-adding it, it is drawn with a white
 *     border of BORDER_SIZE pixels around it.
 */

#include <stdlib.h>
#include <glib/gi18n.h>

#include "plugin.h"
#include "panel.h"
#include "misc.h"

#include "dbg.h"


#define PLUGIN_NAME      "MonitorsPlugin"
#define BORDER_SIZE      2                  /* Pixels               */
#define DEFAULT_WIDTH    40                 /* Pixels               */
#define UPDATE_PERIOD    2000               /* Milliseconds         */
#define COLOR_SIZE       8                  /* In chars : #xxxxxx\0 */

#ifndef ENTER
#define ENTER fprintf(stderr, "Entering %s\n", __func__);
#endif

/*
 * Stats are stored in a circular buffer. 
 * Newest values are on the left of the ring cursor.
 * Oldest values are on the right of the ring cursor.
 */
typedef float stats_set;

struct Monitor {
    GdkGC        *graphics_context; /* Graphics context for drawing area      */
    GdkColor     foreground_color;  /* Foreground color for drawing area      */
    GtkWidget    *da;               /* Drawing area                           */
    GdkPixmap    *pixmap;           /* Pixmap to be drawn on drawing area     */
    gint         pixmap_width;      /* Width and size of the buffer           */
    gint         pixmap_height;     /* Does not include border size           */
    stats_set    *stats;            /* Circular buffer of values              */
    gint         ring_cursor;       /* Cursor for ring/circular buffer        */
    gchar        *color;            /* Color of the graph                     */
    gboolean     (*update) (struct Monitor *); /* Update function             */
    void         (*update_tooltip) (struct Monitor *);
};

typedef struct Monitor Monitor;
typedef gboolean (*update_func) (Monitor *);
typedef void (*tooltip_update_func) (Monitor *);

/* 
 * Position of our monitors : monitor 0 will always be on the left of the
 * plugin, monitor 1 on the right of monitor 0 (or on the left of the plugin if
 * monitor 0 is not displayed), etc.
 */
#define CPU_POSITION    0
#define MEM_POSITION    1
#define N_MONITORS      2

/* Our plugin */
typedef struct {
    Monitor  *monitors[N_MONITORS];          /* Monitors                      */
    int      displayed_monitors[N_MONITORS]; /* Booleans                      */
    guint    timer;                          /* Timer for regular updates     */
} MonitorsPlugin;

/* 
 * Prototypes 
 */
static Monitor* monitor_init(Plugin *, Monitor *, gchar *);
static void monitor_free(Monitor *m);
static void monitor_set_foreground_color(Plugin *, Monitor *, const gchar *);

/* CPU Monitor */
static gboolean cpu_update(Monitor *);
static void     cpu_tooltip_update (Monitor *m);

/* RAM Monitor */
static gboolean mem_update(Monitor *);
static void     mem_tooltip_update (Monitor *m);


static gboolean configure_event(GtkWidget*, GdkEventConfigure*, gpointer);
static gboolean expose_event(GtkWidget *, GdkEventExpose *, Monitor *);
static void redraw_pixmap (Monitor *m);

/* Monitors functions */
static int monitors_constructor(Plugin *, char **);
static void monitors_destructor(Plugin*p);
static void monitors_config (Plugin *p, GtkWindow *parent);
static void monitors_apply_config (Plugin *);
static void monitors_save(Plugin *p, FILE *fp);
static gboolean monitors_update(gpointer);
static Monitor* monitors_add_monitor (Plugin *p, MonitorsPlugin *mp, 
        update_func update, tooltip_update_func update_tooltip, gchar *color);



/******************************************************************************
 *                              Monitor functions                             *
 ******************************************************************************/
static Monitor*
monitor_init(Plugin *p, Monitor *m, gchar *color) 
{
    ENTER;

    m->da = gtk_drawing_area_new();
    gtk_widget_set_size_request(m->da, DEFAULT_WIDTH, PANEL_HEIGHT_DEFAULT);
    gtk_widget_add_events(m->da, GDK_BUTTON_PRESS_MASK);

    m->graphics_context = gdk_gc_new(p->panel->topgwin->window);
    monitor_set_foreground_color(p, m, color);

    /* Signals */
    g_signal_connect(G_OBJECT(m->da), "configure-event", 
        G_CALLBACK(configure_event), (gpointer) m);
    g_signal_connect (G_OBJECT(m->da), "expose-event",
        G_CALLBACK(expose_event), (gpointer) m);
    g_signal_connect(G_OBJECT(m->da), "button-press-event", 
                    G_CALLBACK(plugin_button_press_event), p);

    return m;
}

static void
monitor_free(Monitor *m)
{
    if (!m)
        return;

    g_free(m->color);
    if (m->graphics_context)
        g_object_unref(m->graphics_context);
    if (m->pixmap)
        g_object_unref(m->pixmap);
    if (m->stats)
        g_free(m->stats);
    g_free(m);

    return;
}

static void
monitor_set_foreground_color(Plugin *p, Monitor *m, const gchar *color)
{
    g_free(m->color);
    m->color = g_strndup(color, COLOR_SIZE - 1);
    gdk_color_parse(color, &m->foreground_color);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(p->panel->topgwin->window),
                            &m->foreground_color, FALSE, TRUE);
    gdk_gc_set_foreground(m->graphics_context, &m->foreground_color);
}
/******************************************************************************
 *                          End of monitor functions                          *
 ******************************************************************************/

/******************************************************************************
 *                                 CPU monitor                                *
 ******************************************************************************/
typedef unsigned long CPUTick;	   /* Value from /proc/stat                   */
typedef float CPUSample;	   /* Saved CPU utilization value as 0.0..1.0 */

struct cpu_stat {
    CPUTick u, n, s, i;		  /* User, nice, system, idle */
};

static gboolean
cpu_update(Monitor * c)
{
    static struct cpu_stat previous_cpu_stat = { 0, 0, 0, 0 };

    if ((c->stats != NULL) && (c->pixmap != NULL))
    {
        /* Open statistics file and scan out CPU usage. */
        struct cpu_stat cpu;
        FILE * stat = fopen("/proc/stat", "r");
        if (stat == NULL)
            return TRUE;
        int fscanf_result = fscanf(stat, "cpu %lu %lu %lu %lu", 
                                    &cpu.u, &cpu.n, &cpu.s, &cpu.i);
        fclose(stat);

        /* Ensure that fscanf succeeded. */
        if (fscanf_result == 4)
        {
            /* Comcolors delta from previous statistics. */
            struct cpu_stat cpu_delta;
            cpu_delta.u = cpu.u - previous_cpu_stat.u;
            cpu_delta.n = cpu.n - previous_cpu_stat.n;
            cpu_delta.s = cpu.s - previous_cpu_stat.s;
            cpu_delta.i = cpu.i - previous_cpu_stat.i;

            /* Copy current to previous. */
            memcpy(&previous_cpu_stat, &cpu, sizeof(struct cpu_stat));

            /* Comcolors user+nice+system as a fraction of total.
             * Introduce this sample to ring buffer, increment and wrap ring
             * buffer cursor. */
            float cpu_uns = cpu_delta.u + cpu_delta.n + cpu_delta.s;
            c->stats[c->ring_cursor] = cpu_uns / (cpu_uns + cpu_delta.i);
            c->ring_cursor += 1;
            if (c->ring_cursor >= c->pixmap_width)
                c->ring_cursor = 0;

            /* Redraw with the new sample. */
            redraw_pixmap(c);
        }
    }
    return TRUE;
}

static void
cpu_tooltip_update (Monitor *m)
{
    gchar tooltip_text[20];
    if (m && m->stats) {
        gint ring_pos = (m->ring_cursor == 0)
            ? m->pixmap_width - 1 : m->ring_cursor - 1;
        g_snprintf(tooltip_text, 20, "CPU usage : %.2f%%",
                m->stats[ring_pos] * 100);
        gtk_widget_set_tooltip_text(m->da, tooltip_text);
    }
}

/******************************************************************************
 *                            End of CPU Monitor                              *
 ******************************************************************************/

/******************************************************************************
 *                               RAM Monitor                                  *
 ******************************************************************************/
static gboolean
mem_update(Monitor * m)
{
    ENTER; 

    FILE *meminfo;
    int mem_total = 0;
    int mem_free  = 0;
    int mem_buffers = 0;
    int mem_cached = 0;

    if (m->stats && m->pixmap)
    {
        meminfo = fopen("/proc/meminfo", "r");
        if (!meminfo)
            RET(FALSE);

        if (fscanf(meminfo, "MemTotal: %d kB\n", &mem_total) != 1) {
            fclose (meminfo);
            RET(FALSE);
        }
        if (fscanf(meminfo, "MemFree: %d kB\n", &mem_free) != 1) {
            fclose (meminfo);
            RET(FALSE);
        }
        if (fscanf(meminfo, "Buffers: %d kB\n", &mem_buffers) != 1) {
            fclose (meminfo);
            RET(FALSE);
        }
        if (fscanf(meminfo, "Cached: %d kB\n", &mem_cached) != 1) {
            fclose (meminfo);
            RET(FALSE);
        }

        fclose(meminfo);

        /* Adding stats to the buffer:
	 * It is debatable if 'mem_buffers' counts as free or not. I'll go with
	 * no, because it may need to be flushed. mem_free definitely counts as
	 * 'free' because it is immediately released should any application
	 * need it. */
        m->stats[m->ring_cursor] = (mem_total - mem_free - mem_cached)/(float)mem_total;
        m->ring_cursor++;

        if (m->ring_cursor >= m->pixmap_width)
            m->ring_cursor = 0; 


        /* Redraw the pixmap, with the new sample */
        redraw_pixmap (m);
    }

    RET(TRUE);
}

static void
mem_tooltip_update (Monitor *m)
{
    gchar tooltip_text[20];
    if (m && m->stats) {
        gint ring_pos = (m->ring_cursor == 0)
            ? m->pixmap_width - 1 : m->ring_cursor - 1;
        g_snprintf(tooltip_text, 20, "RAM usage : %.2f%%",
                m->stats[ring_pos] * 100);
        gtk_widget_set_tooltip_text(m->da, tooltip_text);
    }
}
/******************************************************************************
 *                             End of RAM Monitor                             *
 ******************************************************************************/

/******************************************************************************
 *                            Basic events handlers                           *
 ******************************************************************************/
static gboolean
configure_event(GtkWidget* widget, GdkEventConfigure* dummy, gpointer data) 
{
    (void) dummy;

    int new_pixmap_width, new_pixmap_height;

    new_pixmap_width = widget->allocation.width - BORDER_SIZE * 2;
    new_pixmap_height = widget->allocation.height - BORDER_SIZE *2;
    Monitor *m;

    m = (Monitor *) data;

    if (new_pixmap_width > 0 && new_pixmap_height > 0)
    {
        /*
         * If the stats buffer does not exist (first time we get inside this
         * function) or its size changed, reallocate the buffer and preserve
         * existing data.
         */
        if (!m->stats || (new_pixmap_width != m->pixmap_width))
        {
            stats_set *new_stats = g_new0(stats_set, new_pixmap_width);
            
            if (!new_stats)
                return TRUE;

            if (m->stats)
            {
                /* New allocation is larger.
                 * Add new "oldest" samples of zero following the cursor*/
                if (new_pixmap_width > m->pixmap_width)
                {
                    /* Number of values between the ring cursor and the end of
                     * the buffer */
                    int nvalues = m->pixmap_width - m->ring_cursor;

                    memcpy(new_stats,
                           m->stats,
                           m->ring_cursor * sizeof (stats_set));
                    memcpy(new_stats + nvalues,
                           m->stats + m->ring_cursor,
                           nvalues * sizeof(stats_set));
                }
                /* New allocation is smaller, but still larger than the ring
                 * buffer cursor */
                else if (m->ring_cursor <= new_pixmap_width)
                {
                    /* Numver of values that can be stored between the end of
                     * the new buffer and the ring cursor */
                    int nvalues = new_pixmap_width - m->ring_cursor;
                    memcpy(new_stats,
                           m->stats,
                           m->ring_cursor * sizeof(stats_set));
                    memcpy(new_stats + m->ring_cursor,
                           m->stats + m->pixmap_width - nvalues,
                           nvalues * sizeof(stats_set));
                }
                /* New allocation is smaller, and also smaller than the ring
                 * buffer cursor.  Discard all oldest samples following the ring 
                 * buffer cursor and additional samples at the beginning of the 
                 * buffer. */
                else
                {
                    memcpy(new_stats,
                           m->stats + m->ring_cursor - new_pixmap_width,
                           new_pixmap_width * sizeof(stats_set));
                }
                g_free(m->stats);
            }
            m->stats = new_stats;
        }

        m->pixmap_width = new_pixmap_width;
        m->pixmap_height = new_pixmap_height;
        if (m->pixmap)
            g_object_unref(m->pixmap);
        m->pixmap = gdk_pixmap_new(widget->window,
                                   m->pixmap_width,
                                   m->pixmap_height,
                                   -1);
        redraw_pixmap(m);
    }
    
    return TRUE;
}

static gboolean
expose_event(GtkWidget * widget, GdkEventExpose * event, Monitor *m) 
{
    /* Draw the requested part of the pixmap onto the drawing area.
     * Translate it in both x and y by the border size. */
    if (m->pixmap != NULL)
    {
        gdk_draw_drawable (widget->window,
              m->da->style->black_gc,
              m->pixmap,
              event->area.x, event->area.y,
              event->area.x + BORDER_SIZE, event->area.y + BORDER_SIZE,
              event->area.width, event->area.height);
    }
    
    return FALSE;
    
};
/******************************************************************************
 *                       End of basic events handlers                         *
 ******************************************************************************/

static void
redraw_pixmap (Monitor *m)
{
    int i;

    /* Erase pixmap */
    gdk_draw_rectangle(m->pixmap, m->da->style->black_gc, TRUE,
                    0, 0, m->pixmap_width, m->pixmap_height);

    for (i = 0; i < m->pixmap_width; i++)
    {
        /* Draw one bar of the graph */
        gdk_draw_line(m->pixmap,
            m->graphics_context,
            i, m->pixmap_height,
        i,(1-m->stats[(m->ring_cursor +i)%m->pixmap_width])*m->pixmap_height);

    }

    /* Redraw pixmap */
    gtk_widget_queue_draw(m->da);
}


static update_func update_functions [N_MONITORS] = { 
    [CPU_POSITION] = cpu_update, 
    [MEM_POSITION] = mem_update 
};

static char *default_colors[N_MONITORS] = {
    [CPU_POSITION] = "#0000FF",
    [MEM_POSITION] = "#FF0000"
};


static tooltip_update_func tooltip_update[N_MONITORS] = {
    [CPU_POSITION] = cpu_tooltip_update,
    [MEM_POSITION] = mem_tooltip_update
};

/* Colors currently used. We cannot store them in the "struct Monitor"s where
 * they belong, because we free these when the user removes them. And since we
 * want the colors to stay the same even after removing/adding a widget... */
static char *colors[N_MONITORS] = { 
    NULL, 
    NULL 
};

/* 
 * This function is called every UPDATE_PERIOD milliseconds. It updates all
 * monitors.
 */
static gboolean
monitors_update(gpointer data)
{
    MonitorsPlugin *mp;
    int i;
    
    mp = (MonitorsPlugin *) data;
    if (!mp)
        RET(FALSE);

    for (i = 0; i < N_MONITORS; i++)
    {
        if (mp->monitors[i])
        {
            mp->monitors[i]->update(mp->monitors[i]);
            if (mp->monitors[i]->update_tooltip)
                mp->monitors[i]->update_tooltip(mp->monitors[i]);
        }
    }

    return TRUE;
}

static Monitor*
monitors_add_monitor (Plugin *p, MonitorsPlugin *mp, update_func update,
             tooltip_update_func update_tooltip, gchar *color)
{
    ENTER;

    Monitor *m;

    m = g_new0(Monitor, 1);
    m = monitor_init(p, m, color);
    m->update = update;
    m->update_tooltip = update_tooltip;
    gtk_box_pack_start(GTK_BOX(p->pwid), m->da, FALSE, FALSE, 0);
    gtk_widget_show(m->da);

    RET(m);
}

static int
monitors_constructor(Plugin *p, char **fp)
{
    ENTER;
    int i;
    MonitorsPlugin *mp;

    mp = g_new0(MonitorsPlugin, 1);
    p->priv = mp;

    p->pwid = gtk_hbox_new(TRUE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 1);
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);

    /* Apply options */
    line s;
    s.len = 256;

    if (fp)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR("%s : illegal token %s\n", PLUGIN_NAME, s.str);
                continue;
            }
            if (s.type == LINE_VAR) {
                if (g_ascii_strcasecmp(s.t[0], "DisplayCPU") == 0)
                    mp->displayed_monitors[CPU_POSITION] = atoi(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "DisplayRAM") == 0)
                    mp->displayed_monitors[MEM_POSITION] = atoi(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "CPUColor") == 0)
                    colors[CPU_POSITION] = g_strndup(s.t[1], COLOR_SIZE-1);
                else if (g_ascii_strcasecmp(s.t[0], "RAMColor") == 0)
                    colors[MEM_POSITION] = g_strndup(s.t[1], COLOR_SIZE-1);
                else {
                    ERR("%s : unknown var %s\n", PLUGIN_NAME, s.t[0]);
                    continue;
                }
            }
        }
    }
    else
    {
        /* First time we use this plugin : only display CPU usage */
        mp->displayed_monitors[CPU_POSITION] = 1;
    }
  
    /* Initializing monitors */ 
    for (i = 0; i < N_MONITORS; i++)
    {
        if (!colors[i])
            colors[i] = g_strndup(default_colors[i], COLOR_SIZE-1);

        if (mp->displayed_monitors[i])
        {
            mp->monitors[i] = monitors_add_monitor(p, mp, 
                                                   update_functions[i], 
                                                   tooltip_update[i], 
                                                   colors[i]);
        }
    }
   
    /* Adding a timer : monitors will be updated every UPDATE_PERIOD
     * milliseconds */
    mp->timer = g_timeout_add(UPDATE_PERIOD, (GSourceFunc) monitors_update, 
                              (gpointer) mp);
    RET(TRUE);
}

static void
monitors_destructor(Plugin *p)
{
    ENTER;
    int            i;
    MonitorsPlugin *mp;
    
    mp = (MonitorsPlugin *) p->priv;

    /* Removing timer */
    g_source_remove(mp->timer);
    
    /* Freeing all monitors */
    for (i = 0; i < N_MONITORS; i++)
    {
        if (mp->monitors[i])
            monitor_free(mp->monitors[i]);
    }

    g_free(mp); 

    RET();
}


static void 
monitors_config (Plugin *p, GtkWindow *parent)
{
    ENTER;

    GtkWidget *dialog;
    MonitorsPlugin *mp;

    mp = (MonitorsPlugin *) p->priv;

    dialog = create_generic_config_dlg(_(p->class->name),
        GTK_WIDGET(parent),
        (GSourceFunc) monitors_apply_config, (gpointer) p,
        _("Display CPU usage"), &mp->displayed_monitors[0], CONF_TYPE_BOOL,
        _("CPU color"), &colors[CPU_POSITION], CONF_TYPE_STR,
        _("Display RAM usage"), &mp->displayed_monitors[1], CONF_TYPE_BOOL,
        _("RAM color"), &colors[MEM_POSITION], CONF_TYPE_STR,
        NULL);
    gtk_window_present(GTK_WINDOW(dialog));    

    RET();
}

static void
monitors_apply_config (Plugin *p)
{
    ENTER;
    MonitorsPlugin *mp;
    mp = (MonitorsPlugin *) p->priv;

    int i;
    int current_n_monitors = 0;

start:
    for (i = 0; i < N_MONITORS; i++)
    {
        if (mp->displayed_monitors[i])
            current_n_monitors++;

        if (mp->displayed_monitors[i] && !mp->monitors[i])
        {
            /* We've just activated monitor<i> */
            mp->monitors[i] = monitors_add_monitor(p, mp, 
                                                   update_functions[i], 
                                                   tooltip_update[i], 
                                                   colors[i]);
            /*
             * It is probably best for users if their monitors are always
             * displayed in the same order : the CPU monitor always on the left,
             * the RAM monitor always on the right of the CPU monitor (if the
             * CPU monitor is displayed), etc. That's why we do not just use
             * gtk_box_pack_start/gtk_box_pack_end, and use
             * gtk_box_reorder_child.
             */
            gtk_box_reorder_child(GTK_BOX(p->pwid), 
                                  mp->monitors[i]->da,current_n_monitors-1);
        }
        else if (!mp->displayed_monitors[i] && mp->monitors[i])
        {
            /* We've just removed monitor<i> */
            gtk_container_remove(GTK_CONTAINER(p->pwid), mp->monitors[i]->da);
            monitor_free(mp->monitors[i]);
            mp->monitors[i] = NULL;
        }
        if (mp->monitors[i] && 
            strncmp(mp->monitors[i]->color, colors[i], COLOR_SIZE) != 0)
        {
            /* We've changed the color */
            monitor_set_foreground_color(p, mp->monitors[i], colors[i]);
        }
    }

    /* Workaround meant to prevent users to display no monitor at all.
     * FIXME : write something clean. When there is only one monitor displayed,
     * its toggle button should not be clickable in the prefs. */
    if (current_n_monitors == 0)
    {
        mp->displayed_monitors[0] = 1;
        goto start;
    }


    
    RET();
}

static void
monitors_save(Plugin *p, FILE *fp)
{
    ENTER;

    MonitorsPlugin *mp;
    
    mp = (MonitorsPlugin *) p->priv;

    lxpanel_put_bool(fp, "DisplayCPU", mp->displayed_monitors[CPU_POSITION]);
    lxpanel_put_bool(fp, "DisplayRAM", mp->displayed_monitors[MEM_POSITION]);

    if (mp->monitors[CPU_POSITION])
        lxpanel_put_str(fp, "CPUColor", colors[CPU_POSITION]);

    if (mp->monitors[MEM_POSITION])
        lxpanel_put_str(fp, "RAMColor", colors[MEM_POSITION]);

    RET();    
}

PluginClass monitors_plugin_class = {
    PLUGINCLASS_VERSIONING,
    type : "monitors",
    name : N_("Resource monitors"),
    version: "0.1", 
    description: N_("Display monitors (CPU, RAM)"),
    constructor: monitors_constructor,
    destructor : monitors_destructor,
    config: monitors_config,
    save: monitors_save,
    panel_configuration_changed: NULL
};
