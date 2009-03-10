/*
 * ACPI battery monitor plugin for LXPanel
 *
 * Copyright (C) 2007 by Greg McNew <gmcnew@gmail.com>
 * Copyright (C) 2008 by Hong Jen Yee <pcman.tw@gmail.com>
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
 *
 * This plugin monitors battery usage on ACPI-enabled systems by reading the
 * battery information found in /proc/acpi/battery/. The update interval is
 * user-configurable and defaults to 1 second.
 *
 * The battery's remaining life is estimated from its current charge and current
 * rate of discharge. The user may configure an alarm command to be run when
 * their estimated remaining battery life reaches a certain level.
 */

/* FIXME:
 *  Here are somethings need to be improvec:
 *  1. Replace pthread stuff with gthread counterparts for portability.
 *  2. Check "/proc/acpi/ac_adapter" for AC power.
 *  3. Add an option to hide the plugin when AC power is used or there is no battery.
 *  4. Handle failure gracefully under systems other than Linux.
*/

#include <glib.h>
#include <glib/gi18n.h>
#include <pthread.h> /* used by pthread_create() and alarmThread */
#include <semaphore.h> /* used by update() and alarmProcess() for alarms */
#include <stdlib.h>
#include <string.h>

#include "dbg.h"
#include "misc.h" /* used for the line struct */
#include "panel.h" /* used to determine panel orientation */
#include "plugin.h"
#include "glib-mem.h" /* compatibility macros for g_slice* */

#define BATTERY_DIRECTORY "/proc/acpi/battery/" /* must be slash-terminated */
#define BATTERY_SYSFS_DIRECTORY "/sys/class/power_supply/"
#define AC_ADAPTER_STATE_FILE "/proc/acpi/ac_adapter/AC0/state"
#define AC_ADAPTER_STATE_SYSFS_FILE "/sys/class/power_supply/AC0/online"

/* The last MAX_SAMPLES samples are averaged when charge rates are evaluated.
   This helps prevent spikes in the "time left" values the user sees. */
#define MAX_SAMPLES 10

typedef struct {
    char* name;
    int capacity,   /* unit: mWh */
        charge,     /* unit: mWh */
        is_charging,
        last_rate,   /* unit: mW */
        rate;       /* unit: mW */
}batt_info;

typedef struct {
    char *alarmCommand,
        *backgroundColor,
        *chargingColor1,
        *chargingColor2,
        *dischargingColor1,
        *dischargingColor2;
    GdkColor background,
        charging1,
        charging2,
        discharging1,
        discharging2;
    GdkGC *bg,
        *gc1,
        *gc2;
    GdkPixmap *pixmap;
    GtkWidget *drawingArea;
    int orientation;
    unsigned int alarmTime,
        border,
        height,
        length,
        numSamples,
        requestedBorder,
        *rateSamples,
        rateSamplesSum,
        thickness,
        timer,
        state_elapsed_time,
        info_elapsed_time,
        wasCharging,
        width,
        hide_if_no_battery;
    sem_t alarmProcessLock;
    GList* batteries;
    gboolean has_ac_adapter;
    gboolean use_sysfs;
} batt;


typedef struct {
    char *command;
    sem_t *lock;
} alarm;

static void destructor(Plugin *p);
static void update_display(batt *b, gboolean repaint);

static void batt_info_free( batt_info* bi )
{
    g_free( bi->name );
    g_slice_free( batt_info, bi );
}

static gboolean get_batt_info( batt_info* bi, gboolean use_sysfs )
{
    FILE *info;
    char buf[ 256 ];

    /* Open the info file */
    if (use_sysfs)
        g_snprintf(buf, 256, "%s%s/charge_full", BATTERY_SYSFS_DIRECTORY, bi->name);
    else
        g_snprintf(buf, 256, "%s%s/info", BATTERY_DIRECTORY, bi->name);
    if ((info = fopen(buf, "r"))) {
        /* Read the file until the battery's capacity is found or until
           there are no more lines to be read */
        if (use_sysfs)
            while( fgets(buf, 256, info) &&
                    ! sscanf(buf, "%ld", &bi->capacity) );
        else
            while( fgets(buf, 256, info) &&
                    ! sscanf(buf, "last full capacity: %d",
                    &bi->capacity) );
        fclose(info);
        return TRUE;
    }
    return FALSE;
}

