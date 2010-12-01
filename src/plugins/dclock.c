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

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#define DEFAULT_TIP_FORMAT    "%A %x"
#define DEFAULT_CLOCK_FORMAT  "%R"

/* Private context for digital clock plugin. */
typedef struct {
    Plugin * plugin;				/* Back pointer to Plugin */
    GtkWidget * clock_label;			/* Label containing clock value */
    GtkWidget * clock_icon;			/* Icon when icon_only */
    GtkWidget * calendar_window;		/* Calendar window, if it is being displayed */
    char * clock_format;			/* Format string for clock value */
    char * tooltip_format;			/* Format string for tooltip value */
    char * action;				/* Command to execute on a click */
    gboolean bold;				/* True if bold font */
    gboolean icon_only;				/* True if icon only (no clock value) */
    guint timer;				/* Timer for periodic update */
    enum {
	AWAITING_FIRST_CHANGE,			/* Experimenting to determine interval, waiting for first change */
	AWAITING_SECOND_CHANGE,			/* Experimenting to determine interval, waiting for second change */
	ONE_SECOND_INTERVAL,			/* Determined that one second interval is necessary */
	ONE_MINUTE_INTERVAL			/* Determined that one minute interval is sufficient */
    } expiration_interval;			
    int experiment_count;			/* Count of experiments that have been done to determine interval */
    char * prev_clock_value;			/* Previous value of clock */
    char * prev_tooltip_value;			/* Previous value of tooltip */
} DClockPlugin;

static void dclock_popup_map(GtkWidget * widget, DClockPlugin * dc);
static GtkWidget * dclock_create_calendar(DClockPlugin * dc);
static gboolean dclock_button_press_event(GtkWidget * widget, GdkEventButton * evt, Plugin * plugin);
static void dclock_timer_set(DClockPlugin * dc);
static gboolean dclock_update_display(DClockPlugin * dc);
static int dclock_constructor(Plugin * p, char ** fp);
static void dclock_destructor(Plugin * p);
static void dclock_apply_configuration(Plugin * p);
static void dclock_configure(Plugin * p, GtkWindow * parent);
static void dclock_save_configuration(Plugin * p, FILE * fp);
static void dclock_panel_configuration_changed(Plugin * p);

/* Handler for "map" signal on popup window. */
static void dclock_popup_map(GtkWidget * widget, DClockPlugin * dc)
{
    plugin_adjust_popup_position(widget, dc->plugin);
}

/* Display a window containing the standard calendar widget. */
static GtkWidget * dclock_create_calendar(DClockPlugin * dc)
{
    /* Create a new window. */
    GtkWidget * win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(win), 180, 180);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable(GTK_WINDOW(win), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(win), 5);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(win), TRUE);
    gtk_window_stick(GTK_WINDOW(win));

    /* Create a vertical box as a child of the window. */
    GtkWidget * box = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(win), GTK_WIDGET(box));

    /* Create a standard calendar widget as a child of the vertical box. */
    GtkWidget * calendar = gtk_calendar_new();
    gtk_calendar_display_options(
        GTK_CALENDAR(calendar),
        GTK_CALENDAR_SHOW_WEEK_NUMBERS | GTK_CALENDAR_SHOW_DAY_NAMES | GTK_CALENDAR_SHOW_HEADING);
    gtk_box_pack_start_defaults(GTK_BOX(box), calendar);

    /* Connect signals. */
    g_signal_connect(G_OBJECT(win), "map", G_CALLBACK(dclock_popup_map), dc);

    /* Return the widget. */
    return win;
}

/* Handler for "button-press-event" event from main widget. */
static gboolean dclock_button_press_event(GtkWidget * widget, GdkEventButton * evt, Plugin * plugin)
{
    DClockPlugin * dc = (DClockPlugin *) plugin->priv;

    /* Standard right-click handling. */
    if (plugin_button_press_event(widget, evt, plugin))
        return TRUE;

    /* If an action is set, execute it. */
    if (dc->action != NULL)
        g_spawn_command_line_async(dc->action, NULL);

    /* If no action is set, toggle the presentation of the calendar. */
    else
    {
        if (dc->calendar_window == NULL)
        {
            dc->calendar_window = dclock_create_calendar(dc);
            gtk_widget_show_all(dc->calendar_window);
        }
        else
        {
            gtk_widget_destroy(dc->calendar_window);
            dc->calendar_window = NULL;
        }
    }
    return TRUE;
}

