//  by Daniel Kesler <kesler.daniel@gmail.com>

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <glib/gi18n.h>

#include <string.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#define THERMAL_DIRECTORY "/proc/acpi/thermal_zone/" /* must be slash-terminated */
#define THERMAL_TEMPF  "temperature"
#define THERMAL_TRIP  "trip_points"
#define TRIP_CRITICAL "critical (S5):"

typedef struct {
    Plugin * plugin;
    GtkWidget *main;
    GtkWidget *namew;
    GtkTooltips *tip;
    int critical;
    int warning1;
    int warning2;
    int custom_levels, auto_sensor;
    char *sensor,
         *str_cl_normal,
         *str_cl_warning1,
         *str_cl_warning2;
    unsigned int timer;
    GdkColor cl_normal,
             cl_warning1,
             cl_warning2;
} thermal;

static gint
get_critical(thermal *th){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(th->sensor == NULL) return -1;

    sprintf(sstmp,"%s%s",th->sensor,THERMAL_TRIP);

    if (!(state = fopen( sstmp, "r"))) {
        //printf("cannot open %s\n",sstmp);
        return -1;
    }

    while( fgets(buf, 256, state) &&
            ! ( pstr = strstr(buf, TRIP_CRITICAL) ) );
    if( pstr )
    {
        pstr += strlen(TRIP_CRITICAL);
        while( *pstr && *pstr == ' ' )
            ++pstr;

        pstr[strlen(pstr)-3] = '\0';
        printf("Critical: [%s]\n",pstr);
        fclose(state);
        return atoi(pstr);
    }

    fclose(state);
    return -1;
}

static gint
get_temperature(thermal *th){
    FILE *state;
    char buf[ 256 ], sstmp [ 100 ];
    char* pstr;

    if(th->sensor == NULL) return -1;

    sprintf(sstmp,"%s%s",th->sensor,THERMAL_TEMPF);

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
update_display(thermal *th)
{
    char buffer [60];
    int n;
    int temp = get_temperature(th);
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
        n = sprintf(buffer, "<span color=\"#%06x\"><b>%02d</b></span>", gcolor2rgb24(&color), temp);
        gtk_label_set_markup (GTK_LABEL(th->namew), buffer) ;
    }

    RET(TRUE);
}

static void
check_sensors( thermal* th )
{
    GDir *sensorsDirectory;
    const char *sensor_name;
    char sensor_path[100];

    if (! (sensorsDirectory = g_dir_open(THERMAL_DIRECTORY, 0, NULL)))
    {
        th->sensor = NULL;
        return;
    }

    /* Scan the thermal_zone directory for available sensors */
    while ((sensor_name = g_dir_read_name(sensorsDirectory))) {
        if (sensor_name[0] != '.') {
            sprintf(sensor_path,"%s%s/",THERMAL_DIRECTORY, sensor_name);
            if(th->sensor) { 
                g_free(th->sensor);
                th->sensor = NULL; 
            }
            th->sensor = strdup(sensor_path);
            //printf("sensor: %s\n", b->sensor);
            break;
        }
    }
    g_dir_close(sensorsDirectory);
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

    th->main = gtk_event_box_new();
    th->tip  = gtk_tooltips_new();

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
                    th->custom_levels= atoi(s.t[1]);
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

    gdk_color_parse(th->str_cl_normal,   &(th->cl_normal));
    gdk_color_parse(th->str_cl_warning1, &(th->cl_warning1));
    gdk_color_parse(th->str_cl_warning2, &(th->cl_warning2));


    if(th->sensor == NULL) th->auto_sensor = TRUE;
    if(th->auto_sensor) check_sensors(th);

    th->critical = get_critical(th);

    if(!th->custom_levels){
        th->warning1 = th->critical - 10;
        th->warning2 = th->critical - 5;
    }

    update_display(th);
    th->timer = g_timeout_add(1000, (GSourceFunc) update_display, (gpointer)th);

    RET(TRUE);

error:
    RET(FALSE);
}

static void applyConfig(Plugin* p)
{

    thermal *th = (thermal *)p->priv;

    ENTER;

    if (th->str_cl_normal) gdk_color_parse(th->str_cl_normal, &th->cl_normal);
    if (th->str_cl_warning1) gdk_color_parse(th->str_cl_warning1, &th->cl_warning1);
    if (th->str_cl_warning2) gdk_color_parse(th->str_cl_warning2, &th->cl_warning2);

    if(th->auto_sensor) check_sensors(th);

    if(th->custom_levels){
        th->warning1 = th->critical - 10;
        th->warning2 = th->critical - 5;
    }

    RET();
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
            _("Automatic temperature levels"), &th->custom_levels, CONF_TYPE_BOOL,
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
    lxpanel_put_int( fp, "CustomLevels", th->custom_levels );
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
    description : N_("Display system temperature, by kesler.daniel@gmail.com"),

    constructor : thermal_constructor,
    destructor  : thermal_destructor,
    config : config,
    save : save_config
};