static gboolean get_batt_state( batt_info* bi, gboolean use_sysfs )
{
    FILE *state;
    char buf[ 512 ];
    
    if (use_sysfs)
        g_snprintf( buf, 512, "%s%s/uevent", BATTERY_SYSFS_DIRECTORY, bi->name );
    else
        g_snprintf( buf, 512, "%s%s/state", BATTERY_DIRECTORY, bi->name );
    if((state = fopen( buf, "r"))) {
        char *pstr;
        fread(buf, sizeof(buf), 1, state);

        char thisState = 'c';

        if (use_sysfs) {
            /* Read the file until the battery's charging state is found or
               until there are no more lines to be read */
            if (pstr = strstr(buf, "POWER_SUPPLY_STATUS="))
                thisState = *(pstr + 20);

            /* Read the file until the battery's charge/discharge rate is
               found or until there are no more lines to be read */
            if (pstr = strstr(buf, "POWER_SUPPLY_CURRENT_NOW=")) {
                pstr += 25;
                sscanf (pstr, "%ld",&bi->rate );

                if( bi->rate < 0 )
                    bi->rate = 0;
            }

            /* Read the file until the battery's charge is found or until
               there are no more lines to be read */
            if (pstr = strstr (buf, "POWER_SUPPLY_CHARGE_NOW=")) {
                pstr += 24;
                sscanf (pstr, "%ld", &bi->charge);
            }

            /* thisState will be 'c' if the batter is charging and 'd'
               otherwise */
            bi->is_charging = !( thisState - 'C' );
        } else {
            /* Read the file until the battery's charging state is found or
               until there are no more lines to be read */
            if (pstr = strstr(buf, "charging state:"))
                thisState = *(pstr + 25);

            /* Read the file until the battery's charge/discharge rate is
               found or until there are no more lines to be read */
            if (pstr = strstr(buf, "present rate:")) {
                //pstr += 13;
                pstr += 25;
                sscanf (pstr, "%d",&bi->rate );

                if( bi->rate < 0 )
                    bi->rate = 0;
            }

            /* Read the file until the battery's charge is found or until
               there are no more lines to be read */
            if (pstr = strstr (buf, "remaining capacity:")) {
                pstr += 25;
                sscanf (pstr, "%d",&bi->charge);
            }
	    
            /* thisState will be 'c' if the batter is charging and 'd'
               otherwise */
            bi->is_charging = !( thisState - 'c' );
        }



        fclose(state);
        return TRUE;
    }
    return FALSE;
}

static gboolean check_ac_adapter( batt* b )
{
    FILE *state;
    char buf[ 256 ];
    char* pstr;

    if (!(state = fopen( AC_ADAPTER_STATE_FILE, "r"))) {
        if ((state = fopen( AC_ADAPTER_STATE_SYSFS_FILE, "r"))) {
			b->use_sysfs = TRUE;
        } else {
            return FALSE;
        }
    }

    gboolean has_ac_adapter = FALSE;
    if (b->use_sysfs) {
        while( fgets(buf, 256, state) &&
                ! sscanf(buf, "%d", &has_ac_adapter) );
    } else {
        while( fgets(buf, 256, state) &&
                ! ( pstr = strstr(buf, "state:") ) );
        if( pstr )
        {
            pstr += 6;
            while( *pstr && *pstr == ' ' )
                ++pstr;
            if( pstr[0] == 'o' && pstr[1] == 'n' )
                has_ac_adapter = TRUE;
        }
    }

    fclose(state);

    /* if the state of AC adapter changed, is_charging of the batteries might change, too. */
    if( has_ac_adapter != b->has_ac_adapter )
    {
        /* g_debug( "ac_state_changed: %d", has_ac_adapter ); */
        b->has_ac_adapter = has_ac_adapter;
        /* update the state of all batteries */
        g_list_foreach( b->batteries, (GFunc)get_batt_state,
                &b->use_sysfs );
        update_display( b, TRUE );
    }
    return TRUE;
}

/* alarmProcess takes the address of a dynamically allocated alarm struct (which
   it must free). It ensures that alarm commands do not run concurrently. */
static void * alarmProcess(void *arg) {
    alarm *a = (alarm *) arg;

    sem_wait(a->lock);
    system(a->command);
    sem_post(a->lock);

    g_free(a);
    return NULL;
}


