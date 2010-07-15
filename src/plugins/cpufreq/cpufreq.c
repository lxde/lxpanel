/**
 * CPUFreq plugin to lxpanel
 *
 * Copyright (C) 2009 by Daniel Kesler <kesler.daniel@gmail.com>
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
#include <glib.h>
#include <glib/gi18n.h>

#include <string.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#define PROC_ICON            PACKAGE_DATA_DIR "/lxpanel/images/cpufreq-icon.png"
#define SYSFS_CPU_DIRECTORY "/sys/devices/system/cpu"
#define SCALING_GOV         "scaling_governor"
#define SCALING_AGOV        "scaling_available_governors"
#define SCALING_AFREQ       "scaling_available_frequencies"
#define SCALING_CUR_FREQ    "scaling_cur_freq"
#define SCALING_SETFREQ     "scaling_setspeed"
#define SCALING_MAX         "scaling_max_freq"
#define SCALING_MIN         "scaling_min_freq"


typedef struct {
    GtkWidget *main;
    GtkWidget *namew;
    GtkTooltips *tip;
    GList *governors;
    GList *cpus;
    int has_cpufreq;
    char* cur_governor;
    int   cur_freq;
    unsigned int timer;
    gboolean remember;
} cpufreq;

typedef struct {
    void *data;
    cpufreq *cf;
} Param;

static void
get_cur_governor(cpufreq *cf){
    FILE *fp;
    char buf[ 100 ], sstmp [ 256 ];

    sprintf(sstmp,"%s/%s",cf->cpus->data, SCALING_GOV);
    if ((fp = fopen( sstmp, "r")) != NULL) {
        fgets(buf, 100, fp);
        buf[strlen(buf)-1] = '\0';
        if(cf->cur_governor)
        {
          g_free(cf->cur_governor);
          cf->cur_governor = NULL;
        }
        cf->cur_governor = strdup(buf);
        fclose(fp);
    }
}

static void
get_cur_freq(cpufreq *cf){
    FILE *fp;
    char buf[ 100 ], sstmp [ 256 ];

    sprintf(sstmp,"%s/%s",cf->cpus->data, SCALING_CUR_FREQ);
    if ((fp = fopen( sstmp, "r")) != NULL) {
        fgets(buf, 100, fp);
        buf[strlen(buf)-1] = '\0';
        cf->cur_freq = atoi(buf);
        fclose(fp);
    }
}

static void
get_governors(cpufreq *cf){
    FILE *fp;
    GList *l;
    char buf[ 100 ], sstmp [ 256 ], c, bufl = 0;

    g_list_free(cf->governors);
    cf->governors = NULL;

    get_cur_governor(cf);

    if(cf->cpus == NULL){
        cf->governors = NULL;
        return;
    }
    sprintf(sstmp,"%s/%s",cf->cpus->data, SCALING_AGOV);

    if (!(fp = fopen( sstmp, "r"))) {
        printf("cpufreq: cannot open %s\n",sstmp);
        return;
    }

    while((c = fgetc(fp)) != EOF){
        if(c == ' '){
            if(bufl > 1){
                buf[bufl] = '\0';
                cf->governors = g_list_append(cf->governors, strdup(buf));
            }
            bufl = 0;
            buf[0] = '\0';
        }else{
            buf[bufl++] = c;
        }
    }

    fclose(fp);
}

static void
cpufreq_set_freq(GtkWidget *widget, Param* p){
    FILE *fp;
    char buf[ 100 ], sstmp [ 256 ];

    if(strcmp(p->cf->cur_governor, "userspace")) return;

    sprintf(sstmp,"%s/%s",p->cf->cpus->data, SCALING_SETFREQ);
    if ((fp = fopen( sstmp, "w")) != NULL) {
        fprintf(fp,"%s",p->data);
        fclose(fp);
    }
}

static GtkWidget *
frequency_menu(cpufreq *cf){
    FILE *fp;
    Param* param;
    char buf[ 100 ], sstmp [ 256 ], c, bufl = 0;

    sprintf(sstmp,"%s/%s",cf->cpus->data, SCALING_AFREQ);

    if (!(fp = fopen( sstmp, "r"))) {
        printf("cpufreq: cannot open %s\n",sstmp);
        return;
    }

    GtkMenu* menu = GTK_MENU(gtk_menu_new());
    GtkWidget* menuitem;

    while((c = fgetc(fp)) != EOF){
        if(c == ' '){
            if(bufl > 1){
                buf[bufl] = '\0';
                menuitem = GTK_MENU_ITEM(gtk_menu_item_new_with_label(strdup(buf)));
                gtk_menu_append (GTK_MENU_SHELL (menu), menuitem);
                gtk_widget_show (menuitem);
                param = g_new0(Param, 1);
                param->data = strdup(buf);
                param->cf = cf;
                g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(cpufreq_set_freq), param);
                g_object_weak_ref(menuitem, g_free, param);
            }
            bufl = 0;
            buf[0] = '\0';
        }else{
            buf[bufl++] = c;
        }
    }

    fclose(fp);
    return menu;
}

static void
get_cpus(cpufreq *cf)
{

    const char *cpu;
    char cpu_path[100];

    GDir * cpuDirectory = g_dir_open(SYSFS_CPU_DIRECTORY, 0, NULL);
    if (cpuDirectory == NULL)
    {
        cf->cpus = NULL;
        printf("cpufreq: no cpu found\n");
        return;
    }

    while ((cpu = g_dir_read_name(cpuDirectory)))
    {
        /* Look for directories of the form "cpu<n>", where "<n>" is a decimal integer. */
        if ((strncmp(cpu, "cpu", 3) == 0) && (cpu[3] >= '0') && (cpu[3] <= '9'))
        {
            sprintf(cpu_path, "%s/%s/cpufreq", SYSFS_CPU_DIRECTORY, cpu);

            GDir * cpufreqDir = g_dir_open(SYSFS_CPU_DIRECTORY, 0, NULL);
	        if (cpufreqDir == NULL)
            {
                cf->cpus = NULL;
                cf->has_cpufreq = 0;
                break;
            }

            cf->has_cpufreq = 1;
            cf->cpus = g_list_append(cf->cpus, strdup(cpu_path));
        }
    }
    g_dir_close(cpuDirectory);
}

