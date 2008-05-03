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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <locale.h>
#include <string.h>
#include <gdk/gdkx.h>

#include "plugin.h"
#include "panel.h"
#include "misc.h"
#include "bg.h"

#include "glib-mem.h"
#include "lxpanelctl.h"
#include "dbg.h"

static gchar *cfgfile = NULL;
static gchar version[] = VERSION;
gchar *cprofile = "default";

static int config = 0;
FbEv *fbev = NULL;

int log_level;

GSList* all_panels = NULL;  /* a single-linked list storing all panels */

gboolean is_restarting = FALSE;

static int panel_start( Panel *p, char **fp );
void panel_config_save(Panel* panel);   /* defined in configurator.c */

/****************************************************
 *         panel's handlers for WM events           *
 ****************************************************/
/*
static void
panel_del_wm_strut(Panel *p)
{
    XDeleteProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT);
    XDeleteProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT_PARTIAL);
}
*/


void panel_set_wm_strut(Panel *p)
{
    gulong data[12] = { 0 };
    int i = 4;

    if (!GTK_WIDGET_MAPPED (p->topgwin))
        return;
    if ( ! p->setstrut )
    {
        XDeleteProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT_PARTIAL);
        /* old spec, for wms that do not support STRUT_PARTIAL */
        XDeleteProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT);
        return;
    }

    switch (p->edge) {
    case EDGE_LEFT:
        i = 0;
        data[i] = p->aw;
        data[4 + i*2] = p->ay;
        data[5 + i*2] = p->ay + p->ah;
        break;
    case EDGE_RIGHT:
        i = 1;
        data[i] = p->aw;
        data[4 + i*2] = p->ay;
        data[5 + i*2] = p->ay + p->ah;
        break;
    case EDGE_TOP:
        i = 2;
        data[i] = p->ah;
        data[4 + i*2] = p->ax;
        data[5 + i*2] = p->ax + p->aw;
        break;
    case EDGE_BOTTOM:
        i = 3;
        data[i] = p->ah;
        data[4 + i*2] = p->ax;
        data[5 + i*2] = p->ax + p->aw;
        break;
    default:
        ERR("wrong edge %d. strut won't be set\n", p->edge);
        RET();
    }
    DBG("type %d. width %d. from %d to %d\n", i, data[i], data[4 + i*2], data[5 + i*2]);

    /* if wm supports STRUT_PARTIAL it will ignore STRUT */
    XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT_PARTIAL,
          XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) data, 12);
    /* old spec, for wms that do not support STRUT_PARTIAL */
    XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT,
          XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) data, 4);

    RET();
}

static void
print_wmdata(Panel *p)
{
    int i;

    RET();
    DBG("desktop %d/%d\n", p->curdesk, p->desknum);
    DBG("workarea\n");
    for (i = 0; i < p->wa_len/4; i++)
        DBG("(%d, %d) x (%d, %d)\n",
              p->workarea[4*i + 0],
              p->workarea[4*i + 1],
              p->workarea[4*i + 2],
              p->workarea[4*i + 3]);
    RET();
}


/* defined in plugins/menu.c */
gboolean show_system_menu( gpointer system_menu );

/* defined in configurator.c */
void panel_configure(Panel* p, int sel_page );

/* built-in commands, defined in configurator.c */
void restart(void);
void gtk_run(void);

static void process_client_msg ( XClientMessageEvent* ev )
{
    int cmd = ev->data.b[0];
    switch( cmd )
    {
        case LXPANEL_CMD_SYS_MENU:
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                Panel* p = (Panel*)l->data;
                if( p->system_menus )
                {
                    /* show_system_menu( p->system_menus->data ); */
                    /* FIXME: I've no idea why this doesn't work without timeout
                              under some WMs, like icewm. */
                    g_timeout_add( 200, (GSourceFunc)show_system_menu,
                                   p->system_menus->data );
                }
            }
            break;
        }
        case LXPANEL_CMD_RUN:
            gtk_run();
            break;
        case LXPANEL_CMD_CONFIG:
            //FIXME: configure();
            break;
        case LXPANEL_CMD_RESTART:
            restart();
            break;
        case LXPANEL_CMD_EXIT:
            gtk_main_quit();
            break;
    }
}

static GdkFilterReturn
panel_event_filter(GdkXEvent *xevent, GdkEvent *event, gpointer not_used)
{
    Atom at;
    Window win;
    XEvent *ev = (XEvent *) xevent;

    ENTER;
    DBG("win = 0x%x\n", ev->xproperty.window);
    if (ev->type != PropertyNotify ) {
        /* private client message from lxpanelctl */
        if( ev->type == ClientMessage && ev->xproperty.atom == a_LXPANEL_CMD )
        {
            process_client_msg( (XClientMessageEvent*)ev );
        }
        else if( ev->type == DestroyNotify )
        {
            fb_ev_emit_destroy( fbev, ((XDestroyWindowEvent*)ev)->window );
        }
        RET(GDK_FILTER_CONTINUE);
    }

    at = ev->xproperty.atom;
    win = ev->xproperty.window;
    if (win == GDK_ROOT_WINDOW()) {
        if (at == a_NET_CLIENT_LIST) {
                fb_ev_emit(fbev, EV_CLIENT_LIST);
        } else if (at == a_NET_CURRENT_DESKTOP) {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((Panel*)l->data)->curdesk = get_net_current_desktop();
            fb_ev_emit(fbev, EV_CURRENT_DESKTOP);
        } else if (at == a_NET_NUMBER_OF_DESKTOPS) {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((Panel*)l->data)->desknum = get_net_number_of_desktops();
            fb_ev_emit(fbev, EV_NUMBER_OF_DESKTOPS);
        } else if (at == a_NET_DESKTOP_NAMES) {
            fb_ev_emit(fbev, EV_DESKTOP_NAMES);
        } else if (at == a_NET_ACTIVE_WINDOW) {
            fb_ev_emit(fbev, EV_ACTIVE_WINDOW );
        } else if (at == a_NET_CLIENT_LIST_STACKING) {
            fb_ev_emit(fbev, EV_CLIENT_LIST_STACKING);
        } else if (at == a_XROOTPMAP_ID) {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                Panel* p = (Panel*)l->data;
                if (p->transparent) {
                    fb_bg_notify_changed_bg(p->bg);
                }
            }
        } else if (at == a_NET_WORKAREA) {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                Panel* p = (Panel*)l->data;
                g_free( p->workarea );
                p->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->wa_len);
                print_wmdata(p);
            }
        } else
            return GDK_FILTER_CONTINUE;

        return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
}