/* addRate adds a charge/discharge rate to the array of samples and returns the
   average of all the rates in the array */
static int addRate(batt *b, int isCharging, int lastRate) {

    /* Clear the rate samples array if the charge/discharge status has just
       changed */
    if (b->wasCharging != isCharging) {
        b->wasCharging = isCharging;
        b->numSamples = b->rateSamplesSum = 0;
    }

    /* The rateSamples array acts as a circular array-based queue with a fixed
       size. If it is full, there's a meaningful value at index numSamples which
       should be subtracted from the sum before the new value is added; if it
       isn't full, the value at index numSamples does not need to be
       considered. */
    int currentIndex = b->numSamples % MAX_SAMPLES;
    if (b->numSamples >= MAX_SAMPLES)
        b->rateSamplesSum -= b->rateSamples[currentIndex];
    b->rateSamples[currentIndex] = lastRate;
    b->rateSamplesSum += lastRate;

    /* Increment numSamples, but don't let it get too big. As long as it's
       greater than MAX_SAMPLES, we'll know that the next sample will be
       replacing an older one. */
    if (++b->numSamples >= MAX_SAMPLES * 2)
        b->numSamples -= MAX_SAMPLES;

    RET(b->rateSamplesSum / MIN(b->numSamples, MAX_SAMPLES));

}

/* FIXME:
   Don't repaint if percentage of remaining charge and remaining time aren't changed. */
void update_display(batt *b, gboolean repaint) {
    GList* l;
    char tooltip[ 256 ];

    if (! b->pixmap)
        return;

    int capacity = 0,   /* unit: mWh */
        charge = 0,     /* unit: mWh */
        isCharging = 0,
        lastRate = 0,   /* unit: mW */
        rate = 0;       /* unit: mW */

    /* draw background */
    gdk_draw_rectangle(b->pixmap, b->bg, TRUE, 0, 0, b->width, b->height);

    if( b->batteries )
    {
        /* Calculate the total capacity, charge, and charge/discharge rate */
        for( l = b->batteries; l; l = l->next )
        {
            batt_info* bi = (batt_info*)l->data;
            capacity += bi->capacity;
            charge += bi->charge;
            lastRate += bi->rate;
            if( bi->is_charging )
                isCharging = TRUE;
        }

        /* Add the last rate to the array of recent samples and get the average
           rate */
        rate = addRate(b, isCharging, lastRate);

        /* Consider running the alarm command */
        if (! isCharging && rate && charge * 60 / rate <= b->alarmTime) {

            /* Alarms should not run concurrently; determine whether an alarm is
               already running */
            int alarmCanRun;
            sem_getvalue(&(b->alarmProcessLock), &alarmCanRun);

            /* Run the alarm command if it isn't already running */
            if (alarmCanRun) {

                alarm *a = (alarm *) malloc(sizeof(alarm));
                a->command = b->alarmCommand;
                a->lock = &(b->alarmProcessLock);

                /* Manage the alarm process in a new thread, which which will be
                   responsible for freeing the alarm struct it's given */
                pthread_t alarmThread;
                pthread_create(&alarmThread, NULL, alarmProcess, a);

            }
        }

        /* Make a tooltip string, and display remaining charge time if the battery
           is charging or remaining life if it's discharging */
        if (isCharging) {

            if (rate)
                snprintf(tooltip, 256,
                        _("Battery: %d%% charged, %d:%02d until full"),
                        capacity ? charge * 100 / capacity : 0,
                        (capacity - charge) / rate,
                        ((capacity - charge) * 60 / rate) % 60);

            /* A battery will sometimes have a charge rate of 0, even if it isn't
               finished charging */
            else
                snprintf(tooltip, 256,
                        _("Battery: %d%% charged, %s"),
                        capacity ? charge * 100 / capacity : 0,
                        (charge >= capacity) ? _("charging finished") : _("charging") );
        } else {
            /* if we have enough rate information for battery */
            if (rate) {
                snprintf(tooltip, 256,
                        _("Battery: %d%% charged, %d:%02d left"),
                        capacity ? charge * 100 / capacity : 0,
                        charge / rate,
                        (charge * 60 / rate) % 60);
            } else {
                snprintf(tooltip, 256,
                        _("Battery: %d%% charged"),
                        capacity ? charge * 100 / capacity : 0);
            }
        }

        gtk_widget_set_tooltip_text(b->drawingArea, tooltip);

        int chargeLevel = capacity ?
                charge * (b->length - 2 * b->border) / capacity : 0;

        /* Choose the right colors for the charge bar */
        if (isCharging) {
            gdk_gc_set_foreground(b->gc1, &b->charging1);
            gdk_gc_set_foreground(b->gc2, &b->charging2);
        }
        else {
            gdk_gc_set_foreground(b->gc1, &b->discharging1);
            gdk_gc_set_foreground(b->gc2, &b->discharging2);
        }

        gdk_draw_rectangle(b->pixmap, b->bg, TRUE, 0, 0, b->width, b->height);

        if (b->orientation == ORIENT_HORIZ) {

            /* Draw the battery bar vertically, using color 1 for the left half and
               color 2 for the right half */
            gdk_draw_rectangle(b->pixmap, b->gc1, TRUE, b->border,
                    b->height - b->border - chargeLevel, b->width / 2
                    - b->border, chargeLevel);
            gdk_draw_rectangle(b->pixmap, b->gc2, TRUE, b->width / 2,
                    b->height - b->border - chargeLevel, (b->width + 1) / 2
                    - b->border, chargeLevel);

        }
        else {

            /* Draw the battery bar horizontally, using color 1 for the top half and
               color 2 for the bottom half */
            gdk_draw_rectangle(b->pixmap, b->gc1, TRUE, b->border,
                    b->border, chargeLevel, b->height / 2 - b->border);
            gdk_draw_rectangle(b->pixmap, b->gc2, TRUE, b->border, (b->height + 1)
                    / 2, chargeLevel, b->height / 2 - b->border);

        }
    }
    else    /* no battery is found */
    {
        char tip[ 256 ];
        g_snprintf( tip, 256, _("No batteries found") );
        gtk_widget_set_tooltip_text( b->drawingArea, tip );
    }

    if( repaint )
        gtk_widget_queue_draw( b->drawingArea );
}