/* Set the timer. */
static void dclock_timer_set(DClockPlugin * dc)
{
    int milliseconds = 1000;

    /* Get current time to millisecond resolution. */
    struct timeval current_time;
    if (gettimeofday(&current_time, NULL) >= 0)
    {
        /* Compute number of milliseconds until next second boundary. */
        milliseconds = 1000 - (current_time.tv_usec / 1000);

        /* If the expiration interval is the minute boundary,
         * add number of milliseconds after that until next minute boundary. */
        if (dc->expiration_interval == ONE_MINUTE_INTERVAL)
        {
            time_t seconds = 60 - (current_time.tv_sec - (current_time.tv_sec / 60) * 60);
            milliseconds += seconds * 1000;
        }
    }

    /* Be defensive, and set the timer. */
    if (milliseconds <= 0)
        milliseconds = 1000;
    dc->timer = g_timeout_add(milliseconds, (GSourceFunc) dclock_update_display, (gpointer) dc);
}

/* Periodic timer callback.
 * Also used during initialization and configuration change to do a redraw. */
static gboolean dclock_update_display(DClockPlugin * dc)
{
    /* Determine the current time. */
    time_t now;
    time(&now);
    struct tm * current_time = localtime(&now);

    /* Determine the content of the clock label and tooltip. */
    char clock_value[64];
    char tooltip_value[64];
    clock_value[0] = '\0';
    if (dc->clock_format != NULL)
        strftime(clock_value, sizeof(clock_value), dc->clock_format, current_time);
    tooltip_value[0] = '\0';
    if (dc->tooltip_format != NULL)
        strftime(tooltip_value, sizeof(tooltip_value), dc->tooltip_format, current_time);

    /* When we write the clock value, it causes the panel to do a full relayout.
     * Since this function may be called too often while the timing experiment is underway,
     * we take the trouble to check if the string actually changed first. */
    if (( ! dc->icon_only)
    && ((dc->prev_clock_value == NULL) || (strcmp(dc->prev_clock_value, clock_value) != 0)))
    {
        /* Convert "\n" escapes in the user's format string to newline characters. */
        char * newlines_converted = NULL;
        if (strstr(clock_value, "\\n") != NULL)
        {
            newlines_converted = g_strdup(clock_value);	/* Just to get enough space for the converted result */
            char * p;
            char * q;
            for (p = clock_value, q = newlines_converted; *p != '\0'; p += 1)
            {
                if ((p[0] == '\\') && (p[1] == 'n'))
                {
                    *q++ = '\n';
                    p += 1;
                }
                else
                    *q++ = *p;
            }
            *q = '\0';
        }

        gchar * utf8 = g_locale_to_utf8(((newlines_converted != NULL) ? newlines_converted : clock_value), -1, NULL, NULL, NULL);
        if (utf8 != NULL)
        {
            panel_draw_label_text(dc->plugin->panel, dc->clock_label, utf8, dc->bold, TRUE);
            g_free(utf8);
        }
        g_free(newlines_converted);
    }

    /* Determine the content of the tooltip. */
    gchar * utf8 = g_locale_to_utf8(tooltip_value, -1, NULL, NULL, NULL);
    if (utf8 != NULL)
    {
        gtk_widget_set_tooltip_text(dc->plugin->pwid, utf8);
        g_free(utf8);
    }

    /* Conduct an experiment to see how often the value changes.
     * Use this to decide whether we update the value every second or every minute.
     * We need to account for the possibility that the experiment is being run when we cross a minute boundary. */
    if (dc->expiration_interval < ONE_SECOND_INTERVAL)
    {
        if (dc->prev_clock_value == NULL)
        {
            /* Initiate the experiment. */
            dc->prev_clock_value = g_strdup(clock_value);
            dc->prev_tooltip_value = g_strdup(tooltip_value);
        }
        else
        {
            if (((dc->icon_only) || (strcmp(dc->prev_clock_value, clock_value) == 0))
            && (strcmp(dc->prev_tooltip_value, tooltip_value) == 0))
            {
                dc->experiment_count += 1;
                if (dc->experiment_count > 3)
                {
                    /* No change within 3 seconds.  Assume change no more often than once per minute. */
                    dc->expiration_interval = ONE_MINUTE_INTERVAL;
                    g_free(dc->prev_clock_value);
                    g_free(dc->prev_tooltip_value);
                    dc->prev_clock_value = NULL;
                    dc->prev_tooltip_value = NULL;
                }
            }
            else if (dc->expiration_interval == AWAITING_FIRST_CHANGE)
            {
                /* We have a change at the beginning of the experiment, but we do not know when the next change might occur.
                 * Continue the experiment for 3 more seconds. */
                dc->expiration_interval = AWAITING_SECOND_CHANGE;
                dc->experiment_count = 0;
                g_free(dc->prev_clock_value);
                g_free(dc->prev_tooltip_value);
                dc->prev_clock_value = g_strdup(clock_value);
                dc->prev_tooltip_value = g_strdup(tooltip_value);
            }
            else
            {
                /* We have a second change.  End the experiment. */
                dc->expiration_interval = ((dc->experiment_count > 3) ? ONE_MINUTE_INTERVAL : ONE_SECOND_INTERVAL);
                g_free(dc->prev_clock_value);
                g_free(dc->prev_tooltip_value);
                dc->prev_clock_value = NULL;
                dc->prev_tooltip_value = NULL;
            }
        }
    }

    /* Reset the timer and return. */
    dclock_timer_set(dc);
    return FALSE;
}