/****************************************************
 *         panel's handlers for GTK events          *
 ****************************************************/


static gint
panel_delete_event(GtkWidget * widget, GdkEvent * event, gpointer data)
{
    ENTER;
    RET(FALSE);
}

static gint
panel_destroy_event(GtkWidget * widget, GdkEvent * event, gpointer data)
{
    //Panel *p = (Panel *) data;
    //if (!p->self_destroy)
    gtk_main_quit();
    RET(FALSE);
}

static void
on_root_bg_changed(FbBg *bg, Panel* p)
{
    panel_update_background( p );
}

/* This function should only be called after the panel has been realized */
void panel_update_background( Panel* p )
{
    GList* l;
    GdkPixmap* pixmap = NULL;

    /* handle background image of panel */
    gtk_widget_set_app_paintable(p->topgwin, TRUE);

    if (p->background) {
        pixmap = fb_bg_get_pix_from_file(p->topgwin, p->background_file);
        if( p->bg )
        {
            g_object_unref( p->bg );
            p->bg = NULL;
        }
    } else if (p->transparent) {
        if( ! p->bg )
        {
            p->bg = fb_bg_get_for_display();
            g_signal_connect(G_OBJECT(p->bg), "changed", G_CALLBACK(on_root_bg_changed), p);
        }
        pixmap = fb_bg_get_xroot_pix_for_win( p->bg, p->topgwin );

        if (pixmap && pixmap !=  GDK_NO_BG) {
            if (p->alpha)
                fb_bg_composite( pixmap, p->topgwin->style->black_gc, p->tintcolor, p->alpha );
        }
    }
    else
    {
        if( p->bg )
        {
            g_object_unref( p->bg );
            p->bg = NULL;
        }
    }

    if( pixmap )
    {
        gtk_widget_set_app_paintable( p->topgwin, TRUE );
        gdk_window_set_back_pixmap( p->topgwin->window, pixmap, FALSE );
        g_object_unref( pixmap );
    }
    else
    {
//        gdk_window_set_back_pixmap( p->topgwin->window, p->topgwin->style->bg_pixmap[0], FALSE );
        gtk_widget_set_app_paintable( p->topgwin, FALSE );
//        gdk_window_set_background( p->topgwin->window, &p->topgwin->style->bg[0] );
    }

    for( l = p->plugins; l; l = l->next )
    {
        Plugin* pl = (Plugin*)l->data;
        plugin_set_background( pl, p );
    }

    gdk_window_clear( p->topgwin->window );
    gtk_widget_queue_draw( p->topgwin );
}

static gboolean delay_update_background( Panel* p )
{
    panel_update_background( p );
    return FALSE;
}

static void
panel_realize(GtkWidget *widget, Panel *p)
{
    g_idle_add_full( G_PRIORITY_LOW, delay_update_background, p, NULL );
}

static void
panel_style_set(GtkWidget *widget, GtkStyle* prev, Panel *p)
{
    /* FIXME: This dirty hack is used to fix the background of systray... */
    if( GTK_WIDGET_REALIZED( widget ) )
        g_idle_add_full( G_PRIORITY_LOW, delay_update_background, p, NULL );
}

static gint
panel_size_req(GtkWidget *widget, GtkRequisition *req, Panel *p)
{
    ENTER;

    if (p->widthtype == WIDTH_REQUEST)
        p->width = (p->orientation == ORIENT_HORIZ) ? req->width : req->height;
    if (p->heighttype == HEIGHT_REQUEST)
        p->height = (p->orientation == ORIENT_HORIZ) ? req->height : req->width;
    calculate_position(p);
    req->width  = p->aw;
    req->height = p->ah;

    RET( TRUE );
}

static gint
panel_size_alloc(GtkWidget *widget, GtkAllocation *a, Panel *p)
{
    ENTER;
    if (p->widthtype == WIDTH_REQUEST)
        p->width = (p->orientation == ORIENT_HORIZ) ? a->width : a->height;
    if (p->heighttype == HEIGHT_REQUEST)
        p->height = (p->orientation == ORIENT_HORIZ) ? a->height : a->width;
    calculate_position(p);

    if (a->width == p->aw && a->height == p->ah && a->x == p->ax && a->y == p ->ay) {
        RET(TRUE);
    }

    gtk_window_move(GTK_WINDOW(p->topgwin), p->ax, p->ay);
    panel_set_wm_strut(p);
    RET(TRUE);
}

static  gboolean
panel_configure_event (GtkWidget *widget, GdkEventConfigure *e, Panel *p)
{
    ENTER;
    if (e->width == p->cw && e->height == p->ch && e->x == p->cx && e->y == p->cy)
        RET(TRUE);
    p->cw = e->width;
    p->ch = e->height;
    p->cx = e->x;
    p->cy = e->y;

    if (p->transparent)
        fb_bg_notify_changed_bg(p->bg);

    RET(FALSE);
}

static gint
panel_popupmenu_configure(GtkWidget *widget, gpointer user_data)
{
    panel_configure( (Panel*)user_data, 0 );
    return TRUE;
}

