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

//#define DEBUG
#include "dbg.h"


#define DEFAULT_TIP_FORMAT    "%A %x"
#define DEFAULT_CLOCK_FORMAT  "%R"

typedef struct {
    GtkWidget *eb;
    GtkWidget *main;
    GtkWidget *clockw;
    GtkTooltips *tip;
    char *tfmt;
    char *cfmt;
    char *action;
    short lastDay;
    int timer;
} dclock;

//static dclock me;

static void
update_label_orient( plugin* p );


static  gboolean
clicked( GtkWidget *widget, gpointer dummy, dclock *dc)
{
    ENTER2;
    DBG2("%s\n", dc->action);
    system (dc->action);
    RET2(TRUE);
}



static gint
clock_update(gpointer data )
{
    char output[64], str[64];
    time_t now;
    struct tm * detail;
    dclock *dc;
    gchar *utf8;

    ENTER;
    g_assert(data != NULL);
    dc = (dclock *)data;

    time(&now);
    detail = localtime(&now);
    strftime(output, sizeof(output), dc->cfmt, detail) ;
    g_snprintf(str, 64, "<b>%s</b>", output);
    gtk_label_set_markup (GTK_LABEL(dc->clockw), str) ;

    if (detail->tm_mday != dc->lastDay) {
        dc->lastDay = detail->tm_mday ;

        strftime (output, sizeof(output), dc->tfmt, detail) ;
            if ((utf8 = g_locale_to_utf8(output, -1, NULL, NULL, NULL))) {
                gtk_tooltips_set_tip(dc->tip, dc->main, utf8, NULL) ;
                g_free(utf8);
            }
    }
    RET(TRUE);
}


static int
dclock_constructor(plugin *p, char** fp)
{
    line s;
    dclock *dc;

    ENTER;
    dc = g_new0(dclock, 1);
    g_return_val_if_fail(dc != NULL, 0);
    p->priv = dc;

    s.len = 256;
    dc->cfmt = dc->tfmt = dc->action = 0;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "dclock: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "ClockFmt"))
                    dc->cfmt = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "TooltipFmt"))
                    dc->tfmt = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "Action"))
                    dc->action = g_strdup(s.t[1]);
                else {
                    ERR( "dclock: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "dclock: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }
    if (!dc->cfmt)
        dc->cfmt = g_strdup(DEFAULT_CLOCK_FORMAT);
    if (!dc->tfmt)
        dc->tfmt = g_strdup(DEFAULT_TIP_FORMAT);
    dc->main = gtk_event_box_new();
    //gtk_widget_add_events (dc->main, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);
    //button = gtk_button_new();
    //gtk_container_add(GTK_CONTAINER(dc->main), button);
    if (dc->action)
        g_signal_connect (G_OBJECT (dc->main), "button_press_event",
              G_CALLBACK (clicked), (gpointer) dc);
    dc->clockw = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(dc->clockw), 0.5, 0.5);
    gtk_misc_set_padding(GTK_MISC(dc->clockw), 4, 0);
    update_label_orient( p );
    gtk_container_add(GTK_CONTAINER(dc->main), dc->clockw);
    gtk_widget_show_all(dc->main);
    dc->tip = gtk_tooltips_new();
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( dc->tip );
#else
    g_object_ref( dc->tip );
    gtk_object_sink( dc->tip );
#endif
    dc->timer = g_timeout_add(1000, (GSourceFunc) clock_update, (gpointer)dc);
    gtk_container_add(GTK_CONTAINER(p->pwid), dc->main);

    clock_update( dc );
    RET(1);

 error:
    g_free(dc->cfmt);
    g_free(dc->tfmt);
    g_free(dc->action);
    g_free(dc);
    RET(0);
}


static void
dclock_destructor(plugin *p)
{
  dclock *dc = (dclock *)p->priv;

  ENTER;
  dc = (dclock *) p->priv;
  if (dc->timer)
      g_source_remove(dc->timer);
  gtk_widget_destroy(dc->main);
  g_object_unref( dc->tip );
  g_free(dc->cfmt);
  g_free(dc->tfmt);
  g_free(dc->action);
  g_free(dc);
  RET();
}

static void apply_config( plugin* p )
{
    /* NOTE: This dirty hack is used to force the update of tooltip
       because tooltip will be updated when dc->lastDay != today.
    */
    dclock* dc = (dclock*)p->priv;
    --dc->lastDay;
    clock_update( dc );
}

static void dclock_config( plugin *p, GtkWindow* parent )
{
    GtkWidget* dlg;
    dclock *dc = (dclock *)p->priv;
    dlg = create_generic_config_dlg( _(p->class->name),
                                     GTK_WIDGET(parent),
                                    (GSourceFunc) apply_config, (gpointer) p,
                                     _("Clock Format"), &dc->cfmt, G_TYPE_STRING,
                                     _("Tooltip Format"), &dc->tfmt, G_TYPE_STRING,
                                     _("Action"), &dc->action, G_TYPE_STRING,
                                     NULL );
    gtk_window_present( GTK_WINDOW(dlg) );
}

static void save_config( plugin* p, FILE* fp )
{
    dclock *dc = (dclock *)p->priv;
    lxpanel_put_str( fp, "ClockFmt", dc->cfmt );
    lxpanel_put_str( fp, "TooltipFmt", dc->tfmt );
    lxpanel_put_str( fp, "Action", dc->action );
}

static void
update_label_orient( plugin* p )
{
    dclock *dc = (dclock *)p->priv;
    GtkLabel* label = GTK_LABEL(dc->clockw);
    /* FIXME: gtk+ has only limited support for this, sigh! */
    gdouble angle;
    if( p->panel->edge == EDGE_LEFT )
        angle = 90.0;
    else if( p->panel->edge == EDGE_RIGHT )
        angle = 270.0;
    else
        angle = 0.0;
    gtk_label_set_angle( GTK_LABEL(label), angle );
}

plugin_class dclock_plugin_class = {
    fname: NULL,
    count: 0,

    type : "dclock",
    name : N_("Digital Clock"),
    version: "1.0",
    description : N_("Display Digital clock and Tooltip"),

    constructor : dclock_constructor,
    destructor  : dclock_destructor,
    config : dclock_config,
    save : save_config,
    orientation : update_label_orient
};
