#include <stdlib.h>
#include <unistd.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "gtkbgbox.h"
#include "dbg.h"

typedef struct {
    GtkTooltips *tips;
    char* image;
    char* path;
    char* cmd;
    char* term;
    GtkWidget *button;
} dirmenu;

static GtkWidget* create_menu( const char* path, gboolean open_at_top );

static void open_dir( const char* path )
{
    char cmd[1024];
    /* FIXME: File manager shouldn't be hardcoded.
              This must be fixed before next release.
     */
    g_snprintf( cmd, 1024, "pcmanfm %s", path );
    g_debug( cmd );
    g_spawn_command_line_async( cmd, NULL );
}

static void open_in_term( const char* path )
{
    char cmd[1024];
    chdir( path );
    /* FIXME: Terminal emulator shouldn't be hardcoded.
              This must be fixed before next release.
     */
    g_spawn_command_line_async( "x-terminal-emulator", NULL );
}

static void
menu_pos( GtkMenu *menu, gint *x, gint *y, gboolean *push_in, plugin* p )
{
    int ox, oy, w, h;
    dirmenu *dm = (dirmenu *)p->priv;

    ENTER;
    gdk_window_get_origin( dm->button->window, &ox, &oy );
    w = GTK_WIDGET(menu)->requisition.width;
    h = GTK_WIDGET(menu)->requisition.height;
    if (p->panel->orientation == ORIENT_HORIZ) {
        *x = ox;
        if (*x + w > gdk_screen_width())
            *x = ox + dm->button->allocation.width - w;
        *y = oy - h;
        if (*y < 0)
            *y = oy + dm->button->allocation.height;
    } else {
        *x = ox + dm->button->allocation.width;
        if (*x > gdk_screen_width())
            *x = ox - w;
        *y = oy;
        if (*y + h >  gdk_screen_height())
            *y = oy + dm->button->allocation.height - h;
    }
    *push_in = TRUE;
    RET();
}

static void on_select( GtkMenuItem* item, GtkMenu* parent )
{
    GtkWidget* sub = gtk_menu_item_get_submenu( item );
    char* path;
    if( !sub )
        return;
    path = (char*)g_object_get_data( sub, "path" );
    if( !path ){
        path = g_build_filename( (char*)g_object_get_data( parent, "path" ),
                                 (char*)g_object_get_data( item, "name" ), NULL );
        sub = create_menu( path, TRUE );
        g_free( path );
        gtk_menu_item_set_submenu( item, sub );
    }
}

static void on_deselect( GtkMenuItem* item, GtkMenu* parent )
{
    /* delete old menu on deselect to save resource */
    gtk_menu_item_set_submenu( item, gtk_menu_new() );
}

GtkWidget* create_menu( const char* path, gboolean open_at_top )
{
    GDir* dir;
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item, *term;
    /* GList *list = NULL; */

    g_object_set_data_full( menu, "path", g_strdup(path), g_free );

    if( dir = g_dir_open( path, 0, NULL ) ) {
        const char* name;
        while( name = g_dir_read_name( dir ) ) {
            if( name[0] == '.' )
                continue;
            char* full = g_build_filename( path, name, NULL );
            if( g_file_test( full, G_FILE_TEST_IS_DIR) ) {
                char* disp = g_filename_display_name( name );
                GtkWidget *dummy;
                item = gtk_image_menu_item_new_with_label( disp );
                g_free( disp );
                g_object_set_data_full( item, "name", g_strdup(name), g_free );
                gtk_image_menu_item_set_image( (GtkImageMenuItem*)item,
                                               gtk_image_new_from_stock(GTK_STOCK_DIRECTORY, GTK_ICON_SIZE_MENU) );
                dummy = gtk_menu_new();
                gtk_menu_item_set_submenu( item, dummy );
                gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );
                g_signal_connect( item, "select", G_CALLBACK(on_select), menu);
                g_signal_connect( item, "deselect", G_CALLBACK(on_deselect), menu);
            }
            g_free( full );
        }
        g_dir_close( dir );
    }

    item = gtk_menu_item_new_with_mnemonic( _("_Open") );
    g_signal_connect_swapped( item, "activate",
                              G_CALLBACK( open_dir ),
                              g_object_get_data( menu, "path" ) );
    term = gtk_menu_item_new_with_mnemonic( _("Open in _Terminal") );
    g_signal_connect_swapped( term, "activate",
                              G_CALLBACK( open_in_term ),
                              g_object_get_data( menu, "path" ) );
    if( open_at_top ) {
        gtk_menu_shell_insert( GTK_MENU_SHELL(menu), gtk_separator_menu_item_new(), 0 );
        gtk_menu_shell_insert( GTK_MENU_SHELL(menu), term, 0 );
        gtk_menu_shell_insert( GTK_MENU_SHELL(menu), item, 0 );
    }
    else {
        gtk_menu_shell_append( GTK_MENU_SHELL(menu), gtk_separator_menu_item_new() );
        gtk_menu_shell_append( GTK_MENU_SHELL(menu), term );
        gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );
    }

    gtk_widget_show_all( menu );
    return menu;
}