static gint
panel_press_button_event(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
    GdkEventButton *event_button;
    GtkWidget* img;

    g_return_val_if_fail (event != NULL, FALSE);
    event_button = (GdkEventButton *)event;
    if (event_button->button == 3) {
            GtkWidget *menu;
            Panel* panel = (Panel*)user_data;
            /* create menu */
            menu = lxpanel_get_panel_menu( panel, NULL, FALSE );
            gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event_button->button, event_button->time);
            return TRUE;
    }

    return FALSE;
}

static void panel_popupmenu_config_plugin( GtkMenuItem* item, Plugin* plugin )
{
    plugin->class->config( plugin, plugin->panel->topgwin );
}

#if 0
static void on_add_plugin_response( GtkDialog* dlg,
                                    int response,
                                    Panel* p )
{
    if( response == GTK_RESPONSE_OK )
    {
        GtkTreeView* view;
        GtkTreeSelection* tree_sel;
        GtkTreeIter it;
        GtkTreeModel* model;

        view = (GtkTreeView*)g_object_get_data( G_OBJECT(dlg), "avail-plugins" );
        tree_sel = gtk_tree_view_get_selection( view );
        if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        {
            char* type = NULL;
            Plugin* pl;
            gtk_tree_model_get( model, &it, 1, &type, -1 );
            if( pl = plugin_load( type ) )
            {
                GtkTreePath* tree_path;

                pl->panel = p;
                plugin_start( pl, NULL );
                p->plugins = g_list_append(p->plugins, pl);
                /* FIXME: will show all cause problems? */
                gtk_widget_show_all( pl->pwid );

                /* update background of the newly added plugin */
                plugin_widget_set_background( pl->pwid, pl->panel );
            }
            g_free( type );
        }
    }
    gtk_widget_destroy( (GtkWidget*)dlg );
}

void panel_add_plugin( Panel* panel, GtkWindow* parent_win )
{
    GtkWidget* dlg, *scroll;
    GList* classes;
    GList* tmp;
    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    GtkTreeView* view;
    GtkListStore* list;
    GtkTreeSelection* tree_sel;

    classes = plugin_get_available_classes();

    dlg = gtk_dialog_new_with_buttons( _("Add plugin to panel"),
                                       GTK_WINDOW(parent_win), 0,
                                       GTK_STOCK_CANCEL,
                                       GTK_RESPONSE_CANCEL,
                                       GTK_STOCK_ADD,
                                       GTK_RESPONSE_OK, NULL );

    /* gtk_widget_set_sensitive( parent_win, FALSE ); */
    scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_shadow_type( (GtkScrolledWindow*)scroll,
                                          GTK_SHADOW_IN );
    gtk_scrolled_window_set_policy((GtkScrolledWindow*)scroll,
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC );
    gtk_box_pack_start( (GtkBox*)GTK_DIALOG(dlg)->vbox, scroll,
                         TRUE, TRUE, 4 );
    view = (GtkTreeView*)gtk_tree_view_new();
    gtk_container_add( (GtkContainer*)scroll, view );
    tree_sel = gtk_tree_view_get_selection( view );
    gtk_tree_selection_set_mode( tree_sel, GTK_SELECTION_BROWSE );

    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
                                            _("Available plugins"),
                                            render, "text", 0, NULL );
    gtk_tree_view_append_column( view, col );

    list = gtk_list_store_new( 2,
                               G_TYPE_STRING,
                               G_TYPE_STRING );

    for( tmp = classes; tmp; tmp = tmp->next ) {
        PluginClass* pc = (PluginClass*)tmp->data;
        if( ! pc->invisible ) {
            /* FIXME: should we display invisible plugins? */
            GtkTreeIter it;
            gtk_list_store_append( list, &it );
            gtk_list_store_set( list, &it,
                                0, _(pc->name),
                                1, pc->type, -1 );
            /* g_debug( "%s (%s)", pc->type, _(pc->name) ); */
        }
    }

    gtk_tree_view_set_model( view, GTK_TREE_MODEL(list) );
    g_object_unref( list );

    g_signal_connect( dlg, "response",
                      on_add_plugin_response, panel );
    g_object_set_data( dlg, "avail-plugins", view );
    g_object_weak_ref( dlg, plugin_class_list_free, classes );

    gtk_window_set_default_size( (GtkWindow*)dlg, 320, 400 );
    gtk_widget_show_all( dlg );
}
#endif

static void panel_popupmenu_add_item( GtkMenuItem* item, Panel* panel )
{
    /* panel_add_plugin( panel, panel->topgwin ); */
    panel_configure( panel, 1 );
}

static void panel_popupmenu_remove_item( GtkMenuItem* item, Plugin* plugin )
{
    Panel* panel = plugin->panel;
    panel->plugins = g_list_remove( panel->plugins, plugin );
    plugin_stop( plugin ); /* free the plugin widget & its data */
    plugin_put( plugin ); /* free the lib if necessary */

    panel_config_save( plugin->panel );
}

/* FIXME: Potentially we can support multiple panels at the same edge,
 * but currently this cannot be done due to some positioning problems. */
static char* gen_panel_name( int edge )
{
    const char* edge_str = num2str( edge_pair, edge, "" );
    char* name = NULL;
    char* dir = get_config_file( cprofile, "panels", FALSE );
    int i;
    for( i = 0; i < G_MAXINT; ++i )
    {
        char* f;
        if( G_LIKELY( i > 0 ) )
            name =  g_strdup_printf( "%s%d", edge_str, i );
        else
            name = g_strdup( edge_str );
        f = g_build_filename( dir, name, NULL );
        if( ! g_file_test( f, G_FILE_TEST_EXISTS ) )
        {
            g_free( f );
            break;
        }
        g_free( name );
        g_free( f );
    }
    g_free( dir );
    return name;
}

/* FIXME: Potentially we can support multiple panels at the same edge,
 * but currently this cannot be done due to some positioning problems. */