static void check_batteries( batt* b )
{
    GDir *batteryDirectory;
    const char *battery_name;
    GList* l;
    gboolean need_update_display = FALSE;
    b->use_sysfs = FALSE;

    if (! (batteryDirectory = g_dir_open(BATTERY_DIRECTORY, 0, NULL)))
    {
        if (! (batteryDirectory = g_dir_open(BATTERY_SYSFS_DIRECTORY, 0, NULL))) {
            g_list_foreach( b->batteries, (GFunc)batt_info_free, NULL );
            g_list_free( b->batteries );
            b->batteries = NULL;
            return;
        }
	    b->use_sysfs = TRUE;
    }

    /* Remove dead entries */
    for( l = b->batteries; l; )
    {
        GList* next = l->next;
        batt_info* bi = (batt_info*)l->data;
        char* path;
        if (b->use_sysfs)
            path = g_build_filename( BATTERY_SYSFS_DIRECTORY, bi->name, NULL );
        else
            path = g_build_filename( BATTERY_DIRECTORY, bi->name, NULL );

        if( ! g_file_test( path, G_FILE_TEST_EXISTS ) ) /* file no more exists */
        {
            b->batteries = g_list_remove_link( b->batteries, l );   /* remove from the list */
            need_update_display = TRUE;
        }
        g_free( path );
        l = next;
    }

    /* Scan the battery directory for available batteries */
    while ((battery_name = g_dir_read_name(batteryDirectory))) {
        if (battery_name[0] != '.'&&strncmp(battery_name, "BAT", 3)==0) {
            /* find the battery in our list */
            for( l = b->batteries; l; l = l->next )
            {
                batt_info* bi = (batt_info*)l->data;
                if( 0 == strcmp( bi->name, battery_name ) )
                    break;
            }
            if( ! l ) /* not found, this is a new battery */
            {
                batt_info* bi = g_slice_new0( batt_info );
                bi->name = g_strdup( battery_name );
                /* get battery info & state for the newly added entry */
                get_batt_info(bi, b->use_sysfs);
                get_batt_state(bi, b->use_sysfs);
                b->batteries = g_list_prepend( b->batteries, bi );  /* add to our list */
                need_update_display = TRUE;
            }
        }
    }
    g_dir_close(batteryDirectory);

    if( need_update_display )
        update_display( b, TRUE );
}

