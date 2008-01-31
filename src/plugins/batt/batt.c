/*
 * ACPI battery monitor plugin for LXPanel
 *
 * Copyright (C) 2007 by Greg McNew <gmcnew@gmail.com>
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


#include <dirent.h> /* used by getStatus() */
#include <glib/gi18n.h>
#include <pthread.h> /* used by pthread_create() and alarmThread */
#include <semaphore.h> /* used by update() and alarmProcess() for alarms */
#include <stdlib.h>

#include "dbg.h"
#include "gtkbgbox.h"
#include "misc.h" /* used for the line struct */
#include "panel.h" /* used to determine panel orientation */
#include "plugin.h"

#define BATTERY_DIRECTORY "/proc/acpi/battery/" /* must be slash-terminated */
#define DEFAULT_UPDATE_INTERVAL_MS 1000

/* On the initial author's system, the files in the ACPI battery directory were
   only updated once every two seconds when the power cord was left alone.
   However, the files updated the battery's state within 500 ms of the power
   cord being plugged in or removed. Update intervals significantly below 500 ms
   will not noticeably reduce the lag between the state reported by the
   operating system and the state reported by the plugin. Some safeguard is
   necessary to prevent the user from disabling their system with an
   unreasonably low update interval; 100 ms ought to be enough for anybody. */
#define MIN_UPDATE_INTERVAL_MS 100

/* The last MAX_SAMPLES samples are averaged when charge rates are evaluated.
   This helps prevent spikes in the "time left" values the user sees. */
#define MAX_SAMPLES 10


typedef struct {
    char *alarmCommand,
        *backgroundColor,
        *chargingColor1,
        *chargingColor2,
        *dischargingColor1,
        *dischargingColor2,
        *updateInterval;
    GdkColor *background,
        *charging1,
        *charging2,
        *discharging1,
        *discharging2;
    GdkGC *bg,
        *gc1,
        *gc2;
    GdkPixmap *pixmap;
    GtkTooltips *tooltip;
    GtkWidget *drawingArea,
        *eventBox;
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
        wasCharging,
        width;
    sem_t alarmProcessLock;
} batt;


typedef struct {
    char *command;
    sem_t *lock;
} alarm;


/* strToInterval converts a string containing a seconds value (like 2.5) to its
 * millisecond value (2500) */
static int strToInterval(char *str) {

    ENTER;

    if (str && str[0]) {
        int updateIntervalMs = (int) (atof(str) * 1000.0);
        if (updateIntervalMs >= MIN_UPDATE_INTERVAL_MS)
            RET(updateIntervalMs);
    }

    RET(DEFAULT_UPDATE_INTERVAL_MS);

}


/* getStatus gets the capacity, charge, and charge/discharge rates for the
 * system's battery. The return value is negative if an error occurs, 0 if the
 * battery is discharging, and 1 if the battery is charging.
 *
 * If there are multiple batteries in the system, their capacity, charge, and
 * charge/discharge rates are summed, making the batteries appear to the user as
 * a single large battery. */