static void panel_popupmenu_create_panel( GtkMenuItem* item, Panel* panel )
{
    int i;
    GSList* group = NULL;
    GtkWidget* btns[ 4 ], *box, *frame;
    const char* edges[]={N_("Left"), N_("Right"), N_("Top"), N_("Bottom")};
    GtkWidget* dlg = gtk_dialog_new_with_buttons(
                                        _("Create New Panel"),
                                        panel->topgwin,
                                        GTK_DIALOG_MODAL,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OK, GTK_RESPONSE_OK, NULL );
    gtk_container_set_border_width( (GtkContainer*)dlg, 8 );
    frame = gtk_frame_new( _("Where to put the panel?"));
    gtk_box_pack_start( (GtkBox*)((GtkDialog*)dlg)->vbox, frame, TRUE, TRUE, 0 );
    box = gtk_vbox_new( FALSE, 2 );
    gtk_container_add( (GtkContainer*)frame, box );
    for( i = 0; i < 4; ++i )
    {
        GSList* l;
        btns[ i ] = gtk_radio_button_new_with_label( group, _(edges[i]) );
        group = gtk_radio_button_get_group( (GtkRadioButton*)btns[ i ] );
        gtk_box_pack_start( box, btns[ i ], FALSE, TRUE, 2 );
        for( l = all_panels; l; l = l->next )
        {
            Panel* p = (Panel*)l->data;
            /* If there is already a panel on this edge */
            if( p->edge == (i + 1) )
                gtk_widget_set_sensitive( btns[i], FALSE );
            /* FIXME: multiple panel at the same edge should be supported in the future. */
        }
    }
    gtk_widget_show_all( dlg );

    if( gtk_dialog_run( dlg ) == GTK_RESPONSE_OK )
    {
        char* pfp;
        char default_conf[128];
        for( i = 0; i < 4; ++i )
        {
            if( gtk_toggle_button_get_active( (GtkToggleButton*)btns[i] ) )
                break;
        }
        ++i;    /* 0 is EDGE_NONE, all edge values start from 1 */
        g_snprintf( default_conf, 128,
                "global{\n"
                "edge=%s\n"
                "}\n",
                num2str( edge_pair, i, "bottom" ) );
        panel = g_slice_new0( Panel );
        panel->name = gen_panel_name(i);
        pfp = default_conf;
        if ( panel_start( panel, &pfp )) {
            panel_config_save( panel );
            all_panels = g_slist_prepend( all_panels, panel );
        }
        else {
            panel_destroy( panel );
        }
    }
    gtk_widget_destroy( dlg );
}

static void panel_popupmenu_delete_panel( GtkMenuItem* item, Panel* panel )
{
    GtkWidget* dlg;
    gboolean ok;
    dlg = gtk_message_dialog_new_with_markup( panel->topgwin,
                                                    GTK_DIALOG_MODAL,
                                                    GTK_MESSAGE_QUESTION,
                                                    GTK_BUTTONS_OK_CANCEL,
                                                    _("Really delete this panel?\n<b>Warning: This can not be recovered.</b>") );
    gtk_window_set_title( (GtkWindow*)dlg, _("Confirm") );
    ok = ( gtk_dialog_run( (GtkDialog*)dlg ) == GTK_RESPONSE_OK );
    gtk_widget_destroy( dlg );
    if( ok )
    {
        gchar *fname, *dir;
        all_panels = g_slist_remove( all_panels, panel );

        /* delete the config file of this panel */
        dir = get_config_file( cprofile, "panels", FALSE );
        fname = g_build_filename( dir, panel->name, NULL );
        g_free( dir );
        g_unlink( fname );

        panel_destroy( panel );
    }
}

extern GtkWidget* lxpanel_get_panel_menu( Panel* panel, Plugin* plugin, gboolean use_sub_menu )
{
    GtkWidget *ret,*menu, *menu_item, *img;
    char* tmp;
    ret = menu = gtk_menu_new();

/*
    img = gtk_image_new_from_stock( GTK_STOCK_ADD, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Add Item To Panel"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_add_item), panel );
*/

    img = gtk_image_new_from_stock( GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Add / Remove Panel Items"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_add_item), panel );

    if( plugin )
    {
        img = gtk_image_new_from_stock( GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU );
        tmp = g_strdup_printf( _("Remove \"%s\" From Panel"), _(plugin->class->name) );
        menu_item = gtk_image_menu_item_new_with_label( tmp );
        g_free( tmp );
        gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
        g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_remove_item), plugin );
    }

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

    img = gtk_image_new_from_stock( GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Panel Settings"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect(G_OBJECT(menu_item), "activate", G_CALLBACK(panel_popupmenu_configure), panel );

    img = gtk_image_new_from_stock( GTK_STOCK_NEW, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Create New Panel"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_create_panel), panel );
    /* FIXME: Potentially we can support multiple panels at the same edge,
     * but currently this cannot be done due to some positioning problems. */
    /* currently, disable the option when there are already four panels */
    if( g_slist_length( all_panels ) >= 4 )
        gtk_widget_set_sensitive( menu_item, FALSE );

    img = gtk_image_new_from_stock( GTK_STOCK_DELETE, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Delete This Panel"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_delete_panel), panel );
    if( ! all_panels->next )    /* if this is the only panel */
        gtk_widget_set_sensitive( menu_item, FALSE );

    if( use_sub_menu )
    {
        ret = gtk_menu_new();
        menu_item = gtk_image_menu_item_new_with_label(_("Panel"));
        gtk_menu_shell_append(GTK_MENU_SHELL(ret), menu_item);
        gtk_menu_item_set_submenu( menu_item, menu );

        gtk_widget_show_all(ret);
    }

    if( plugin )
    {
        menu_item = gtk_separator_menu_item_new();
        gtk_menu_shell_prepend(GTK_MENU_SHELL(ret), menu_item);

        img = gtk_image_new_from_stock( GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU );
        tmp = g_strdup_printf( _("\"%s\" Settings"), _(plugin->class->name) );
        menu_item = gtk_image_menu_item_new_with_label( tmp );
        g_free( tmp );
        gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
        gtk_menu_shell_prepend(GTK_MENU_SHELL(ret), menu_item);
        if( plugin->class->config )
            g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_config_plugin), plugin );
        else
            gtk_widget_set_sensitive( menu_item, FALSE );
    }

    gtk_widget_show_all(menu);

    g_signal_connect( ret, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL );
    return ret;
}



