#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "bg.h"

#include "eggtraymanager.h"
#include "fixedtip.h"


//#define DEBUG
#include "dbg.h"


typedef struct {
    Plugin *plug;
    GtkWidget *box;
    NaTrayManager *tray_manager;
    int icon_num;
} tray;

//static void run_gtktray(tray *tr);

static void
force_redraw (tray *tr)
{
    /* Force the icons to redraw their backgrounds.
    * gtk_widget_queue_draw() doesn't work across process boundaries,
    * so we do this instead.
    */
    GtkWidget* w = tr->plug->pwid;
    gtk_widget_set_size_request( w, w->allocation.width, w->allocation.height );
    gtk_widget_hide (tr->box);
    if( gtk_events_pending() )
        gtk_main_iteration();
    gtk_widget_show (tr->box);
    gtk_widget_set_size_request( w, -1, -1 );
}


static void
tray_added (NaTrayManager *manager, GtkWidget *icon, tray *tr)
{
    gtk_box_pack_end (GTK_BOX (tr->box), icon, FALSE, FALSE, 0);
    gtk_widget_show (icon);
    /* g_debug( "add icon %p", icon ); */
    gdk_display_sync( gtk_widget_get_display(icon) );
    
    /* FIXME: This is very dirty, but it works. */
    plugin_widget_set_background( tr->plug->pwid, tr->plug->panel );
    /* plugin_widget_set_background( icon, tr->plug->panel ); */

    if (!tr->icon_num) {
        DBG("first icon\n");
        gtk_widget_show_all(tr->box);
    }
    force_redraw(tr);
    tr->icon_num++;
    DBG("add icon\n");
}

static void
tray_removed (NaTrayManager *manager, GtkWidget *icon, tray *tr)
{
    tr->icon_num--;
    DBG("del icon\n");
    if (!tr->icon_num) {
        gtk_widget_hide(tr->box);
        DBG("last icon\n");
    }

    force_redraw(tr);
}

static void
message_sent (NaTrayManager *manager, GtkWidget *icon, const char *text, glong id, glong timeout,
              void *data)
{
    /* FIXME multihead */
    int x, y;

    gdk_window_get_origin (icon->window, &x, &y);
    fixed_tip_show (0, x, y, FALSE, gdk_screen_height () - 50, text);
}

static void
message_cancelled (NaTrayManager *manager, GtkWidget *icon, glong id,
                   void *data)
{

}



static void
tray_destructor(Plugin *p)
{
    tray *tr = (tray *)p->priv;

    ENTER;
    /* Make sure we drop the manager selection */
    if (tr->tray_manager)
        g_object_unref (G_OBJECT (tr->tray_manager));
    fixed_tip_hide ();
    g_free(tr);
    RET();
}

/* Dirty hacks used to handle background of systray */
static gboolean delay_update_bg( gpointer pl_gpointer )
{
        Plugin* pl = (Plugin *)(pl_gpointer);
	plugin_widget_set_background( pl->pwid, pl->panel );
	return FALSE;
}

static void tray_realized( GtkWidget* widget, Plugin* p )
{
	g_idle_add( delay_update_bg, p );
}

static int
tray_constructor(Plugin *p, char** fp)
{
    line s;
    tray *tr;
    GdkScreen *screen;

    ENTER;
    s.len = 256;
    if( fp )
    {
        while ( lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            /* g_debug("s.str = \'%s\'", s.str); */
            ERR("tray: illegal in this context %s\n", s.str);
            RET(0);
        }
    }
    tr = g_new0(tray, 1);
    g_return_val_if_fail(tr != NULL, 0);
    p->priv = tr;
    tr->plug = p;
    tr->icon_num = 0;

    tr->box = p->panel->my_box_new(FALSE, 0);

    screen = gtk_widget_get_screen (GTK_WIDGET (p->panel->topgwin));

    if (na_tray_manager_check_running(screen)) {
        tr->tray_manager = NULL;
        ERR("tray: another systray already running\n");
        RET(1);
    }
    tr->tray_manager = na_tray_manager_new ();
    if (!na_tray_manager_manage_screen (tr->tray_manager, screen))
        g_printerr ("tray: System tray didn't get the system tray manager selection\n");

    g_signal_connect (tr->tray_manager, "tray_icon_added", G_CALLBACK (tray_added), tr);
    g_signal_connect (tr->tray_manager, "tray_icon_removed", G_CALLBACK (tray_removed), tr);
    g_signal_connect (tr->tray_manager, "message_sent", G_CALLBACK (message_sent), tr);
    g_signal_connect (tr->tray_manager, "message_cancelled", G_CALLBACK (message_cancelled), tr);

    gtk_widget_show_all(tr->box);
    gtk_widget_set_size_request (tr->box, -1, -1);

    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( p->pwid, GTK_NO_WINDOW );
    gtk_widget_set_name( p->pwid, "tray" );	/* this hack is requierd for plugin_widget_set_background() */

    gtk_container_add( (GtkContainer*)p->pwid, tr->box );
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 1);

	g_signal_connect( p->pwid, "realize", G_CALLBACK( tray_realized ), p );

    RET(1);

}

static void orientation_changed( Plugin* p )
{
    tray *tr = (tray *)p->priv;
    GtkBox* newbox;
    GList *l, *children;
    int width = 0, height = 0;

    /* another systray already running */
    if (!tr->tray_manager)
        return;

    na_tray_manager_set_orientation( tr->tray_manager, p->panel->orientation );

    /* weird... na_tray_manager will re-add the icons to the tray automatically... */
    gtk_widget_destroy( tr->box );
    tr->box = p->panel->my_box_new( FALSE, 0 );
    gtk_container_add(GTK_CONTAINER(p->pwid), tr->box);

    force_redraw(tr);
}

PluginClass tray_plugin_class = {
    fname: NULL,
    count: 0,

    type : "tray",
    name : N_("System Tray"),
    version: "1.0",
    description : N_("Old KDE/GNOME Tray"),

    constructor : tray_constructor,
    destructor  : tray_destructor,
    config : NULL,
    save : NULL,
    orientation : orientation_changed
};
