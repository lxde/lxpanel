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
#include <pthread.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "glib-mem.h"

#include "dbg.h"

#define DEFAULT_TIP_FORMAT    "%A %x"
#define DEFAULT_CLOCK_FORMAT  "%R"

typedef struct {
    Panel* panel;
    GtkWidget *eb;
    GtkWidget *main;
    GtkWidget *clockw;
    GtkWidget *calwin;
    GtkTooltips *tip;
    char *tfmt;
    char *cfmt;
    char *action;
    short lastDay;
    int bold;
    int timer;
    gboolean cal_show;
} dclock;

static void
update_label_orient( Plugin* p );

static GtkWidget *create_calendar()
{
    GtkWidget *win;
    GtkWidget *calendar;

    /* create a new window */
    win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_default_size(GTK_WINDOW(win), 180, 180);
    gtk_window_set_decorated(GTK_WINDOW(win), FALSE);
    gtk_window_set_resizable (GTK_WINDOW(win), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(win), 5);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(win), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(win), TRUE);
//    gtk_window_set_type_hint(GTK_WINDOW(win), GDK_WINDOW_TYPE_HINT_DOCK);
    gtk_window_set_position(GTK_WINDOW(win), GTK_WIN_POS_MOUSE);
    gtk_window_stick (GTK_WINDOW(win));

    GtkVBox* box = gtk_vbox_new(FALSE, 0);

    /* calendar */
    calendar = gtk_calendar_new();
    gtk_calendar_display_options(GTK_CALENDAR(calendar),
                                 GTK_CALENDAR_SHOW_WEEK_NUMBERS |
                                 GTK_CALENDAR_SHOW_DAY_NAMES |
                                 GTK_CALENDAR_SHOW_HEADING);
//    gtk_container_add(GTK_CONTAINER(win), calendar);
    gtk_box_pack_start_defaults( box, calendar );
    gtk_container_add(GTK_CONTAINER(win), box);

    gtk_widget_show_all(win);

    RET(win);
}

static int
actionProcess( void *arg )
{
    ENTER;
    RET(system((char *) arg));
}

static  gboolean
clicked( GtkWidget *widget, GdkEventButton* evt, Plugin* plugin)
{
    dclock *dc = (dclock*)plugin->priv;
    ENTER2;
    if( evt->button == 3 )  /* right button */
    {
        GtkMenu* popup = lxpanel_get_panel_menu( plugin->panel, plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, evt->button, evt->time );
        return TRUE;
    }

    if( dc->action ) {
        pthread_t actionThread;
        pthread_create(&actionThread, NULL, actionProcess, dc->action);
    } else {
        if (!dc->cal_show) {
            dc->cal_show = TRUE;
            dc->calwin = create_calendar();
        } else {
            dc->cal_show = FALSE;
            gtk_widget_destroy(dc->calwin);
            dc->calwin = NULL;
        }
    }
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
    strftime(output, sizeof(output),
             (dc->cfmt ? dc->cfmt : DEFAULT_CLOCK_FORMAT), detail);

    if (dc->bold&& dc->panel->usefontcolor)
        g_snprintf(str, 64, "<span color=\"#%06x\"><b>%s</b></span>", gcolor2rgb24( &dc->panel->gfontcolor ), output);
    else if (dc->bold)
        g_snprintf(str, 64, "<b>%s</b>", output);
    else if ( dc->panel->usefontcolor)
        g_snprintf(str, 64, "<span color=\"#%06x\">%s</span>", gcolor2rgb24(&dc->panel->gfontcolor), output);
    else
        g_snprintf(str, 64, "%s", output);

    gtk_label_set_markup (GTK_LABEL(dc->clockw), str);

    if (detail->tm_mday != dc->lastDay) {
        dc->lastDay = detail->tm_mday ;
        strftime (output, sizeof(output),
                  (dc->tfmt ? dc->tfmt : DEFAULT_TIP_FORMAT), detail);
            if ((utf8 = g_locale_to_utf8(output, -1, NULL, NULL, NULL))) {
                gtk_tooltips_set_tip(dc->tip, dc->main, utf8, NULL);
                g_free(utf8);
            }
    }

    RET(TRUE);
}