/****************************************************
 *         panel creation                           *
 ****************************************************/
static void
make_round_corners(Panel *p)
{
    /* FIXME: This should be re-written with shape extension of X11 */
    /* gdk_window_shape_combine_mask() can be used */
}

void panel_set_dock_type(Panel *p)
{
    if (p->setdocktype) {
        Atom state = a_NET_WM_WINDOW_TYPE_DOCK;
        XChangeProperty(GDK_DISPLAY(), p->topxwin,
                        a_NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *) &state, 1);
    }
    else {
        XDeleteProperty( GDK_DISPLAY(), p->topxwin, a_NET_WM_WINDOW_TYPE );
    }
}

static void
panel_start_gui(Panel *p)
{
    Atom state[3];
    XWMHints wmhints;
    guint32 val;

    ENTER;

    // main toplevel window
    p->topgwin =  gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_set_border_width(GTK_CONTAINER(p->topgwin), 0);
    gtk_window_set_resizable(GTK_WINDOW(p->topgwin), FALSE);
    gtk_window_set_wmclass(GTK_WINDOW(p->topgwin), "panel", "lxpanel");
    gtk_window_set_title(GTK_WINDOW(p->topgwin), "panel");
    gtk_window_set_position(GTK_WINDOW(p->topgwin), GTK_WIN_POS_NONE);
    gtk_window_set_decorated(GTK_WINDOW(p->topgwin), FALSE);

    g_signal_connect(G_OBJECT(p->topgwin), "delete-event",
          G_CALLBACK(panel_delete_event), p);
    g_signal_connect(G_OBJECT(p->topgwin), "destroy-event",
          G_CALLBACK(panel_destroy_event), p);
    g_signal_connect (G_OBJECT (p->topgwin), "size-request",
          (GCallback) panel_size_req, p);
    g_signal_connect (G_OBJECT (p->topgwin), "size-allocate",
          (GCallback) panel_size_alloc, p);
    g_signal_connect (G_OBJECT (p->topgwin), "configure-event",
          (GCallback) panel_configure_event, p);

    gtk_widget_add_events( p->topgwin, GDK_BUTTON_PRESS_MASK );
    g_signal_connect(G_OBJECT (p->topgwin), "button_press_event",
          (GCallback) panel_press_button_event, p);

    g_signal_connect (G_OBJECT (p->topgwin), "realize",
          (GCallback) panel_realize, p);

    g_signal_connect (G_OBJECT (p->topgwin), "style-set",
          (GCallback)panel_style_set, p);
    gtk_widget_realize(p->topgwin);
    //gdk_window_set_decorations(p->topgwin->window, 0);

    // main layout manager as a single child of panel
    p->box = p->my_box_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(p->box), 0);
//    gtk_container_add(GTK_CONTAINER(p->bbox), p->box);
    gtk_container_add(GTK_CONTAINER(p->topgwin), p->box);
    gtk_widget_show(p->box);
    if (p->round_corners)
        make_round_corners(p);

    p->topxwin = GDK_WINDOW_XWINDOW(GTK_WIDGET(p->topgwin)->window);
    DBG("topxwin = %x\n", p->topxwin);

    /* the settings that should be done before window is mapped */
    wmhints.flags = InputHint;
    wmhints.input = 0;
    XSetWMHints (GDK_DISPLAY(), p->topxwin, &wmhints);
#define WIN_HINTS_SKIP_FOCUS      (1<<0)    /* "alt-tab" skips this win */
    val = WIN_HINTS_SKIP_FOCUS;
    XChangeProperty(GDK_DISPLAY(), p->topxwin,
          XInternAtom(GDK_DISPLAY(), "_WIN_HINTS", False), XA_CARDINAL, 32,
          PropModeReplace, (unsigned char *) &val, 1);

    panel_set_dock_type(p);

    /* window mapping point */
    gtk_widget_show_all(p->topgwin);

    /* the settings that should be done after window is mapped */

    /* send it to running wm */
    Xclimsg(p->topxwin, a_NET_WM_DESKTOP, 0xFFFFFFFF, 0, 0, 0, 0);
    /* and assign it ourself just for case when wm is not running */
    val = 0xFFFFFFFF;
    XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_DESKTOP, XA_CARDINAL, 32,
          PropModeReplace, (unsigned char *) &val, 1);

    state[0] = a_NET_WM_STATE_SKIP_PAGER;
    state[1] = a_NET_WM_STATE_SKIP_TASKBAR;
    state[2] = a_NET_WM_STATE_STICKY;
    XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STATE, XA_ATOM,
          32, PropModeReplace, (unsigned char *) state, 3);

    calculate_position(p);
    gdk_window_move_resize(p->topgwin->window, p->ax, p->ay, p->aw, p->ah);
    panel_set_wm_strut(p);

    p->tooltips = gtk_tooltips_new();
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( p->tooltips );
#else
    g_object_ref( p->tooltips );
    gtk_object_sink( p->tooltips );
#endif

    RET();
}

