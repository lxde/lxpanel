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

#include <stdlib.h>
#include <unistd.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>
#include <string.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "dbg.h"

/* NOTE: dirty hack for g_quark_from_static_string */
#define NAME_ID GPOINTER_TO_UINT("name")
#define PATH_ID GPOINTER_TO_UINT("path")

typedef struct {
    Panel* panel;
    char* image;
    char* path;
    char* name;
    GtkWidget *button;
} dirmenu;

static GdkPixbuf* folder_icon = NULL;

static GtkWidget* create_menu( Plugin* p,
                               const char* path,
                               gboolean open_at_top );

static void open_dir( Plugin* p, const char* path )
{
    char* cmd;
    char* quote = g_shell_quote( path );
    const char* fm = lxpanel_get_file_manager();
    if( strstr( fm, "%s" ) )
        cmd = g_strdup_printf( fm, quote );
    else
        cmd = g_strdup_printf( "%s %s", fm, quote );
    g_free( quote );
    g_spawn_command_line_async( cmd, NULL );
    g_free( cmd );
}

static void on_open_dir( GtkWidget* item, Plugin* p )
{
    GtkWidget* menu = gtk_widget_get_parent(item);
    const char* path = g_object_get_qdata( menu, PATH_ID );
    open_dir( p, path );
}

static void open_in_term( Plugin* p, const char* path )
{
    char* term = g_strdup( lxpanel_get_terminal() );
    char* sp = strchr( term, ' ' );
    if( sp )
        *sp = '\0';
    chdir( path );
    g_spawn_command_line_async( term, NULL );
    g_free( term );
}

static void on_open_in_term( GtkWidget* item, Plugin* p )
{
    GtkWidget* menu = gtk_widget_get_parent(item);
    const char* path = g_object_get_qdata( menu, PATH_ID );
    open_in_term( p, path );
}

static void
menu_pos( GtkMenu *menu, gint *x, gint *y, gboolean *push_in, Plugin* p )
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

static void on_select( GtkMenuItem* item, Plugin* p )
{
    GtkMenu* parent;
    GtkWidget* sub = gtk_menu_item_get_submenu( item );
    char* path;
    if( !sub )
        return;
    parent = (GtkMenu*)gtk_widget_get_parent( (GtkWidget*)item );
    path = (char*)g_object_get_qdata( sub, PATH_ID );
    if( !path ){
        path = g_build_filename( (char*)g_object_get_qdata( parent, PATH_ID ),
                                 (char*)g_object_get_qdata( item, NAME_ID ), NULL );
        sub = create_menu( p, path, TRUE );
        g_free( path );
        gtk_menu_item_set_submenu( item, sub );
    }
}

#if GTK_CHECK_VERSION(2, 10, 0)
/* NOTE: It seems that this doesn't work in older versions of gtk+?? */
static void on_deselect( GtkMenuItem* item, Plugin* p )
{
    /* delete old menu on deselect to save resource */
    gtk_menu_item_set_submenu( item, gtk_menu_new() );
}
#endif

void on_sel_done( GtkWidget *menu, Plugin* p )
{
    gtk_widget_destroy( menu );
    if( folder_icon )
    {
        g_object_unref( folder_icon );
        folder_icon = NULL;
    }
}

static GtkWidget* create_menu( Plugin* p,
                               const char* path,
                               gboolean open_at_top )
{
    GDir* dir;
    GtkWidget *menu = gtk_menu_new();
    GtkWidget *item, *term;
    /* GList *list = NULL; */

    if( G_UNLIKELY(NULL == folder_icon) )
    {
        int w, h;
        gtk_icon_size_lookup_for_settings( gtk_widget_get_settings(menu),
                                                                        GTK_ICON_SIZE_MENU, &w, &h );
        folder_icon = gtk_icon_theme_load_icon( gtk_icon_theme_get_default(),
            "gnome-fs-directory", MAX( w, h ), 0, NULL );

        if( ! folder_icon )
            folder_icon = gtk_widget_render_icon( menu, GTK_STOCK_DIRECTORY, GTK_ICON_SIZE_MENU, NULL );
    }

    g_object_set_qdata_full( menu, PATH_ID, g_strdup(path), g_free );

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
                g_object_set_qdata_full( item, NAME_ID, g_strdup(name), g_free );
                gtk_image_menu_item_set_image( (GtkImageMenuItem*)item,
                                               gtk_image_new_from_stock(GTK_STOCK_DIRECTORY, GTK_ICON_SIZE_MENU) );
                dummy = gtk_menu_new();
                gtk_menu_item_set_submenu( item, dummy );
                gtk_menu_shell_append( GTK_MENU_SHELL(menu), item );
                g_signal_connect( item, "select",
                                  G_CALLBACK(on_select), p );
#if GTK_CHECK_VERSION(2, 10, 0)
                /* NOTE: It seems that this doesn't work in older
                         versions of gtk+?? */
                g_signal_connect( item, "deselect",
                                  G_CALLBACK(on_deselect), p);
#endif
            }
            g_free( full );
        }
        g_dir_close( dir );
    }

    item = gtk_image_menu_item_new_from_stock( GTK_STOCK_OPEN, NULL );
    g_signal_connect( item, "activate",
                      G_CALLBACK( on_open_dir ), p );
    term = gtk_menu_item_new_with_mnemonic( _("Open in _Terminal") );
    g_signal_connect( term, "activate",
                      G_CALLBACK( on_open_in_term ), p );

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