static int getStatus(int *capacity, int *charge, int *rate) {

    ENTER;

    FILE *info, *state;
    DIR *batteryDirectory;
    unsigned int batteries = 0,
        numDischarging = 0;
    struct dirent *battery;

    *capacity = *charge = *rate = 0;

    if (! (batteryDirectory = opendir(BATTERY_DIRECTORY)))
        RET(-1);

    /* The buffer string is used to store file paths and also to read one line
       of a file at a time */
    line buffer;
    buffer.len = 256;

    /* Read and process each entry in the battery directory */
    while ((battery = readdir(batteryDirectory))) {
        if (battery->d_name[0] != '.') {
            batteries++;
            int thisCapacity = 0,
                thisCharge = 0,
                thisRate = 0;

            /* Open the info file */
            snprintf(buffer.str, buffer.len, "%s%s/info", BATTERY_DIRECTORY,
                    battery->d_name);
            if ((info = fopen(buffer.str, "r"))) {

                /* Read the file until the battery's capacity is found or until
                   there are no more lines to be read */
                while (fgets(buffer.str, buffer.len, info) &&
                        ! sscanf(buffer.str, "last full capacity: %d mWh",
                        &thisCapacity));

                fclose(info);

            }

            /* Open the state file */
            snprintf(buffer.str, buffer.len, "%s%s/state", BATTERY_DIRECTORY,
                    battery->d_name);
            if ((state = fopen(buffer.str, "r"))) {

                char thisState = 'c';

                /* Read the file until the battery's charging state is found or
                   until there are no more lines to be read */
                while (fgets(buffer.str, buffer.len, state) &&
                        ! sscanf(buffer.str, "charging state: %c", &thisState));

                /* Read the file until the battery's charge/discharge rate is
                   found or until there are no more lines to be read */
                while (fgets(buffer.str, buffer.len, state) &&
                        ! sscanf(buffer.str, "present rate: %d mW", &thisRate));

                /* Read the file until the battery's charge is found or until
                   there are no more lines to be read */
                while (fgets(buffer.str, buffer.len, state) &&
                        ! sscanf(buffer.str, "remaining capacity: %d mWh",
                        &thisCharge));

                /* thisState will be 'c' if the batter is charging and 'd'
                   otherwise */
                if (thisState - 'c')
                    numDischarging++;

                fclose(state);

            }

            /* Update the total capacity, charge, and charge/discharge rate */
            *capacity += thisCapacity;
            *charge += thisCharge;
            *rate += thisRate;

        }
    }
    
    closedir(batteryDirectory);

    /* If at least one battery could be found, return 0 if any batteries are
       discharging or 1 if all are charging; if no batteries could be found,
       return an error status */
    RET(batteries ? ! numDischarging : -1);

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


/* update() is the main loop of the plugin. It gets the battery's state, updates
   the visual charge indicator, sets a meaningful tooltip for the plugin, and
   runs the alarm command if necessary. */   
static int update(batt *b) {

    ENTER;

    if (! b->pixmap)
        RET(TRUE);

    int capacity,   /* unit: mWh */
        charge,     /* unit: mWh */
        isCharging,
        lastRate,   /* unit: mW */
        rate;       /* unit: mW */

    /* getStatus will return a negative value on an error */
    if ((isCharging = getStatus(&capacity, &charge, &lastRate)) < 0) {

        gdk_draw_rectangle(b->pixmap, b->bg, TRUE, 0, 0, b->width, b->height);

        /* Describe the error in a tooltip */
        line tooltip;
        tooltip.len = 256;
        snprintf(tooltip.str, tooltip.len, "No batteries found");
        gtk_tooltips_set_tip(b->tooltip, b->drawingArea, _(tooltip.str), NULL);
        gtk_widget_queue_draw(b->drawingArea);

        RET(TRUE);

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

    line tooltip;
    tooltip.len = 256;

    /* Make a tooltip string, and display remaining charge time if the battery
       is charging or remaining life if it's discharging */
    if (isCharging) {

        if (rate)
            snprintf(tooltip.str, tooltip.len,
                    "Battery: %d%% charged, %d:%02d until full",
                    capacity ? charge * 100 / capacity : 0,
                    (capacity - charge) / rate,
                    ((capacity - charge) * 60 / rate) % 60);

        /* A battery will sometimes have a charge rate of 0, even if it isn't
           finished charging */
        else
            snprintf(tooltip.str, tooltip.len,
                    "Battery: %d%% charged, %s charging",
                    capacity ? charge * 100 / capacity : 0,
                    (charge >= capacity) ? "finished" : "not");

    }
    else
        snprintf(tooltip.str, tooltip.len,
                "Battery: %d%% charged, %d:%02d left",
                capacity ? charge * 100 / capacity : 0,
                rate ? charge / rate : 0,
                rate ? (charge * 60 / rate) % 60 : 0);

    gtk_tooltips_set_tip(b->tooltip, b->drawingArea, _(tooltip.str), NULL);

    int chargeLevel = capacity ?
            charge * (b->length - 2 * b->border) / capacity : 0;

    /* Choose the right colors for the charge bar */
    if (isCharging) {
        gdk_gc_set_foreground(b->gc1, b->charging1);
        gdk_gc_set_foreground(b->gc2, b->charging2);
    }
    else {
        gdk_gc_set_foreground(b->gc1, b->discharging1);
        gdk_gc_set_foreground(b->gc2, b->discharging2);
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

    gtk_widget_queue_draw(b->drawingArea);

    RET(TRUE);

}


/* An update will be performed whenever the user clicks on the charge bar */
static gint buttonPressEvent(GtkWidget *widget, GdkEventConfigure *event,
        batt *b) {

    ENTER;

    update(b);

    RET(TRUE);

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
    update(b);

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
constructor(plugin *p, char **fp)
{

    ENTER;

    batt *b;
    p->priv = b = g_new0(batt, 1);
    b->eventBox = gtk_event_box_new();
    b->drawingArea = gtk_drawing_area_new();

    if ((b->orientation = p->panel->orientation) == ORIENT_HORIZ) {
        b->height = b->length = 20;
        b->thickness = b->width = 8;
    }
    else {
        b->height = b->thickness = 8;
        b->length = b->width = 20;
    }
    gtk_widget_set_size_request(b->drawingArea, b->width, b->height);

    gtk_container_add(GTK_CONTAINER(b->eventBox), b->drawingArea);
    gtk_widget_show_all(b->eventBox);
    gtk_widget_show(b->drawingArea);
    b->tooltip = p->panel->tooltips;

    b->bg = gdk_gc_new(p->panel->topgwin->window);
    b->gc1 = gdk_gc_new(p->panel->topgwin->window);
    b->gc2 = gdk_gc_new(p->panel->topgwin->window);
    b->background = (GdkColor *) malloc(sizeof(GdkColor));
    b->charging1 = (GdkColor *) malloc(sizeof(GdkColor));
    b->charging2 = (GdkColor *) malloc(sizeof(GdkColor));
    b->discharging1 = (GdkColor *) malloc(sizeof(GdkColor));
    b->discharging2 = (GdkColor *) malloc(sizeof(GdkColor));

    gtk_bgbox_set_background(p->pwid, BG_STYLE, 0, 0);
    gtk_container_add(GTK_CONTAINER(p->pwid), b->eventBox);
    gtk_container_set_border_width (GTK_CONTAINER (p->pwid), 1);
    g_signal_connect (G_OBJECT (b->eventBox), "button_press_event",
            G_CALLBACK(buttonPressEvent), (gpointer) b);
    g_signal_connect (G_OBJECT (b->drawingArea),"configure_event",
          G_CALLBACK (configureEvent), (gpointer) b);
    g_signal_connect (G_OBJECT (b->drawingArea), "expose_event",
          G_CALLBACK (exposeEvent), (gpointer) b);

    sem_init(&(b->alarmProcessLock), 0, 1);

    b->alarmCommand = b->backgroundColor = b->chargingColor1 = b->chargingColor2
            = b->dischargingColor1 = b->dischargingColor2 = b->updateInterval
            = NULL;

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
                if (!g_ascii_strcasecmp(s.t[0], "AlarmCommand"))
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
                else if (!g_ascii_strcasecmp(s.t[0], "UpdateInterval"))
                    b->updateInterval = g_strdup(s.t[1]);
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
                    goto error;
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
    if (! b->updateInterval)
        b->updateInterval = g_strdup("1.0");

    gdk_color_parse(b->backgroundColor, b->background);
    gdk_color_parse(b->chargingColor1, b->charging1);
    gdk_color_parse(b->chargingColor2, b->charging2);
    gdk_color_parse(b->dischargingColor1, b->discharging1);
    gdk_color_parse(b->dischargingColor2, b->discharging2);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), b->background, FALSE, TRUE);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), b->charging1, FALSE, TRUE);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), b->charging2, FALSE, TRUE);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), b->discharging1, FALSE, TRUE);
    gdk_colormap_alloc_color(gdk_drawable_get_colormap(
            p->panel->topgwin->window), b->discharging2, FALSE, TRUE);
    gdk_gc_set_foreground(b->bg, b->background);

    /* Start the update loop */
    b->timer = g_timeout_add(strToInterval(b->updateInterval),
            (GSourceFunc) update, (gpointer) b);

    RET(TRUE);