void panel_set_orientation(Panel *p)
{
    GList* l;
    p->orientation = (p->edge == EDGE_TOP || p->edge == EDGE_BOTTOM)
        ? ORIENT_HORIZ : ORIENT_VERT;
    if (p->orientation == ORIENT_HORIZ) {
        p->my_box_new = gtk_hbox_new;
        p->my_separator_new = gtk_vseparator_new;
    } else {
        p->my_box_new = gtk_vbox_new;
        p->my_separator_new = gtk_hseparator_new;
    }

    /* recreate the main layout box */
    if( p->box ) {
        GtkBox* newbox = GTK_BOX(recreate_box( GTK_BOX(p->box), p->orientation ));
        if( GTK_WIDGET(newbox) != p->box ) {
            p->box = GTK_WIDGET(newbox);
            gtk_container_add( GTK_CONTAINER(p->topgwin), GTK_WIDGET(newbox) );
        }
    }
    /* NOTE: This loop won't be executed when panel started since
       plugins are not loaded at that time.
       This is used when the orientation of the panel is changed
       from the config dialog, and plugins should be re-layout.
    */
    for( l = p->plugins; l; l = l->next ) {
        Plugin* pl = (Plugin*)l->data;
        if( pl->class->orientation ) {
            pl->class->orientation( pl );
        }
    }
}

static int
panel_parse_global(Panel *p, char **fp)
{
    line s;
    s.len = 256;

    if( G_LIKELY( fp ) )
    {
        while (lxpanel_get_line(fp, &s) != LINE_NONE) {
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "edge")) {
                    p->edge = str2num(edge_pair, s.t[1], EDGE_NONE);
                } else if (!g_ascii_strcasecmp(s.t[0], "allign")) {
                    p->allign = str2num(allign_pair, s.t[1], ALLIGN_NONE);
                } else if (!g_ascii_strcasecmp(s.t[0], "margin")) {
                    p->margin = atoi(s.t[1]);
                } else if (!g_ascii_strcasecmp(s.t[0], "widthtype")) {
                    p->widthtype = str2num(width_pair, s.t[1], WIDTH_NONE);
                } else if (!g_ascii_strcasecmp(s.t[0], "width")) {
                    p->width = atoi(s.t[1]);
                } else if (!g_ascii_strcasecmp(s.t[0], "heighttype")) {
                    p->heighttype = str2num(height_pair, s.t[1], HEIGHT_NONE);
                } else if (!g_ascii_strcasecmp(s.t[0], "height")) {
                    p->height = atoi(s.t[1]);
                } else if (!g_ascii_strcasecmp(s.t[0], "spacing")) {
                    p->spacing = atoi(s.t[1]);
                } else if (!g_ascii_strcasecmp(s.t[0], "SetDockType")) {
                    p->setdocktype = str2num(bool_pair, s.t[1], 0);
                } else if (!g_ascii_strcasecmp(s.t[0], "SetPartialStrut")) {
                    p->setstrut = str2num(bool_pair, s.t[1], 0);
                } else if (!g_ascii_strcasecmp(s.t[0], "RoundCorners")) {
                    p->round_corners = str2num(bool_pair, s.t[1], 0);
                } else if (!g_ascii_strcasecmp(s.t[0], "Transparent")) {
                    p->transparent = str2num(bool_pair, s.t[1], 0);
                } else if (!g_ascii_strcasecmp(s.t[0], "Alpha")) {
                    p->alpha = atoi(s.t[1]);
                    if (p->alpha > 255)
                        p->alpha = 255;
                } else if (!g_ascii_strcasecmp(s.t[0], "TintColor")) {
                    if (!gdk_color_parse (s.t[1], &p->gtintcolor))
                        gdk_color_parse ("white", &p->gtintcolor);
                    p->tintcolor = gcolor2rgb24(&p->gtintcolor);
                    DBG("tintcolor=%x\n", p->tintcolor);
                } else if (!g_ascii_strcasecmp(s.t[0], "useFontColor")) {
                    p->usefontcolor = str2num(bool_pair, s.t[1], 0);
                } else if (!g_ascii_strcasecmp(s.t[0], "FontColor")) {
                    if (!gdk_color_parse (s.t[1], &p->gfontcolor))
                        gdk_color_parse ("black", &p->gfontcolor);
                    p->fontcolor = gcolor2rgb24(&p->gfontcolor);
                    DBG("fontcolor=%x\n", p->fontcolor);
                } else if (!g_ascii_strcasecmp(s.t[0], "Background")) {
                    p->background = str2num(bool_pair, s.t[1], 0);
                } else if( !g_ascii_strcasecmp(s.t[0], "BackgroundFile") ) {
                    p->background_file = g_strdup( s.t[1] );
                } else {
                    ERR( "lxpanel: %s - unknown var in Global section\n", s.t[0]);
                    RET(0);
                }
            } else if (s.type == LINE_BLOCK_END) {
                break;
            } else {
                ERR( "lxpanel: illegal in this context %s\n", s.str);
                RET(0);
            }
        }
    }
    panel_set_orientation( p );

    if (p->width < 0)
        p->width = 100;
    if (p->widthtype == WIDTH_PERCENT && p->width > 100)
        p->width = 100;
    p->heighttype = HEIGHT_PIXEL;
    if (p->heighttype == HEIGHT_PIXEL) {
        if (p->height < PANEL_HEIGHT_MIN)
            p->height = PANEL_HEIGHT_MIN;
        else if (p->height > PANEL_HEIGHT_MAX)
            p->height = PANEL_HEIGHT_MAX;
    }

    if (p->background)
        p->transparent = 0;

    p->curdesk = get_net_current_desktop();
    p->desknum = get_net_number_of_desktops();
    p->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->wa_len);
    print_wmdata(p);

    panel_start_gui(p);
    RET(1);
}