static void show_menu( GtkWidget* widget, plugin *p, int btn, guint32 time )
{
    dirmenu *dm = (dirmenu *)p->priv;
    char* path = dm->path ? expand_tilda(dm->path) : NULL;
    GtkWidget* menu = create_menu( path ? path : g_get_home_dir(), FALSE );
    g_free( path );

    g_signal_connect( menu, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL );
    gtk_menu_popup( GTK_MENU(menu),
                    NULL, NULL,
                    (GtkMenuPositionFunc)menu_pos, p,
                    btn, time);
}

static gint
clicked (GtkWidget *widget, GdkEventButton *event, gpointer data)
{
    plugin *p = (plugin*)data;
    dirmenu *dm = (dirmenu *)p->priv;

    ENTER;
    if (event->type != GDK_BUTTON_PRESS)
        RET(FALSE);

    if (event->button == 1) {
        show_menu( widget, p, event->button, event->time );
    } else {
        open_dir( dm->path );
    }

    RET(FALSE);
}

static void
dirmenu_destructor(plugin *p)
{
    dirmenu *dm = (dirmenu *)p->priv;
    ENTER;
    g_free( dm->image );
    g_free( dm->path );
    g_object_unref( dm->tips );
    g_free(dm);
    RET();
}

static int
dirmenu_constructor(plugin *p, char **fp)
{
    line s;
    gchar *fname;
    dirmenu *dm;
    int w, h;

    ENTER;
    s.len = 256;
    dm = g_new0(dirmenu, 1);
    g_return_val_if_fail(dm != NULL, 0);
    dm->tips = gtk_tooltips_new();
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( dm->tips );
#else
    g_object_ref( dm->tips );
    gtk_object_sink( dm->tips );
#endif
    p->priv = dm;
    fname = NULL;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "dirmenu: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "image")) {
                    dm->image = g_strdup( s.t[1] );
                    fname = expand_tilda(s.t[1]);
                }
                else if (!g_ascii_strcasecmp(s.t[0], "cmd")) {
                    dm->cmd = g_strdup( s.t[1] );
                }
                else if (!g_ascii_strcasecmp(s.t[0], "term")) {
                    dm->cmd = g_strdup( s.t[1] );
                }
                else if (!g_ascii_strcasecmp(s.t[0], "path")) {
                    dm->path = g_strdup( s.t[1] );
                }
                else {
                    ERR( "dirmenu: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "dirmenu: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }
    if (p->panel->orientation == ORIENT_HORIZ) {
        w = 10000;
        h = p->panel->ah;
    } else {
        w = p->panel->aw;
        h = 10000;
    }

    dm->button = fb_button_new_from_file(fname, w, h, 0x202020, TRUE);
    gtk_container_set_border_width( GTK_CONTAINER(dm->button), 0 );
    g_signal_connect( dm->button, "button_press_event",
                      G_CALLBACK(clicked), p );

    gtk_widget_show( dm->button );
    gtk_container_add( GTK_CONTAINER(p->pwid), dm->button );
    if (p->panel->transparent) 
        gtk_bgbox_set_background( dm->button, BG_ROOT, p->panel->tintcolor, p->panel->alpha );

    g_free(fname);
    gtk_tooltips_set_tip(GTK_TOOLTIPS (dm->tips), dm->button, dm->path, NULL);
    RET(1);

 error:
    g_free(fname);
    dirmenu_destructor(p);
    ERR( "%s - exit\n", __FUNCTION__);
    RET(0);
}

static void save_config( plugin* p, FILE* fp )
{
    dirmenu* dm = (dirmenu*)p->priv;
    lxpanel_put_str( fp, "path", dm->path );
    lxpanel_put_str( fp, "image", dm->image );
/*
    lxpanel_put_str( fp, "cmd", dm->cmd );
    lxpanel_put_str( fp, "term", dm->term );
*/
}

plugin_class dirmenu_plugin_class = {
    fname: NULL,
    count: 0,

    type : "dirmenu",
    name : N_("Directory Menu"),
    version: "1.0",
    description : N_("Browse directory tree via menu (Author: PCMan)"),

    constructor : dirmenu_constructor,
    destructor  : dirmenu_destructor,
    config : NULL,
    save : save_config
};