/* This callback is called every 3 seconds */
static int update_timout(batt *b) {
    GList* l;
    GDK_THREADS_ENTER();
    ++b->state_elapsed_time;
    ++b->info_elapsed_time;

    /* check the existance of batteries every 3 seconds */
    check_batteries( b );

    /* check the existance of AC adapter every 3 seconds,
     * and update charging state of batteries if needed. */
    check_ac_adapter( b );

    /* check state of batteries every 30 seconds */
    if( b->state_elapsed_time == 30/3 )  /* 30 sec */
    {
        /* update state of batteries */
        for( l = b->batteries; l; l = l->next )
            get_batt_state( (batt_info*)l->data, b->use_sysfs );
        b->state_elapsed_time = 0;
    }
    /* check the capacity of batteries every 1 hour */
    if( b->info_elapsed_time == 3600/3 )  /* 1 hour */
    {
        /* update info of batteries */
        for( l = b->batteries; l; l = l->next )
            get_batt_info( (batt_info*)l->data, b->use_sysfs );
        b->info_elapsed_time = 0;
    }

    update_display( b, TRUE );

    GDK_THREADS_LEAVE();
    return TRUE;
}

/* An update will be performed whenever the user clicks on the charge bar */
static gint buttonPressEvent(GtkWidget *widget, GdkEventButton *event,
        Plugin* plugin) {

    batt *b = (batt*)plugin->priv;

    update_display(b, TRUE);

    if( event->button == 3 )  /* right button */
    {
        GtkMenu* popup = lxpanel_get_panel_menu( plugin->panel, plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, event->button, event->time );
        return TRUE;
    }
    return FALSE;
}


static gint configureEvent(GtkWidget *widget, GdkEventConfigure *event,
        batt *b) {

    ENTER;

    if (b->pixmap)
        g_object_unref(b->pixmap);

    /* Update the plugin's dimensions */
    b->width = widget->allocation.width;
    b->height = widget->allocation.height;
    if (b->orientation == ORIENT_HORIZ) {
        b->length = b->height;
        b->thickness = b->width;
    }
    else {
        b->length = b->width;
        b->thickness = b->height;
    }

    b->pixmap = gdk_pixmap_new (widget->window, widget->allocation.width,
          widget->allocation.height, -1);

    /* Perform an update so the bar will look right in its new orientation */
    update_display(b, FALSE);

    RET(TRUE);

}


static gint exposeEvent(GtkWidget *widget, GdkEventExpose *event, batt *b) {

    ENTER;

    gdk_draw_drawable (widget->window, b->drawingArea->style->black_gc,
            b->pixmap, event->area.x, event->area.y, event->area.x,
            event->area.y, event->area.width, event->area.height);

    RET(FALSE);

}