static int
panel_parse_plugin(Panel *p, char **fp)
{
    line s;
    Plugin *plug = NULL;
    gchar *type = NULL;
    int expand , padding, border;
    char* pconfig = NULL;

    ENTER;
    s.len = 256;
    border = expand = padding = 0;
    while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_NONE) {
            ERR( "lxpanel: bad line %s\n", s.str);
            goto error;
        }
        if (s.type == LINE_VAR) {
            if (!g_ascii_strcasecmp(s.t[0], "type")) {
                type = g_strdup(s.t[1]);
                DBG("plug %s\n", type);
            } else if (!g_ascii_strcasecmp(s.t[0], "expand"))
                expand = str2num(bool_pair,  s.t[1], 0);
            else if (!g_ascii_strcasecmp(s.t[0], "padding"))
                padding = atoi(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "border"))
                border = atoi(s.t[1]);
            else {
                ERR( "lxpanel: unknown var %s\n", s.t[0]);
                goto error;
            }
        } else if (s.type == LINE_BLOCK_START) {
            if (!g_ascii_strcasecmp(s.t[0], "Config")) {
                pconfig = *fp;
                int pno = 1;
                while (pno) {
                    get_line_as_is(fp, &s);
                    if (s.type == LINE_NONE) {
                        ERR( "lxpanel: unexpected eof\n");
                        goto error;
                    } else if (s.type == LINE_BLOCK_START) {
                        pno++;
                    } else if (s.type == LINE_BLOCK_END) {
                        pno--;
                    }
                }
            } else {
                ERR( "lxpanel: unknown block %s\n", s.t[0]);
                goto error;
            }
        } else {
            ERR( "lxpanel: illegal in this context %s\n", s.str);
            goto error;
        }
    }

    if (!type || !(plug = plugin_load(type))) {
        ERR( "lxpanel: can't load %s plugin\n", type);
        goto error;
    }

    plug->panel = p;
    plug->expand = expand;
    plug->padding = padding;
    plug->border = border;
    DBG("starting\n");
    if (!plugin_start(plug, pconfig ? &pconfig : NULL)) {
        ERR( "lxpanel: can't start plugin %s\n", type);
        goto error;
    }
    DBG("plug %s\n", type);
    p->plugins = g_list_append(p->plugins, plug);

    g_free( type );
    RET(1);

 error:
    g_free(type);
    if (plug)
          plugin_put(plug);
    RET(0);
}

int panel_start( Panel *p, char **fp )
{
    line s;

    /* parse global section */
    ENTER;
    s.len = 256;

    p->allign = ALLIGN_CENTER;
    p->edge = EDGE_BOTTOM;
    p->widthtype = WIDTH_PERCENT;
    p->width = 100;
    p->heighttype = HEIGHT_PIXEL;
    p->height = PANEL_HEIGHT_DEFAULT;
    p->setdocktype = 1;
    p->setstrut = 1;
    p->round_corners = 0;
    p->transparent = 0;
    p->alpha = 127;
    p->tintcolor = 0xFFFFFFFF;
    p->usefontcolor = 0;
    p->fontcolor = 0x00000000;
    p->spacing = 0;

    if ((lxpanel_get_line(fp, &s) != LINE_BLOCK_START) || g_ascii_strcasecmp(s.t[0], "Global")) {
        ERR( "lxpanel: config file must start from Global section\n");
        RET(0);
    }
    if (!panel_parse_global(p, fp))
        RET(0);

    while (lxpanel_get_line(fp, &s) != LINE_NONE) {
        if ((s.type  != LINE_BLOCK_START) || g_ascii_strcasecmp(s.t[0], "Plugin")) {
            ERR( "lxpanel: expecting Plugin section\n");
            RET(0);
        }
        panel_parse_plugin(p, fp);
    }
    gtk_widget_show_all(p->topgwin);

    /* update backgrond of panel and all plugins */
    panel_update_background( p );

    print_wmdata(p);
    RET(1);
}

static void
delete_plugin(gpointer data, gpointer udata)
{
    ENTER;
    plugin_stop((Plugin *)data);
    plugin_put((Plugin *)data);
    RET();
}

void panel_destroy(Panel *p)
{
    ENTER;

    g_list_foreach(p->plugins, delete_plugin, NULL);
    g_list_free(p->plugins);
    p->plugins = NULL;

    if( p->system_menus ){
        do{
        } while ( g_source_remove_by_user_data( p->system_menus ) );
    }

    if( p->tooltips )
        g_object_unref( p->tooltips );

    if( p->topgwin )
        gtk_widget_destroy(p->topgwin);
    g_free(p->workarea);
    g_free( p->background_file );
    g_slist_free( p->system_menus );
    gdk_flush();
    XFlush(GDK_DISPLAY());
    XSync(GDK_DISPLAY(), True);

    g_free( p->name );
    g_slice_free( Panel, p );
    RET();
}

Panel* panel_new( const char* config_file, const char* config_name )
{
    char *fp, *pfp; /* point to current position of profile data in memory */
    Panel* panel = NULL;
    char* ret;
    if( G_LIKELY(config_file) )
    {
        g_file_get_contents( config_file, &fp, NULL, NULL );
        if( fp )
        {
            panel = g_slice_new0( Panel );
            panel->name = g_strdup( config_name );
            pfp = fp;

            if (! panel_start( panel, &pfp )) {
                ERR( "lxpanel: can't start panel\n");
                panel_destroy( panel );
                panel = NULL;
            }

            g_free( fp );
        }
    }
    return panel;
}

static void
usage()
{
    g_print(_("lxpanel %s - lightweight GTK2+ panel for UNIX desktops\n"), version);
    g_print(_("Command line options:\n"));
    g_print(_(" --help      -- print this help and exit\n"));
    g_print(_(" --version   -- print version and exit\n"));
    g_print(_(" --log <number> -- set log level 0-5. 0 - none 5 - chatty\n"));
    g_print(_(" --configure -- launch configuration utility\n"));
    g_print(_(" --profile name -- use specified profile\n"));
    g_print("\n");
    g_print(_(" -h  -- same as --help\n"));
    g_print(_(" -p  -- same as --profile\n"));
    g_print(_(" -v  -- same as --version\n"));
    g_print(_(" -C  -- same as --configure\n"));
    g_print(_("\nVisit http://lxde.org/ for detail.\n\n"));
}