error:
    g_object_unref(b->bg);
    g_object_unref(b->gc1);
    g_object_unref(b->gc2);
    g_free(b->alarmCommand);
    g_free(b->backgroundColor);
    g_free(b->chargingColor1);
    g_free(b->chargingColor2);
    g_free(b->dischargingColor1);
    g_free(b->dischargingColor2);
    g_free(b->background);
    g_free(b->charging1);
    g_free(b->charging2);
    g_free(b->discharging1);
    g_free(b->discharging2);
    g_free(b->rateSamples);
    g_free(b->updateInterval);
    sem_destroy(&(b->alarmProcessLock));
    g_free(b);
    RET(FALSE);
}


static void
destructor(plugin *p)
{

    ENTER;

    batt *b = (batt *) p->priv;

    g_object_unref(b->pixmap);
    g_object_unref(b->bg);
    g_object_unref(b->gc1);
    g_object_unref(b->gc2);
    g_free(b->alarmCommand);
    g_free(b->backgroundColor);
    g_free(b->chargingColor1);
    g_free(b->chargingColor2);
    g_free(b->dischargingColor1);
    g_free(b->dischargingColor2);
    g_free(b->background);
    g_free(b->charging1);
    g_free(b->charging2);
    g_free(b->discharging1);
    g_free(b->discharging2);
    g_free(b->rateSamples);
    g_free(b->updateInterval);
    sem_destroy(&(b->alarmProcessLock));
    g_source_remove(b->timer);
    g_free(b);

    RET();

}