static void show_menu( GtkWidget* widget, Plugin *p, int btn, guint32 time )
{
    dirmenu *dm = (dirmenu *)p->priv;
    char* path = dm->path ? expand_tilda(dm->path) : NULL;
    GtkWidget* menu = create_menu( p,
                                   path ? path : g_get_home_dir(),
                                   FALSE );
    g_free( path );

    g_signal_connect( menu, "selection-done", G_CALLBACK(on_sel_done), NULL );
    gtk_menu_popup( GTK_MENU(menu),
                    NULL, NULL,
                    (GtkMenuPositionFunc)menu_pos, p,
                    btn, time);
}

static gint
clicked (GtkWidget *widget, GdkEventButton *event, Plugin *p)
{
    dirmenu *dm = (dirmenu *)p->priv;

    ENTER;
    if (event->type != GDK_BUTTON_PRESS)
        RET(FALSE);

    if (event->button == 1) {
        show_menu( widget, p, event->button, event->time );
    } else {
        char* path = dm->path ? expand_tilda( dm->path ) : NULL;
        const char* ppath = path ? path : g_get_home_dir();
        if( event->button == 2 )
            open_in_term( p, ppath );
        else
            open_dir( p, ppath );
        g_free( path );
    }

    RET(TRUE);
}

static void
dirmenu_destructor(Plugin *p)
{
    dirmenu *dm = (dirmenu *)p->priv;
    ENTER;
    g_free( dm->image );
    g_free( dm->path );
    g_free(dm);
    RET();
}

static int
dirmenu_constructor(Plugin *p, char **fp)
{
    line s;
    gchar *fname;
    dirmenu *dm;
    int w, h;

    ENTER;
    s.len = 256;
    dm = g_new0(dirmenu, 1);
    g_return_val_if_fail(dm != NULL, 0);

    dm->panel = p->panel;
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
                else if (!g_ascii_strcasecmp(s.t[0], "path")) {
                    dm->path = g_strdup( s.t[1] );
                }
		else if (!g_ascii_strcasecmp(s.t[0], "name")) {
                    dm->name = g_strdup( s.t[1] );
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

    if (! fname)
        fname = strdup("file-manager");

    dm->button = fb_button_new_from_file_with_label(fname, w, h, 0x202020, TRUE, (p->panel->orientation == ORIENT_HORIZ ? dm->name : NULL));

    gtk_container_set_border_width( GTK_CONTAINER(dm->button), 0 );
    g_signal_connect( dm->button, "button_press_event",
                      G_CALLBACK(clicked), p );

    gtk_widget_show( dm->button );
    g_free(fname);

    fname = dm->path ? expand_tilda(dm->path) : NULL;
    gtk_tooltips_set_tip(GTK_TOOLTIPS (dm->panel->tooltips),
                         dm->button,
                         fname ? fname : g_get_home_dir(), NULL);
    g_free( fname );

    /* store the created plugin widget in plugin->pwid */
    p->pwid = dm->button;

    RET(1);

 error:
    g_free(fname);
    dirmenu_destructor(p);
    ERR( "%s - exit\n", __FUNCTION__);
    RET(0);
}

static void save_config( Plugin* p, FILE* fp )
{
    dirmenu* dm = (dirmenu*)p->priv;
    lxpanel_put_str( fp, "path", dm->path );
    lxpanel_put_str( fp, "image", dm->image );
}

PluginClass dirmenu_plugin_class = {
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