static int
constructor(Plugin *p, char **fp)
{
    ENTER;

    batt *b;
    p->priv = b = g_new0(batt, 1);
    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( p->pwid, GTK_NO_WINDOW );
    gtk_container_set_border_width( GTK_CONTAINER(p->pwid), 1 );

    b->drawingArea = gtk_drawing_area_new();
    gtk_widget_add_events( b->drawingArea, GDK_BUTTON_PRESS_MASK );

    gtk_container_add( (GtkContainer*)p->pwid, b->drawingArea );

    if ((b->orientation = p->panel->orientation) == ORIENT_HORIZ) {
        b->height = b->length = 20;
        b->thickness = b->width = 8;
    }
    else {
        b->height = b->thickness = 8;
        b->length = b->width = 20;
    }
    gtk_widget_set_size_request(b->drawingArea, b->width, b->height);

    gtk_widget_show(b->drawingArea);

    b->bg = gdk_gc_new(p->panel->topgwin->window);
    b->gc1 = gdk_gc_new(p->panel->topgwin->window);
    b->gc2 = gdk_gc_new(p->panel->topgwin->window);

    g_signal_connect (G_OBJECT (b->drawingArea), "button_press_event",
            G_CALLBACK(buttonPressEvent), (gpointer) p);
    g_signal_connect (G_OBJECT (b->drawingArea),"configure_event",
          G_CALLBACK (configureEvent), (gpointer) b);
    g_signal_connect (G_OBJECT (b->drawingArea), "expose_event",
          G_CALLBACK (exposeEvent), (gpointer) b);

    sem_init(&(b->alarmProcessLock), 0, 1);

    b->alarmCommand = b->backgroundColor = b->chargingColor1 = b->chargingColor2
            = b->dischargingColor1 = b->dischargingColor2 = NULL;

    /* Set default values for integers */
    b->alarmTime = 5;
    b->requestedBorder = 1;
    b->numSamples = b->rateSamplesSum = b->wasCharging = 0;

    b->rateSamples = (unsigned int *) malloc(sizeof(int) * MAX_SAMPLES);

    line s;
    s.len = 256;

    if (fp) {

        /* Apply options */
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "batt: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "HideIfNoBattery"))
                    b->hide_if_no_battery = atoi(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "AlarmCommand"))
                    b->alarmCommand = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "BackgroundColor"))
                    b->backgroundColor = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "ChargingColor1"))
                    b->chargingColor1 = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "ChargingColor2"))
                    b->chargingColor2 = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "DischargingColor1"))
                    b->dischargingColor1 = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "DischargingColor2"))
                    b->dischargingColor2 = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "AlarmTime"))
                    b->alarmTime = atoi(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "BorderWidth"))
                    b->requestedBorder = atoi(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "Size")) {
                    b->thickness = MAX(1, atoi(s.t[1]));
                    if (b->orientation == ORIENT_HORIZ)
                        b->width = b->thickness;
                    else
                        b->height = b->thickness;
                    gtk_widget_set_size_request(b->drawingArea, b->width,
                            b->height);
                }
                else {
                    ERR( "batt: unknown var %s\n", s.t[0]);
                    continue;
                }
            }
            else {
                ERR( "batt: illegal in this context %s\n", s.str);
                goto error;
            }
        }

    }

    /* Make sure the border value is acceptable */
    b->border = MIN(MAX(0, b->requestedBorder),
            (MIN(b->length, b->thickness) - 1) / 2);

    /* Apply more default options */
    if (! b->alarmCommand)
        b->alarmCommand = g_strdup("xmessage Battery low");
    if (! b->backgroundColor)
        b->backgroundColor = g_strdup("black");
    if (! b->chargingColor1)
        b->chargingColor1 = g_strdup("#28f200");
    if (! b->chargingColor2)
        b->chargingColor2 = g_strdup("#22cc00");
    if (! b->dischargingColor1)
        b->dischargingColor1 = g_strdup("#ffee00");
    if (! b->dischargingColor2)
        b->dischargingColor2 = g_strdup("#d9ca00");

    gdk_color_parse(b->backgroundColor, &b->background);
    gdk_color_parse(b->chargingColor1, &b->charging1);
    gdk_color_parse(b->chargingColor2, &b->charging2);
    gdk_color_parse(b->dischargingColor1, &b->discharging1);
    gdk_color_parse(b->dischargingColor2, &b->discharging2);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), &b->background, FALSE, TRUE);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), &b->charging1, FALSE, TRUE);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), &b->charging2, FALSE, TRUE);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), &b->discharging1, FALSE, TRUE);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), &b->discharging2, FALSE, TRUE);
    gdk_gc_set_foreground(b->bg, &b->background);

    check_batteries( b );   /* get available batteries */

    /* Start the update loop */
#if GTK_CHECK_VERSION( 2, 14, 0 )
    b->timer = g_timeout_add_seconds( 3, (GSourceFunc) update_timout, (gpointer) b);
#else
    b->timer = g_timeout_add( 3000,
            (GSourceFunc) update_timout, (gpointer) b);
#endif
    RET(TRUE);

error:
    destructor( p );
    RET(FALSE);
}


static void
destructor(Plugin *p)
{
    ENTER;

    batt *b = (batt *) p->priv;

    if (b->pixmap)
        g_object_unref(b->pixmap);

    g_object_unref(b->gc1);
    g_object_unref(b->gc2);
    g_free(b->alarmCommand);
    g_free(b->backgroundColor);
    g_free(b->chargingColor1);
    g_free(b->chargingColor2);
    g_free(b->dischargingColor1);
    g_free(b->dischargingColor2);

    g_free(b->rateSamples);
    sem_destroy(&(b->alarmProcessLock));
    g_source_remove(b->timer);
    g_free(b);

    RET();

}


