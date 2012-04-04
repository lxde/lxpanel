/**
 * Thermal plugin to lxpanel
 *
 * Copyright (C) 2007 by Daniel Kesler <kesler.daniel@gmail.com>
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

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#include <string.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#define PROC_THERMAL_DIRECTORY "/proc/acpi/thermal_zone/" /* must be slash-terminated */
#define PROC_THERMAL_TEMPF  "temperature"
#define PROC_THERMAL_TRIP  "trip_points"
#define PROC_TRIP_CRITICAL "critical (S5):"

#define SYSFS_THERMAL_DIRECTORY "/sys/class/thermal/" /* must be slash-terminated */
#define SYSFS_THERMAL_SUBDIR_PREFIX "thermal_zone"
#define SYSFS_THERMAL_TEMPF  "temp"
#define SYSFS_THERMAL_TRIP  "trip_point_0_temp"


typedef struct thermal {
    Plugin * plugin;
    GtkWidget *main;
    GtkWidget *namew;
    GtkTooltips *tip;
    int critical;
    int warning1;
    int warning2;
    int not_custom_levels, auto_sensor;
    char *sensor,
         *str_cl_normal,
         *str_cl_warning1,
         *str_cl_warning2;
    unsigned int timer;
    GdkColor cl_normal,
             cl_warning1,
             cl_warning2;
    gint (*get_temperature)(struct thermal *th);
    gint (*get_critical)(struct thermal *th);
} thermal;


static gint
proc_get_critical(thermal *th){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(th->sensor == NULL) return -1;

    sprintf(sstmp,"%s%s",th->sensor,PROC_THERMAL_TRIP);

    if (!(state = fopen( sstmp, "r"))) {
        //printf("cannot open %s\n",sstmp);
        return -1;
    }

    while( fgets(buf, 256, state) &&
            ! ( pstr = strstr(buf, PROC_TRIP_CRITICAL) ) );
    if( pstr )
    {
        pstr += strlen(PROC_TRIP_CRITICAL);
        while( *pstr && *pstr == ' ' )
            ++pstr;

        pstr[strlen(pstr)-3] = '\0';
        //printf("Critical: [%s]\n",pstr);
        fclose(state);
        return atoi(pstr);
    }

    fclose(state);
    return -1;
}

static gint
proc_get_temperature(thermal *th){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(th->sensor == NULL) return -1;

    sprintf(sstmp,"%s%s",th->sensor,PROC_THERMAL_TEMPF);

    if (!(state = fopen( sstmp, "r"))) {
        //printf("cannot open %s\n",sstmp);
        return -1;
    }

    while( fgets(buf, 256, state) &&
            ! ( pstr = strstr(buf, "temperature:") ) );
    if( pstr )
    {
        pstr += 12;
        while( *pstr && *pstr == ' ' )
            ++pstr;

        pstr[strlen(pstr)-3] = '\0';
        fclose(state);
        return atoi(pstr);
    }

    fclose(state);
    return -1;
}

static gint
sysfs_get_critical(thermal *th){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(th->sensor == NULL) return -1;

    sprintf(sstmp,"%s%s",th->sensor,SYSFS_THERMAL_TRIP);

    if (!(state = fopen( sstmp, "r"))) {
        //printf("cannot open %s\n",sstmp);
        return -1;
    }

    while( fgets(buf, 256, state) &&
            ! ( pstr = buf ) );
    if( pstr )
    {
        fclose(state);
        return atoi(pstr)/1000;
    }

    fclose(state);
    return -1;
}

static gint
sysfs_get_temperature(thermal *th){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(th->sensor == NULL) return -1;

    sprintf(sstmp,"%s%s",th->sensor,SYSFS_THERMAL_TEMPF);

    if (!(state = fopen( sstmp, "r"))) {
        //printf("cannot open %s\n",sstmp);
        return -1;
    }

    while (fgets(buf, 256, state) &&
	   ! ( pstr = buf ) );
    if( pstr )
    {
        fclose(state);
        return atoi(pstr)/1000;
    }

    fclose(state);
    return -1;
}


static void
set_get_functions(thermal *th)
{
    if (th->sensor && strncmp(th->sensor, "/sys/", 5) == 0){
        th->get_temperature = sysfs_get_temperature;
        th->get_critical = sysfs_get_critical;
    } else {
        th->get_temperature = proc_get_temperature;
        th->get_critical = proc_get_critical;
    }
}