static void
handle_error(Display * d, XErrorEvent * ev)
{
    char buf[256];

    ENTER;
    if (log_level >= LOG_WARN) {
        XGetErrorText(GDK_DISPLAY(), ev->error_code, buf, 256);
        LOG(LOG_WARN, "lxpanel : X error: %s\n", buf);
    }
    RET();
}

/* Lightweight lock related functions - X clipboard hacks */

#define CLIPBOARD_NAME "LXPANEL_SELECTION"

/*
 * clipboard_get_func - dummy get_func for gtk_clipboard_set_with_data ()
 */
static void
clipboard_get_func(
    GtkClipboard *clipboard G_GNUC_UNUSED,
    GtkSelectionData *selection_data G_GNUC_UNUSED,
    guint info G_GNUC_UNUSED,
    gpointer user_data_or_owner G_GNUC_UNUSED)
{
}

/*
 * clipboard_clear_func - dummy clear_func for gtk_clipboard_set_with_data ()
 */
static void clipboard_clear_func(
    GtkClipboard *clipboard G_GNUC_UNUSED,
    gpointer user_data_or_owner G_GNUC_UNUSED)
{
}

/*
 * Lightweight version for checking single instance.
 * Try and get the CLIPBOARD_NAME clipboard instead of using file manipulation.
 *
 * Returns TRUE if successfully retrieved and FALSE otherwise.
 */
static gboolean check_main_lock()
{
    static const GtkTargetEntry targets[] = { { CLIPBOARD_NAME, 0, 0 } };
    gboolean retval = FALSE;
    GtkClipboard *clipboard;
    Atom atom;

    atom = gdk_x11_get_xatom_by_name(CLIPBOARD_NAME);

    XGrabServer(GDK_DISPLAY());

    if (XGetSelectionOwner(GDK_DISPLAY(), atom) != None)
        goto out;

    clipboard = gtk_clipboard_get(gdk_atom_intern(CLIPBOARD_NAME, FALSE));

    if (gtk_clipboard_set_with_data(clipboard, targets,
                                    G_N_ELEMENTS (targets),
                                    clipboard_get_func,
                                    clipboard_clear_func, NULL))
        retval = TRUE;

out:
    XUngrabServer (GDK_DISPLAY ());
    gdk_flush ();

    return retval;
}
#undef CLIPBOARD_NAME

static gboolean start_all_panels( )
{
    gboolean is_global;
    for( is_global = 0; ! all_panels && is_global < 2; ++is_global )
    {
        char* panel_dir = get_config_file( cprofile, "panels", is_global );
        GDir* dir = g_dir_open( panel_dir, 0, NULL );
        char* name;

        if( ! dir )
        {
            g_free( panel_dir );
            continue;
        }

        while( name = g_dir_read_name( dir ) )
        {
            char* panel_config = g_build_filename( panel_dir, name, NULL );
            Panel* panel = panel_new( panel_config, name );
            if( panel )
                all_panels = g_slist_prepend( all_panels, panel );
            g_free( panel_config );
        }
        g_dir_close( dir );
        g_free( panel_dir );
    }
    return all_panels != NULL;
}

void load_global_config();
void free_global_config();

int main(int argc, char *argv[], char *env[])
{
    int i;

    setlocale(LC_CTYPE, "");

    gtk_init(&argc, &argv);

#ifdef ENABLE_NLS
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    XSetLocaleModifiers("");
    XSetErrorHandler((XErrorHandler) handle_error);

    resolve_atoms();

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage();
            exit(0);
        } else if (!strcmp(argv[i], "-v") || !strcmp(argv[i], "--version")) {
            printf("lxpanel %s\n", version);
            exit(0);
        } else if (!strcmp(argv[i], "--log")) {
            i++;
            if (i == argc) {
                ERR( "lxpanel: missing log level\n");
                usage();
                exit(1);
            } else {
                log_level = atoi(argv[i]);
            }
        } else if (!strcmp(argv[i], "--configure") || !strcmp(argv[i], "-C")) {
            config = 1;
        } else if (!strcmp(argv[i], "--profile") || !strcmp(argv[i], "-p")) {
            i++;
            if (i == argc) {
                ERR( "lxpanel: missing profile name\n");
                usage();
                exit(1);
            } else {
                cprofile = g_strdup(argv[i]);
            }
        } else {
            printf("lxpanel: unknown option - %s\n", argv[i]);
            usage();
            exit(1);
        }
    }

    /* Check for duplicated lxpanel instances */
    if (!check_main_lock() && !config) {
        printf("There is alreay an instance of LXPanel. Now to exit\n");
        exit(1);
    }

    /* Add our own icons to the search path of icon theme */
    gtk_icon_theme_append_search_path( gtk_icon_theme_get_default(),
                                       PACKAGE_DATA_DIR "/lxpanel/images" );

    fbev = fb_ev_new();

restart:
    is_restarting = FALSE;

    load_global_config();

    XSelectInput (GDK_DISPLAY(), GDK_ROOT_WINDOW(), SubstructureNotifyMask|PropertyChangeMask);
    gdk_window_add_filter(gdk_get_default_root_window (), (GdkFilterFunc)panel_event_filter, NULL);

    if( G_UNLIKELY( ! start_all_panels() ) )
        g_warning( "Config files are not found.\n" );
/*
 * FIXME: configure??
    if (config)
        configure();
*/
    gtk_main();

    XSelectInput (GDK_DISPLAY(), GDK_ROOT_WINDOW(), NoEventMask);
    gdk_window_remove_filter(gdk_get_default_root_window (), (GdkFilterFunc)panel_event_filter, NULL);

    /* destroy all panels */
    g_slist_foreach( all_panels, (GFunc) panel_destroy, NULL );
    g_slist_free( all_panels );
    all_panels = NULL;
    g_free( cfgfile );

    free_global_config();

    if( is_restarting )
        goto restart;

    g_object_unref(fbev);

    return 0;
}