static void orientation(plugin *p) {

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


static void applyConfig(plugin* p)
{

    ENTER;

    batt *b = (batt *) p->priv;

    /* Update colors */
    if (b->backgroundColor &&
            gdk_color_parse(b->backgroundColor, b->background)) {
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), b->background, FALSE, TRUE);
        gdk_gc_set_foreground(b->bg, b->background);
    }
    if (b->chargingColor1 && gdk_color_parse(b->chargingColor1, b->charging1))
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), b->charging1, FALSE, TRUE);
    if (b->chargingColor2 && gdk_color_parse(b->chargingColor2, b->charging2))
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), b->charging2, FALSE, TRUE);
    if (b->dischargingColor1 &&
            gdk_color_parse(b->dischargingColor1, b->discharging1))
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), b->discharging1, FALSE, TRUE);
    if (b->dischargingColor2 &&
            gdk_color_parse(b->dischargingColor2, b->discharging2))
        gdk_colormap_alloc_color(gdk_drawable_get_colormap(
                p->panel->topgwin->window), b->discharging2, FALSE, TRUE);

    /* Make sure the border value is acceptable */ 
    b->border = MIN(MAX(0, b->requestedBorder),
            (MIN(b->length, b->thickness) - 1) / 2);

    /* Restart the update loop with a new interval */
    g_source_remove(b->timer);
    b->timer = g_timeout_add(strToInterval(b->updateInterval),
            (GSourceFunc) update, (gpointer) b);

    /* Resize the widget */
    if (b->orientation == ORIENT_HORIZ)
        b->width = b->thickness;
    else
        b->height = b->thickness;
    gtk_widget_set_size_request(b->drawingArea, b->width, b->height);

    RET();

}


static void config(plugin *p, GtkWindow* parent) {

    ENTER;

    GtkWidget *dialog;
    batt *b = (batt *) p->priv;
    dialog = create_generic_config_dlg(_(p->class->name),
            GTK_WIDGET(parent),
            (GSourceFunc) applyConfig, (gpointer) p,
            _("Alarm command"), &b->alarmCommand, G_TYPE_STRING,
            _("Alarm time (minutes left)"), &b->alarmTime, G_TYPE_INT,
            _("Update interval (seconds)"), &b->updateInterval, G_TYPE_STRING,
            _("Background color"), &b->backgroundColor, G_TYPE_STRING,
            _("Charging color 1"), &b->chargingColor1, G_TYPE_STRING,
            _("Charging color 2"), &b->chargingColor2, G_TYPE_STRING,
            _("Discharging color 1"), &b->dischargingColor1, G_TYPE_STRING,
            _("Discharging color 2"), &b->dischargingColor2, G_TYPE_STRING,
            _("Border width"), &b->requestedBorder, G_TYPE_INT,
            _("Size"), &b->thickness, G_TYPE_INT,
            NULL);
    gtk_window_present(GTK_WINDOW(dialog));

    RET();

}


static void save(plugin* p, FILE* fp) {

    ENTER;

    batt *b = (batt *) p->priv;

    lxpanel_put_str(fp, "AlarmCommand", b->alarmCommand);
    lxpanel_put_int(fp, "AlarmTime", b->alarmTime);
    lxpanel_put_str(fp, "BackgroundColor", b->backgroundColor);
    lxpanel_put_int(fp, "BorderWidth", b->requestedBorder);
    lxpanel_put_str(fp, "ChargingColor1", b->chargingColor1);
    lxpanel_put_str(fp, "ChargingColor2", b->chargingColor2);
    lxpanel_put_str(fp, "DischargingColor1", b->dischargingColor1);
    lxpanel_put_str(fp, "DischargingColor2", b->dischargingColor2);
    lxpanel_put_int(fp, "Size", b->thickness);
    lxpanel_put_str(fp, "UpdateInterval", b->updateInterval);

    RET();

}


plugin_class batt_plugin_class = {
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