static gint
update_display(thermal *th)
{
    char buffer [60];
    int temp = th->get_temperature(th);
    GdkColor color;

    if(temp >= th->warning2)
        color = th->cl_warning2;
    else if(temp >= th->warning1)
        color = th->cl_warning1;
    else
        color = th->cl_normal;

    ENTER;
    if(temp == -1)
        panel_draw_label_text(th->plugin->panel, th->namew, "NA", TRUE, TRUE);
    else
    {
        sprintf(buffer, "<span color=\"#%06x\"><b>%02d</b></span>", gcolor2rgb24(&color), temp);
        gtk_label_set_markup (GTK_LABEL(th->namew), buffer) ;
    }

    RET(TRUE);
}


/* get_sensor():
 *      - Get the sensor directory, and store it in '*sensor'.
 *      - It is searched for in 'directory'.
 *      - Only the subdirectories starting with 'subdir_prefix' are accepted as sensors.
 *      - 'subdir_prefix' may be NULL, in which case any subdir is considered a sensor. */
static void
get_sensor(char** sensor, char const* directory, char const* subdir_prefix)
{
    GDir *sensorsDirectory;
    const char *sensor_name;
    char sensor_path[100];

    if (! (sensorsDirectory = g_dir_open(directory, 0, NULL)))
    {
        *sensor = NULL;
        return;
    }

    /* Scan the thermal_zone directory for available sensors */
    while ((sensor_name = g_dir_read_name(sensorsDirectory))) {
        if (sensor_name[0] != '.') {
            if (subdir_prefix) {
                if (strncmp(sensor_name, subdir_prefix, strlen(subdir_prefix)) != 0)
                    continue;
            }
            sprintf(sensor_path,"%s%s/", directory, sensor_name);
            if(*sensor) {
                g_free(*sensor);
                *sensor = NULL;
            }
            *sensor = strdup(sensor_path);
            break;
        }
    }
    g_dir_close(sensorsDirectory);
}

static void
check_sensors( thermal *th )
{
    if(th->sensor) {
        g_free(th->sensor);
        th->sensor = NULL;
    }

    get_sensor(&th->sensor, PROC_THERMAL_DIRECTORY, NULL);

    if (!th->sensor)
        get_sensor(&th->sensor, SYSFS_THERMAL_DIRECTORY, SYSFS_THERMAL_SUBDIR_PREFIX);

    //printf("thermal sensor: %s\n", th->sensor);
}


static void applyConfig(Plugin* p)
{
    thermal *th = p->priv;
    ENTER;

    if (th->str_cl_normal) gdk_color_parse(th->str_cl_normal, &th->cl_normal);
    if (th->str_cl_warning1) gdk_color_parse(th->str_cl_warning1, &th->cl_warning1);
    if (th->str_cl_warning2) gdk_color_parse(th->str_cl_warning2, &th->cl_warning2);

    if(th->sensor == NULL) th->auto_sensor = TRUE;
    if(th->auto_sensor) check_sensors(th);

    set_get_functions(th);

    th->critical = th->get_critical(th);

    if(th->not_custom_levels){
        th->warning1 = th->critical - 10;
        th->warning2 = th->critical - 5;
    }

    RET();
}

