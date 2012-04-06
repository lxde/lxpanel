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

#define MAX_NUM_SENSORS 10
#define MAX_AUTOMATIC_CRITICAL_TEMP 150 /* in degrees Celsius */


typedef struct thermal {
    Plugin * plugin;
    GtkWidget *main;
    GtkWidget *namew;
    GString *tip;
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
    int numsensors;
    char *sensor_array[MAX_NUM_SENSORS];
    gint (*get_temperature[MAX_NUM_SENSORS])(char const* sensor_path);
    gint (*get_critical[MAX_NUM_SENSORS])(char const* sensor_path);
    gint temperature[MAX_NUM_SENSORS];
} thermal;


static gint
proc_get_critical(char const* sensor_path){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(sensor_path == NULL) return -1;

    sprintf(sstmp,"%s%s",sensor_path,PROC_THERMAL_TRIP);

    if (!(state = fopen( sstmp, "r"))) {
        ERR("thermal: cannot open %s\n", sstmp);
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
        fclose(state);
        return atoi(pstr);
    }

    fclose(state);
    return -1;
}

static gint
proc_get_temperature(char const* sensor_path){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(sensor_path == NULL) return -1;

    sprintf(sstmp,"%s%s",sensor_path,PROC_THERMAL_TEMPF);

    if (!(state = fopen( sstmp, "r"))) {
        ERR("thermal: cannot open %s\n", sstmp);
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
sysfs_get_critical(char const* sensor_path){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(sensor_path == NULL) return -1;

    sprintf(sstmp,"%s%s",sensor_path,SYSFS_THERMAL_TRIP);

    if (!(state = fopen( sstmp, "r"))) {
        ERR("thermal: cannot open %s\n", sstmp);
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
sysfs_get_temperature(char const* sensor_path){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(sensor_path == NULL) return -1;

    sprintf(sstmp,"%s%s",sensor_path,SYSFS_THERMAL_TEMPF);

    if (!(state = fopen( sstmp, "r"))) {
        ERR("thermal: cannot open %s\n", sstmp);
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

static gboolean
is_sysfs(char const* path)
{
    return path && strncmp(path, "/sys/", 5) == 0;
}

/* get_temp_function():
 *      - This function is called 'get_temp_function'.
 *      - It takes 'path' as argument.
 *      - It returns a pointer to a function.
 *      - The returned function takes a pointer to a char const as arg.
 *      - The returned function returns a gint. */
static gint (*get_temp_function(char const* path))(char const* sensor_path)
{
    if (is_sysfs(path))
        return sysfs_get_temperature;
    else
        return proc_get_temperature;
}

static gint (*get_crit_function(char const* path))(char const* sensor_path)
{
    if (is_sysfs(path))
        return sysfs_get_critical;
    else
        return proc_get_critical;
}

static gint get_temperature(thermal *th)
{
    gint max = -273;
    gint cur, i;

    for(i = 0; i < th->numsensors; i++){
        cur = th->get_temperature[i](th->sensor_array[i]);
        if (cur > max)
            max = cur;
        th->temperature[i] = cur;
    }

    return max;
}

static gint get_critical(thermal *th)
{
    gint min = MAX_AUTOMATIC_CRITICAL_TEMP;
    gint cur, i;

    for(i = 0; i < th->numsensors; i++){
        cur = th->get_critical[i](th->sensor_array[i]);
        if (cur < min)
            min = cur;
    }

    return min;
}

static gint
update_display(thermal *th)
{
    char buffer [60];
    int i;
    int temp = get_temperature(th);
    GdkColor color;
    gchar *separator;

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

    g_string_truncate(th->tip, 0);
    separator = "";
    for (i = 0; i < th->numsensors; i++){
        g_string_append_printf(th->tip, "%s%s:\t%2d°C", separator, th->sensor_array[i], th->temperature[i]);
        separator = "\n";
    }
    gtk_widget_set_tooltip_text(th->namew, th->tip->str);

    RET(TRUE);
}


static int
add_sensor(thermal* th, char const* sensor_path)
{
    if (th->numsensors + 1 > MAX_NUM_SENSORS){
        ERR("thermal: Too many sensors (max %d), ignoring '%s'\n",
                MAX_NUM_SENSORS, sensor_path);
        return -1;
    }

    th->sensor_array[th->numsensors] = g_strdup(sensor_path);
    th->get_critical[th->numsensors] = get_crit_function(sensor_path);
    th->get_temperature[th->numsensors] = get_temp_function(sensor_path);
    th->numsensors++;

    LOG(LOG_ALL, "thermal: Added sensor %s\n", sensor_path);

    return 0;
}

/* find_sensors():
 *      - Get the sensor directory, and store it in '*sensor'.
 *      - It is searched for in 'directory'.
 *      - Only the subdirectories starting with 'subdir_prefix' are accepted as sensors.
 *      - 'subdir_prefix' may be NULL, in which case any subdir is considered a sensor. */
static void
find_sensors(thermal* th, char const* directory, char const* subdir_prefix)
{
    GDir *sensorsDirectory;
    const char *sensor_name;
    char sensor_path[100];

    if (! (sensorsDirectory = g_dir_open(directory, 0, NULL)))
        return;

    /* Scan the thermal_zone directory for available sensors */
    while ((sensor_name = g_dir_read_name(sensorsDirectory))) {
        if (sensor_name[0] == '.')
            continue;
        if (subdir_prefix) {
            if (strncmp(sensor_name, subdir_prefix, strlen(subdir_prefix)) != 0)
                continue;
        }
        sprintf(sensor_path,"%s%s/", directory, sensor_name);
        add_sensor(th, sensor_path);
    }
    g_dir_close(sensorsDirectory);
}

static void
remove_all_sensors(thermal *th)
{
    int i;

    LOG(LOG_ALL, "thermal: Removing all sensors (%d)\n", th->numsensors);

    for (i = 0; i < th->numsensors; i++)
        g_free(th->sensor_array[i]);

    th->numsensors = 0;
}

static void
check_sensors( thermal *th )
{
    find_sensors(th, PROC_THERMAL_DIRECTORY, NULL);
    find_sensors(th, SYSFS_THERMAL_DIRECTORY, SYSFS_THERMAL_SUBDIR_PREFIX);
    LOG(LOG_INFO, "thermal: Found %d sensors\n", th->numsensors);
}


static void applyConfig(Plugin* p)
{
    thermal *th = p->priv;
    ENTER;

    if (th->str_cl_normal) gdk_color_parse(th->str_cl_normal, &th->cl_normal);
    if (th->str_cl_warning1) gdk_color_parse(th->str_cl_warning1, &th->cl_warning1);
    if (th->str_cl_warning2) gdk_color_parse(th->str_cl_warning2, &th->cl_warning2);

    remove_all_sensors(th);
    if(th->sensor == NULL) th->auto_sensor = TRUE;
    if(th->auto_sensor) check_sensors(th);
    else add_sensor(th, th->sensor);

    th->critical = get_critical(th);

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
    th->tip = g_string_new(NULL);

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
    th->timer = g_timeout_add_seconds(3, (GSourceFunc) update_display, (gpointer)th);

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
  remove_all_sensors(th);
  g_string_free(th->tip, TRUE);
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


/* vim: set sw=4 sts=4 et : */