static int
dclock_constructor(Plugin *p, char** fp)
{
    line s;
    dclock *dc;

    ENTER;
    dc = g_slice_new0(dclock);
    g_return_val_if_fail(dc != NULL, 0);
    p->priv = dc;

    dc->panel = p->panel;

    s.len = 256;
    dc->cfmt = dc->tfmt = dc->action = dc->bold = 0;
    dc->bold = TRUE;
    dc->cal_show = FALSE;
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
                else if (!g_ascii_strcasecmp(s.t[0], "BoldFont"))
                    dc->bold = str2num(bool_pair, s.t[1], 0);
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
    dc->main = gtk_event_box_new();

    g_signal_connect (G_OBJECT (dc->main), "button_press_event",
          G_CALLBACK (clicked), (gpointer) p);
    dc->clockw = gtk_label_new("");
    gtk_misc_set_alignment(GTK_MISC(dc->clockw), 0.5, 0.5);
    gtk_misc_set_padding(GTK_MISC(dc->clockw), 4, 0);
    update_label_orient( p );
    gtk_container_add(GTK_CONTAINER(dc->main), dc->clockw);
    gtk_widget_show_all(dc->main);

    dc->tip = p->panel->tooltips;
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( dc->tip );
#else
    g_object_ref( dc->tip );
    gtk_object_sink( dc->tip );
#endif

    dc->timer = g_timeout_add(1000, (GSourceFunc) clock_update, (gpointer)dc);

    clock_update( dc );

    /* store the created Plugin widget in Plugin->pwid */
    p->pwid = dc->main;

    RET(1);

 error:
    g_free(dc->cfmt);
    g_free(dc->tfmt);
    g_free(dc->action);
    g_slice_free(dclock, dc);
    RET(0);
}


static void
dclock_destructor(Plugin *p)
{
    dclock *dc = (dclock *)p->priv;

    ENTER;
    if (dc->timer)
        g_source_remove(dc->timer);

    /* g_object_unref( dc->tip ); */
	gtk_widget_destroy(dc->clockw);
	gtk_widget_destroy(dc->main);
    g_free(dc->cfmt);
    g_free(dc->tfmt);
    g_free(dc->action);
    g_slice_free(dclock, dc);
    RET();
}

static void apply_config( Plugin* p )
{
    /* NOTE: This dirty hack is used to force the update of tooltip
       because tooltip will be updated when dc->lastDay != today.
    */
    dclock* dc = (dclock*)p->priv;
    --dc->lastDay;
    clock_update( dc );
}

static void dclock_config( Plugin *p, GtkWindow* parent )
{
    GtkWidget* dlg;
    dclock *dc = (dclock *)p->priv;
    dlg = create_generic_config_dlg( _(p->class->name),
                                     GTK_WIDGET(parent),
                                    (GSourceFunc) apply_config, (gpointer) p,
                                     _("Clock Format"), &dc->cfmt, G_TYPE_STRING,
                                     _("Tooltip Format"), &dc->tfmt, G_TYPE_STRING,
                                     _("Action"), &dc->action, G_TYPE_STRING,
                                     _("Bold font"), &dc->bold, G_TYPE_BOOLEAN,
                                     NULL );
    gtk_window_present( GTK_WINDOW(dlg) );
}

static void save_config( Plugin* p, FILE* fp )
{
    dclock *dc = (dclock *)p->priv;
    lxpanel_put_str( fp, "ClockFmt", dc->cfmt );
    lxpanel_put_str( fp, "TooltipFmt", dc->tfmt );
    lxpanel_put_str( fp, "Action", dc->action );
    lxpanel_put_int( fp, "BoldFont", dc->bold );
}

static void
update_label_orient( Plugin* p )
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

PluginClass dclock_plugin_class = {
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