static void
cpufreq_set_governor(GtkWidget *widget, Param* p){
    FILE *fp;
    char buf[ 100 ], sstmp [ 256 ];

    sprintf(sstmp, "%s/%s", p->cf->cpus->data, SCALING_GOV);
    if ((fp = fopen( sstmp, "w")) != NULL) {
        fprintf(fp,"%s",p->data);
        fclose(fp);
    }
}

static GtkWidget *
cpufreq_menu(cpufreq *cf){
    GList *l;
    GSList *group;
    char buff[100];
    GtkMenuItem* menuitem;
    Param* param;

    GtkMenu* menu = GTK_MENU(gtk_menu_new());
        g_signal_connect(menu, "selection-done", gtk_widget_destroy, NULL);

    get_governors(cf);
    group = NULL;

    if((cf->governors == NULL) || (!cf->has_cpufreq) || (cf->cur_governor == NULL)){
        menuitem = GTK_MENU_ITEM(gtk_menu_item_new_with_label("CPUFreq not supported"));
        gtk_menu_append (GTK_MENU_SHELL (menu), menuitem);
        gtk_widget_show (menuitem);
        return menu;
    }

    if(strcmp(cf->cur_governor, "userspace") == 0){
        menuitem = GTK_MENU_ITEM(gtk_menu_item_new_with_label("  Frequency"));
        gtk_menu_append (GTK_MENU_SHELL (menu), menuitem);
        gtk_widget_show (menuitem);
        gtk_menu_item_set_submenu(menuitem, frequency_menu(cf));
        menuitem = GTK_MENU_ITEM(gtk_separator_menu_item_new());
        gtk_menu_append (GTK_MENU_SHELL (menu), menuitem);
        gtk_widget_show (GTK_WIDGET(menuitem));
    }

    for( l = cf->governors; l; l = l->next )
    {
      if(strcmp((char*)l->data, cf->cur_governor) == 0){
        sprintf(buff,"> %s", l->data);
        menuitem = GTK_MENU_ITEM(gtk_menu_item_new_with_label(strdup(buff)));
      }else{
        sprintf(buff,"   %s", l->data);
        menuitem = GTK_MENU_ITEM(gtk_menu_item_new_with_label(strdup(buff)));
      }

      gtk_menu_shell_append (GTK_MENU_SHELL (menu), menuitem);
      gtk_widget_show (menuitem);
      param = g_new0(Param, 1);
      param->data = l->data;
      param->cf = cf;
      g_signal_connect(G_OBJECT(menuitem), "activate", G_CALLBACK(cpufreq_set_governor), param);
      g_object_weak_ref(menuitem, g_free, param);
    }

    return menu;
}