static int
thermal_constructor(Plugin *p, char** fp)
{
    thermal *th;

    ENTER;
    th = g_new0(thermal, 1);
    th->plugin = p;
    p->priv = th;

    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( p->pwid, GTK_NO_WINDOW );
    gtk_container_set_border_width( GTK_CONTAINER(p->pwid), 2 );

    th->namew = gtk_label_new("ww");
    gtk_container_add(GTK_CONTAINER(p->pwid), th->namew);

    th->main = p->pwid;
    th->tip  = gtk_tooltips_new();

    /* By default, use automatic, that is, "not custom" temperature levels. If
     * we were using custom levels, they would be 0°C at startup, so we would
     * display in warning colors by default. */
    th->not_custom_levels = TRUE;

    g_signal_connect (G_OBJECT (p->pwid), "button_press_event",
          G_CALLBACK (plugin_button_press_event), (gpointer) p);

    line s;
    s.len = 256;

    if (fp) {
        /* Apply options */
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "thermal: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "NormalColor")){
                    th->str_cl_normal = g_strdup(s.t[1]);
                }else if (!g_ascii_strcasecmp(s.t[0], "Warning1Color")){
                    th->str_cl_warning1 = g_strdup(s.t[1]);
                }else if (!g_ascii_strcasecmp(s.t[0], "Warning2Color")){
                    th->str_cl_warning2 = g_strdup(s.t[1]);
                }else if (!g_ascii_strcasecmp(s.t[0], "AutomaticSensor")){
                    th->auto_sensor= atoi(s.t[1]);
                }else if (!g_ascii_strcasecmp(s.t[0], "CustomLevels")){
                    th->not_custom_levels= atoi(s.t[1]);
                }else if (!g_ascii_strcasecmp(s.t[0], "Sensor")){
                    th->sensor= g_strdup(s.t[1]);
                }else if (!g_ascii_strcasecmp(s.t[0], "Warning1Temp")){
                    th->warning1 = atoi(s.t[1]);
                }else if (!g_ascii_strcasecmp(s.t[0], "Warning2Temp")){
                    th->warning2 = atoi(s.t[1]);
                }else {
                    ERR( "thermal: unknown var %s\n", s.t[0]);
                }
            }
            else {
                ERR( "thermal: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }

    if(!th->str_cl_normal)
        th->str_cl_normal = g_strdup("#00ff00");
    if(!th->str_cl_warning1)
        th->str_cl_warning1 = g_strdup("#fff000");
    if(!th->str_cl_warning2)
        th->str_cl_warning2 = g_strdup("#ff0000");

    applyConfig(p);

    gtk_widget_show(th->namew);

    update_display(th);
    th->timer = g_timeout_add(1000, (GSourceFunc) update_display, (gpointer)th);

    RET(TRUE);

error:
    RET(FALSE);
}

static void config(Plugin *p, GtkWindow* parent) {
    ENTER;

    GtkWidget *dialog;
    thermal *th = (thermal *) p->priv;
    dialog = create_generic_config_dlg(_(p->class->name),
            GTK_WIDGET(parent),
            (GSourceFunc) applyConfig, (gpointer) p,
            _("Normal"), &th->str_cl_normal, CONF_TYPE_STR,
            _("Warning1"), &th->str_cl_warning1, CONF_TYPE_STR,
            _("Warning2"), &th->str_cl_warning2, CONF_TYPE_STR,
            _("Automatic sensor location"), &th->auto_sensor, CONF_TYPE_BOOL,
            _("Sensor"), &th->sensor, CONF_TYPE_STR,
            _("Automatic temperature levels"), &th->not_custom_levels, CONF_TYPE_BOOL,
            _("Warning1 Temperature"), &th->warning1, CONF_TYPE_INT,
            _("Warning2 Temperature"), &th->warning2, CONF_TYPE_INT,
            NULL);
    gtk_window_present(GTK_WINDOW(dialog));

    RET();
}

static void
thermal_destructor(Plugin *p)
{
  thermal *th = (thermal *)p->priv;

  ENTER;
  th = (thermal *) p->priv;
  g_free(th->sensor);
  g_free(th->str_cl_normal);
  g_free(th->str_cl_warning1);
  g_free(th->str_cl_warning2);
  g_source_remove(th->timer);
  g_free(th);
  RET();
}

static void save_config( Plugin* p, FILE* fp )
{
    thermal *th = (thermal *)p->priv;

    lxpanel_put_str( fp, "NormalColor", th->str_cl_normal );
    lxpanel_put_str( fp, "Warning1Color", th->str_cl_warning1 );
    lxpanel_put_str( fp, "Warning2Color", th->str_cl_warning2 );
    lxpanel_put_int( fp, "CustomLevels", th->not_custom_levels );
    lxpanel_put_int( fp, "Warning1Temp", th->warning1 );
    lxpanel_put_int( fp, "Warning2Temp", th->warning2 );
    lxpanel_put_int( fp, "AutomaticSensor", th->auto_sensor );
    lxpanel_put_str( fp, "Sensor", th->sensor );
}

PluginClass thermal_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "thermal",
    name : N_("Temperature Monitor"),
    version: "0.6",
    description : N_("Display system temperature"),

    constructor : thermal_constructor,
    destructor  : thermal_destructor,
    config : config,
    save : save_config,
    panel_configuration_changed : NULL
};