static void orientation(Plugin *p) {

    ENTER;

    batt *b = (batt *) p->priv;

    if (b->orientation != p->panel->orientation) {
        b->orientation = p->panel->orientation;
        unsigned int swap = b->height;
        b->height = b->width;
        b->width = swap;
        gtk_widget_set_size_request(b->drawingArea, b->width, b->height);
    }

    RET();
}


static void applyConfig(Plugin* p)
{
    ENTER;

    batt *b = (batt *) p->priv;

    /* Update colors */
    if (b->backgroundColor &&
            gdk_color_parse(b->backgroundColor, &b->background)) {
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), &b->background, FALSE, TRUE);
        gdk_gc_set_foreground(b->bg, &b->background);
    }
    if (b->chargingColor1 && gdk_color_parse(b->chargingColor1, &b->charging1))
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), &b->charging1, FALSE, TRUE);
    if (b->chargingColor2 && gdk_color_parse(b->chargingColor2, &b->charging2))
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), &b->charging2, FALSE, TRUE);
    if (b->dischargingColor1 &&
            gdk_color_parse(b->dischargingColor1, &b->discharging1))
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), &b->discharging1, FALSE, TRUE);
    if (b->dischargingColor2 &&
            gdk_color_parse(b->dischargingColor2, &b->discharging2))
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), &b->discharging2, FALSE, TRUE);

    /* Make sure the border value is acceptable */
    b->border = MIN(MAX(0, b->requestedBorder),
            (MIN(b->length, b->thickness) - 1) / 2);

    /* Resize the widget */
    if (b->orientation == ORIENT_HORIZ)
        b->width = b->thickness;
    else
        b->height = b->thickness;
    gtk_widget_set_size_request(b->drawingArea, b->width, b->height);

    RET();
}


static void config(Plugin *p, GtkWindow* parent) {
    ENTER;

    GtkWidget *dialog;
    batt *b = (batt *) p->priv;
    dialog = create_generic_config_dlg(_(p->class->name),
            GTK_WIDGET(parent),
            (GSourceFunc) applyConfig, (gpointer) p,
#if 0
            _("Hide if there is no battery"), &b->hide_if_no_battery, CONF_TYPE_BOOL,
#endif
            _("Alarm command"), &b->alarmCommand, CONF_TYPE_STR,
            _("Alarm time (minutes left)"), &b->alarmTime, CONF_TYPE_INT,
            _("Background color"), &b->backgroundColor, CONF_TYPE_STR,
            _("Charging color 1"), &b->chargingColor1, CONF_TYPE_STR,
            _("Charging color 2"), &b->chargingColor2, CONF_TYPE_STR,
            _("Discharging color 1"), &b->dischargingColor1, CONF_TYPE_STR,
            _("Discharging color 2"), &b->dischargingColor2, CONF_TYPE_STR,
            _("Border width"), &b->requestedBorder, CONF_TYPE_INT,
            _("Size"), &b->thickness, CONF_TYPE_INT,
            NULL);
    gtk_window_present(GTK_WINDOW(dialog));

    RET();
}


static void save(Plugin* p, FILE* fp) {
    batt *b = (batt *) p->priv;
    char l_char=(char)b->hide_if_no_battery;

    lxpanel_put_str(fp, "HideIfNoBattery",&l_char);
    lxpanel_put_str(fp, "AlarmCommand", b->alarmCommand);
    lxpanel_put_int(fp, "AlarmTime", b->alarmTime);
    lxpanel_put_str(fp, "BackgroundColor", b->backgroundColor);
    lxpanel_put_int(fp, "BorderWidth", b->requestedBorder);
    lxpanel_put_str(fp, "ChargingColor1", b->chargingColor1);
    lxpanel_put_str(fp, "ChargingColor2", b->chargingColor2);
    lxpanel_put_str(fp, "DischargingColor1", b->dischargingColor1);
    lxpanel_put_str(fp, "DischargingColor2", b->dischargingColor2);
    lxpanel_put_int(fp, "Size", b->thickness);
}


PluginClass batt_plugin_class = {
    fname       : NULL,
    count       : 0,

    type        : "batt",
    name        : N_("Battery Monitor"),
    version     : "1.0",
    description : N_("Display battery status using ACPI"),

    constructor : constructor,
    destructor  : destructor,
    config      : config,
    save        : save,
    orientation : orientation
};