/* Plugin constructor. */
static int dclock_constructor(Plugin * p, char ** fp)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    DClockPlugin * dc = g_new0(DClockPlugin, 1);
    p->priv = dc;
    dc->plugin = p;

    /* Load parameters from the configuration file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "dclock: illegal token %s\n", s.str);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "ClockFmt") == 0)
                    dc->clock_format = g_strdup(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "TooltipFmt") == 0)
                    dc->tooltip_format = g_strdup(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "Action") == 0)
                    dc->action = g_strdup(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "BoldFont") == 0)
                    dc->bold = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "IconOnly") == 0)
                    dc->icon_only = str2num(bool_pair, s.t[1], 0);
                else
                    ERR( "dclock: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "dclock: illegal in this context %s\n", s.str);
                return 0;
            }
        }
    }

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();

    /* Allocate a horizontal box as the child of the top level. */
    GtkWidget * hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(p->pwid), hbox);
    gtk_widget_show(hbox);

    /* Create a label and an image as children of the horizontal box.
     * Only one of these is visible at a time, controlled by user preference. */
    dc->clock_label = gtk_label_new(NULL);
    gtk_misc_set_alignment(GTK_MISC(dc->clock_label), 0.5, 0.5);
    gtk_misc_set_padding(GTK_MISC(dc->clock_label), 4, 0);
    gtk_container_add(GTK_CONTAINER(hbox), dc->clock_label);
    dc->clock_icon = gtk_image_new();
    gtk_container_add(GTK_CONTAINER(hbox), dc->clock_icon);

    /* Connect signals. */
    g_signal_connect(G_OBJECT (p->pwid), "button_press_event", G_CALLBACK(dclock_button_press_event), (gpointer) p);

    /* Initialize the clock display. */
    if (dc->clock_format == NULL)
        dc->clock_format = g_strdup(DEFAULT_CLOCK_FORMAT);
    if (dc->tooltip_format == NULL)
        dc->tooltip_format = g_strdup(DEFAULT_TIP_FORMAT);
    dclock_apply_configuration(p);

    /* Show the widget and return. */
    gtk_widget_show(p->pwid);
    return 1;
}