static  gboolean
clicked( GtkWidget *widget, GdkEventButton* evt, Plugin* plugin)
{
    ENTER2;
    if( evt->button == 1 )
    {
// Setting governor can't work without root privilege
//      gtk_menu_popup( cpufreq_menu((cpufreq*)plugin->priv), NULL, NULL, NULL, NULL, 
//                      evt->button, evt->time );
      return TRUE;
    }else if ( evt->button == 3 )
    {
        GtkMenu* popup = lxpanel_get_panel_menu( plugin->panel, plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, evt->button, evt->time );
        return TRUE;
      return TRUE;
    }

    RET2(TRUE);
}

static gint
update_tooltip(cpufreq *cf)
{
    char *tooltip;

    get_cur_freq(cf);
    get_cur_governor(cf);

    ENTER;

    tooltip = g_strdup_printf("Frequency: %d MHz\nGovernor: %s", 
                              cf->cur_freq / 1000, cf->cur_governor);
    gtk_tooltips_set_tip(cf->tip, cf->main, tooltip, NULL);
    g_free(tooltip);
    RET(TRUE);
}

static int
cpufreq_constructor(Plugin *p, char** fp)
{
    cpufreq *cf;
    GtkWidget *button;

    ENTER;
    cf = g_new0(cpufreq, 1);
    cf->governors = NULL;
    cf->cpus = NULL;
    g_return_val_if_fail(cf != NULL, 0);
    p->priv = cf;

    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( p->pwid, GTK_NO_WINDOW );
    gtk_container_set_border_width( GTK_CONTAINER(p->pwid), 2 );

    cf->namew = gtk_image_new_from_file(PROC_ICON);
    gtk_container_add(GTK_CONTAINER(p->pwid), cf->namew);

    cf->main = p->pwid;
    cf->tip = gtk_tooltips_new();

#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( cf->tip );
#else
    g_object_ref( cf->tip );
    gtk_object_sink( cf->tip );
#endif

    g_signal_connect (G_OBJECT (p->pwid), "button_press_event", G_CALLBACK (clicked), (gpointer) p);

    cf->has_cpufreq = 0;

    get_cpus(cf);

/*    line s;
    s.len = 256;

    if (fp) {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "cpufreq: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "DefaultGovernor")){
                    //cf->str_cl_normal = g_strdup(s.t[1]);
                }else {
                    ERR( "cpufreq: unknown var %s\n", s.t[0]);
                    continue;
                }
            }
            else {
                ERR( "cpufreq: illegal in cfis context %s\n", s.str);
                goto error;
            }
        }

    }*/
    update_tooltip(cf);
    cf->timer = g_timeout_add(2000, (GSourceFunc)update_tooltip, (gpointer)cf);

    gtk_widget_show(cf->namew);

    RET(TRUE);

/*error:
    RET(FALSE);*/
}

static void applyConfig(Plugin* p) { }

static void config(Plugin *p, GtkWindow* parent) {
    ENTER;

    GtkWidget *dialog;
    cpufreq *cf = (cpufreq *) p->priv;
    dialog = create_generic_config_dlg(_(p->class->name),
            GTK_WIDGET(parent),
            (GSourceFunc) applyConfig, (gpointer) p,
            _("Remember governor and frequency"), &cf->remember, CONF_TYPE_BOOL,
            NULL);
    gtk_window_present(GTK_WINDOW(dialog));

    RET();
}

static void
cpufreq_destructor(Plugin *p)
{
    cpufreq *cf = (cpufreq *)p->priv;
    g_list_free ( cf->cpus );
    g_list_free ( cf->governors );
    g_source_remove(cf->timer);
    g_free(cf);
}

static void save_config( Plugin* p, FILE* fp )
{
    cpufreq *cf = (cpufreq *)p->priv;

    lxpanel_put_bool( fp, "Remember", cf->remember);
    lxpanel_put_str( fp, "Governor", cf->cur_governor );
    lxpanel_put_int( fp, "Frequency", cf->cur_freq );
}

PluginClass cpufreq_plugin_class = {
    PLUGINCLASS_VERSIONING,

    type : "cpufreq",
    name : N_("CPUFreq frontend"),
    version: "0.1",
    description : N_("Display CPU frequency and allow to change governors and frequency"),

    constructor : cpufreq_constructor,
    destructor  : cpufreq_destructor,
    config : config,
    save : NULL,
    panel_configuration_changed : NULL
};