/* Plugin destructor. */
static void dclock_destructor(Plugin * p)
{
    DClockPlugin * dc = (DClockPlugin *) p->priv;

    /* Remove the timer. */
    if (dc->timer != 0)
        g_source_remove(dc->timer);

    /* Ensure that the calendar is dismissed. */
    if (dc->calendar_window != NULL)
        gtk_widget_destroy(dc->calendar_window);

    /* Deallocate all memory. */
    g_free(dc->clock_format);
    g_free(dc->tooltip_format);
    g_free(dc->action);
    g_free(dc->prev_clock_value);
    g_free(dc->prev_tooltip_value);
    g_free(dc);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static void dclock_apply_configuration(Plugin * p)
{
    DClockPlugin * dc = (DClockPlugin *) p->priv;

    /* Set up the icon or the label as the displayable widget. */
    if (dc->icon_only)
    {
        panel_image_set_from_file(p->panel, dc->clock_icon, PACKAGE_DATA_DIR "/lxpanel/images/clock.png");
        gtk_widget_show(dc->clock_icon);
        gtk_widget_hide(dc->clock_label);
    }
    else
    {
        gtk_widget_show(dc->clock_label);
        gtk_widget_hide(dc->clock_icon);
    }

    /* Rerun the experiment to determine update interval and update the display. */
    g_free(dc->prev_clock_value);
    g_free(dc->prev_tooltip_value);
    dc->expiration_interval = AWAITING_FIRST_CHANGE;
    dc->experiment_count = 0;
    dc->prev_clock_value = NULL;
    dc->prev_tooltip_value = NULL;
    dclock_update_display(dc);

    /* Hide the calendar. */
    if (dc->calendar_window != NULL)
    {
        gtk_widget_destroy(dc->calendar_window);
        dc->calendar_window = NULL;
    }
}

/* Callback when the configuration dialog is to be shown. */
static void dclock_configure(Plugin * p, GtkWindow * parent)
{
    DClockPlugin * dc = (DClockPlugin *) p->priv;
    GtkWidget * dlg = create_generic_config_dlg(
        _(p->class->name),
        GTK_WIDGET(parent),
        (GSourceFunc) dclock_apply_configuration, (gpointer) p,
        _("Clock Format"), &dc->clock_format, CONF_TYPE_STR,
        _("Tooltip Format"), &dc->tooltip_format, CONF_TYPE_STR,
        _("Format codes: man 3 strftime; \\n for line break"), NULL, CONF_TYPE_TRIM,
        _("Action when clicked (default: display calendar)"), &dc->action, CONF_TYPE_STR,
        _("Bold font"), &dc->bold, CONF_TYPE_BOOL,
        _("Tooltip only"), &dc->icon_only, CONF_TYPE_BOOL,
        NULL);
    gtk_window_present(GTK_WINDOW(dlg));
}

/* Callback when the configuration is to be saved. */
static void dclock_save_configuration(Plugin * p, FILE * fp)
{
    DClockPlugin * dc = (DClockPlugin *) p->priv;
    lxpanel_put_str(fp, "ClockFmt", dc->clock_format);
    lxpanel_put_str(fp, "TooltipFmt", dc->tooltip_format);
    lxpanel_put_str(fp, "Action", dc->action);
    lxpanel_put_int(fp, "BoldFont", dc->bold);
    lxpanel_put_int(fp, "IconOnly", dc->icon_only);
}

/* Callback when panel configuration changes. */
static void dclock_panel_configuration_changed(Plugin * p)
{
    dclock_apply_configuration(p);
}

/* Plugin descriptor. */
PluginClass dclock_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "dclock",
    name : N_("Digital Clock"),
    version: "1.0",
    description : N_("Display digital clock and tooltip"),

    constructor : dclock_constructor,
    destructor  : dclock_destructor,
    config : dclock_configure,
    save : dclock_save_configuration,
    panel_configuration_changed : dclock_panel_configuration_changed
};
