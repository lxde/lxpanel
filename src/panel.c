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
#include <glib/gstdio.h>
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

#include "lxpanelctl.h"
#include "dbg.h"

static gchar *cfgfile = NULL;
static gchar version[] = VERSION;
gchar *cprofile = "default";

static GtkWindowGroup* win_grp; /* window group used to limit the scope of model dialog. */

static int config = 0;
FbEv *fbev = NULL;

GSList* all_panels = NULL;  /* a single-linked list storing all panels */

gboolean is_restarting = FALSE;

static int panel_start( Panel *p, char **fp );
static void panel_start_gui(Panel *p);
void panel_config_save(Panel* panel);   /* defined in configurator.c */

gboolean is_in_lxde = FALSE;

/* A hack used to be compatible with Gnome panel for gtk+ themes.
 * Some gtk+ themes define special styles for desktop panels.
 * http://live.gnome.org/GnomeArt/Tutorials/GtkThemes/GnomePanel
 * So we make a derived class from GtkWindow named PanelToplevel
 * and create the panels with it to be compatible with Gnome themes.
 */
#define PANEL_TOPLEVEL_TYPE				(panel_toplevel_get_type())

typedef struct _PanelToplevel			PanelToplevel;
typedef struct _PanelToplevelClass		PanelToplevelClass;
struct _PanelToplevel
{
	GtkWindow parent;
};
struct _PanelToplevelClass
{
	GtkWindowClass parent_class;
};
G_DEFINE_TYPE(PanelToplevel, panel_toplevel, GTK_TYPE_WINDOW);
static void panel_toplevel_class_init(PanelToplevelClass *klass)
{
}
static void panel_toplevel_init(PanelToplevel *self)
{
}

/* Allocate and initialize new Panel structure. */
static Panel* panel_allocate(void)
{
    Panel* p = g_new0(Panel, 1);
    p->allign = ALLIGN_CENTER;
    p->edge = EDGE_NONE;
    p->widthtype = WIDTH_PERCENT;
    p->width = 100;
    p->heighttype = HEIGHT_PIXEL;
    p->height = PANEL_HEIGHT_DEFAULT;
    p->setdocktype = 1;
    p->setstrut = 1;
    p->round_corners = 0;
    p->autohide = 0;
    p->visible = TRUE;
    p->height_when_hidden = 2;
    p->transparent = 0;
    p->alpha = 255;
    gdk_color_parse("white", &p->gtintcolor);
    p->tintcolor = gcolor2rgb24(&p->gtintcolor);
    p->usefontcolor = 0;
    p->fontcolor = 0x00000000;
    p->spacing = 0;
    p->icon_size = PANEL_ICON_SIZE;
    return p;
}

/* Normalize panel configuration after load from file or reconfiguration. */
static void panel_normalize_configuration(Panel* p)
{
    panel_set_panel_configuration_changed( p );
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
}

/****************************************************
 *         panel's handlers for WM events           *
 ****************************************************/

void panel_set_wm_strut(Panel *p)
{
    int index;
    gulong strut_size;
    gulong strut_lower;
    gulong strut_upper;

    /* Dispatch on edge to set up strut parameters. */
    switch (p->edge)
    {
        case EDGE_LEFT:
            index = 0;
            strut_size = p->aw;
            strut_lower = p->ay;
            strut_upper = p->ay + p->ah;
            break;
        case EDGE_RIGHT:
            index = 1;
            strut_size = p->aw;
            strut_lower = p->ay;
            strut_upper = p->ay + p->ah;
            break;
        case EDGE_TOP:
            index = 2;
            strut_size = p->ah;
            strut_lower = p->ax;
            strut_upper = p->ax + p->aw;
            break;
        case EDGE_BOTTOM:
            index = 3;
            strut_size = p->ah;
            strut_lower = p->ax;
            strut_upper = p->ax + p->aw;
            break;
        default:
            return;
    }

    /* Handle autohide case.  EWMH recommends having the strut be the minimized size. */
    if (p->autohide)
        strut_size = p->height_when_hidden;

    /* Set up strut value in property format. */
    gulong desired_strut[12];
    memset(desired_strut, 0, sizeof(desired_strut));
    if (p->setstrut)
    {
        desired_strut[index] = strut_size;
        desired_strut[4 + index * 2] = strut_lower;
        desired_strut[5 + index * 2] = strut_upper;
    }
    else
    {
        strut_size = 0;
        strut_lower = 0;
        strut_upper = 0;
    }

    /* If strut value changed, set the property value on the panel window.
     * This avoids property change traffic when the panel layout is recalculated but strut geometry hasn't changed. */
    if ((GTK_WIDGET_MAPPED(p->topgwin))
    && ((p->strut_size != strut_size) || (p->strut_lower != strut_lower) || (p->strut_upper != strut_upper) || (p->strut_edge != p->edge)))
    {
        p->strut_size = strut_size;
        p->strut_lower = strut_lower;
        p->strut_upper = strut_upper;
        p->strut_edge = p->edge;

        /* If window manager supports STRUT_PARTIAL, it will ignore STRUT.
         * Set STRUT also for window managers that do not support STRUT_PARTIAL. */
        if (strut_size != 0)
        {
            XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT_PARTIAL,
                XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) desired_strut, 12);
            XChangeProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT,
                XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) desired_strut, 4);
        }
        else
        {
            XDeleteProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT);
            XDeleteProperty(GDK_DISPLAY(), p->topxwin, a_NET_WM_STRUT_PARTIAL);
        }
    }
}

/* defined in plugins/menu.c */
gboolean show_system_menu( gpointer system_menu );

/* defined in configurator.c */
void panel_configure(Panel* p, int sel_page );
gboolean panel_edge_available(Panel* p, int edge);

/* built-in commands, defined in configurator.c */
void restart(void);
void gtk_run(void);
void panel_destroy(Panel *p);

static void process_client_msg ( XClientMessageEvent* ev )
{
    int cmd = ev->data.b[0];
    switch( cmd )
    {
#ifndef DISABLE_MENU
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
#endif
#ifndef DISABLE_MENU
        case LXPANEL_CMD_RUN:
            gtk_run();
            break;
#endif
        case LXPANEL_CMD_CONFIG:
            {
            Panel * p = ((all_panels != NULL) ? all_panels->data : NULL);
            if (p != NULL)
                panel_configure(p, 0);
            }
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
    if (ev->type != PropertyNotify )
    {
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
    if (win == GDK_ROOT_WINDOW())
    {
        if (at == a_NET_CLIENT_LIST)
        {
            fb_ev_emit(fbev, EV_CLIENT_LIST);
        }
        else if (at == a_NET_CURRENT_DESKTOP)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((Panel*)l->data)->curdesk = get_net_current_desktop();
            fb_ev_emit(fbev, EV_CURRENT_DESKTOP);
        }
        else if (at == a_NET_NUMBER_OF_DESKTOPS)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((Panel*)l->data)->desknum = get_net_number_of_desktops();
            fb_ev_emit(fbev, EV_NUMBER_OF_DESKTOPS);
        }
        else if (at == a_NET_DESKTOP_NAMES)
        {
            fb_ev_emit(fbev, EV_DESKTOP_NAMES);
        }
        else if (at == a_NET_ACTIVE_WINDOW)
        {
            fb_ev_emit(fbev, EV_ACTIVE_WINDOW );
        }
        else if (at == a_NET_CLIENT_LIST_STACKING)
        {
            fb_ev_emit(fbev, EV_CLIENT_LIST_STACKING);
        }
        else if (at == a_XROOTPMAP_ID)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                Panel* p = (Panel*)l->data;
                if (p->transparent) {
                    fb_bg_notify_changed_bg(p->bg);
                }
            }
        }
        else if (at == a_NET_WORKAREA)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                Panel* p = (Panel*)l->data;
                g_free( p->workarea );
                p->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->wa_len);
                /* print_wmdata(p); */
            }
        }
        else
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

void panel_determine_background_pixmap(Panel * p, GtkWidget * widget, GdkWindow * window)
{
    GdkPixmap * pixmap = NULL;

    /* Free p->bg if it is not going to be used. */
    if (( ! p->transparent) && (p->bg != NULL))
    {
        g_signal_handlers_disconnect_by_func(G_OBJECT(p->bg), on_root_bg_changed, p);
        g_object_unref(p->bg);
        p->bg = NULL;
    }

    if (p->background)
    {
        /* User specified background pixmap. */
        if (p->background_file != NULL)
            pixmap = fb_bg_get_pix_from_file(widget, p->background_file);
    }

    else if (p->transparent)
    {
        /* Transparent.  Determine the appropriate value from the root pixmap. */
        if (p->bg == NULL)
        {
            p->bg = fb_bg_get_for_display();
            g_signal_connect(G_OBJECT(p->bg), "changed", G_CALLBACK(on_root_bg_changed), p);
        }
        pixmap = fb_bg_get_xroot_pix_for_win(p->bg, widget);
        if ((pixmap != NULL) && (pixmap != GDK_NO_BG) && (p->alpha != 0))
            fb_bg_composite(pixmap, widget->style->black_gc, p->tintcolor, p->alpha);
    }

    if (pixmap != NULL)
    {
        gtk_widget_set_app_paintable(widget, TRUE );
        gdk_window_set_back_pixmap(window, pixmap, FALSE);
        g_object_unref(pixmap);
    }
    else
        gtk_widget_set_app_paintable(widget, FALSE);
}

/* Update the background of the entire panel.
 * This function should only be called after the panel has been realized. */
void panel_update_background(Panel * p)
{
    /* Redraw the top level widget. */
    panel_determine_background_pixmap(p, p->topgwin, p->topgwin->window);
    gdk_window_clear(p->topgwin->window);
    gtk_widget_queue_draw(p->topgwin);

    /* Loop over all plugins redrawing each plugin. */
    GList * l;
    for (l = p->plugins; l != NULL; l = l->next)
    {
        Plugin * pl = (Plugin *) l->data;
        if (pl->pwid != NULL)
            plugin_widget_set_background(pl->pwid, p);
    }

}

static gboolean delay_update_background( Panel* p )
{
    /* Panel could be destroyed while background update scheduled */
    if ( p->topgwin && GTK_WIDGET_REALIZED ( p->topgwin ) ) {
	gdk_display_sync( gtk_widget_get_display(p->topgwin) );
	panel_update_background( p );
    }
    
    return FALSE;
}

static void
panel_realize(GtkWidget *widget, Panel *p)
{
    g_idle_add_full( G_PRIORITY_LOW, 
            (GSourceFunc)delay_update_background, p, NULL );
}

static void
panel_style_set(GtkWidget *widget, GtkStyle* prev, Panel *p)
{
    /* FIXME: This dirty hack is used to fix the background of systray... */
    if( GTK_WIDGET_REALIZED( widget ) )
        g_idle_add_full( G_PRIORITY_LOW, 
                (GSourceFunc)delay_update_background, p, NULL );
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

/* Handler for "button_press_event" signal with Panel as parameter. */
static gboolean panel_button_press_event_with_panel(GtkWidget *widget, GdkEventButton *event, Panel *panel)
{
    if (event->button == 3)	 /* right button */
    {
        GtkMenu* popup = (GtkMenu*) lxpanel_get_panel_menu(panel, NULL, FALSE);
        gtk_menu_popup(popup, NULL, NULL, NULL, NULL, event->button, event->time);
        return TRUE;
    }    
    return FALSE;
}

static void panel_popupmenu_config_plugin( GtkMenuItem* item, Plugin* plugin )
{
    plugin->class->config( plugin, GTK_WINDOW(plugin->panel->topgwin) );

    /* FIXME: this should be more elegant */
    plugin->panel->config_changed = TRUE;
}

static void panel_popupmenu_add_item( GtkMenuItem* item, Panel* panel )
{
    /* panel_add_plugin( panel, panel->topgwin ); */
    panel_configure( panel, 2 );
}

static void panel_popupmenu_remove_item( GtkMenuItem* item, Plugin* plugin )
{
    Panel* panel = plugin->panel;

    /* If the configuration dialog is open, there will certainly be a crash if the
     * user manipulates the Configured Plugins list, after we remove this entry.
     * Close the configuration dialog if it is open. */
    if (panel->pref_dialog != NULL)
    {
        gtk_widget_destroy(panel->pref_dialog);
        panel->pref_dialog = NULL;
    }

    panel->plugins = g_list_remove( panel->plugins, plugin );
    plugin_delete(plugin);
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

static void try_allocate_edge(Panel* p, int edge)
{
    if ((p->edge == EDGE_NONE) && (panel_edge_available(p, edge)))
        p->edge = edge;
}

/* FIXME: Potentially we can support multiple panels at the same edge,
 * but currently this cannot be done due to some positioning problems. */
static void panel_popupmenu_create_panel( GtkMenuItem* item, Panel* panel )
{
    Panel* new_panel = panel_allocate();

    /* Allocate the edge. */
    try_allocate_edge(new_panel, EDGE_BOTTOM);
    try_allocate_edge(new_panel, EDGE_TOP);
    try_allocate_edge(new_panel, EDGE_LEFT);
    try_allocate_edge(new_panel, EDGE_RIGHT);
    new_panel->name = gen_panel_name(new_panel->edge);

    panel_configure(new_panel, 0);
    panel_normalize_configuration(new_panel);
    panel_start_gui(new_panel);
    gtk_widget_show_all(new_panel->topgwin);

    panel_config_save(new_panel);
    all_panels = g_slist_prepend(all_panels, new_panel);
}

static void panel_popupmenu_delete_panel( GtkMenuItem* item, Panel* panel )
{
    GtkWidget* dlg;
    gboolean ok;
    dlg = gtk_message_dialog_new_with_markup( GTK_WINDOW(panel->topgwin),
                                                    GTK_DIALOG_MODAL,
                                                    GTK_MESSAGE_QUESTION,
                                                    GTK_BUTTONS_OK_CANCEL,
                                                    _("Really delete this panel?\n<b>Warning: This can not be recovered.</b>") );
    panel_apply_icon(GTK_WINDOW(dlg));
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
        panel->config_changed = 0;
        panel_destroy( panel );
    }
}

static void panel_popupmenu_about( GtkMenuItem* item, Panel* panel )
{
    GtkWidget *about;
    const gchar* authors[] = {
        "Hong Jen Yee (PCMan) <pcman.tw@gmail.com>",
        "Jim Huang <jserv.tw@gmail.com>",
        "Greg McNew <gmcnew@gmail.com> (battery plugin)",
        "Fred Chien <cfsghost@gmail.com>",
        "Daniel Kesler <kesler.daniel@gmail.com>",
        "Juergen Hoetzel <juergen@archlinux.org>",
        "Marty Jack <martyj19@comcast.net>",
        NULL
    };
    /* TRANSLATORS: Replace this string with your names, one name per line. */
    gchar *translators = _( "translator-credits" );

    about = gtk_about_dialog_new();
    panel_apply_icon(GTK_WINDOW(about));
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about), VERSION);
    gtk_about_dialog_set_name(GTK_ABOUT_DIALOG(about), _("LXPanel"));
    gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(about), gdk_pixbuf_new_from_file(PACKAGE_DATA_DIR "/lxpanel/images/my-computer.png", NULL));
    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(about), _("Copyright (C) 2008-2010"));
    gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about), _( "Desktop panel for LXDE project"));
    gtk_about_dialog_set_license(GTK_ABOUT_DIALOG(about), "This program is free software; you can redistribute it and/or\nmodify it under the terms of the GNU General Public License\nas published by the Free Software Foundation; either version 2\nof the License, or (at your option) any later version.\n\nThis program is distributed in the hope that it will be useful,\nbut WITHOUT ANY WARRANTY; without even the implied warranty of\nMERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\nGNU General Public License for more details.\n\nYou should have received a copy of the GNU General Public License\nalong with this program; if not, write to the Free Software\nFoundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.");
    gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(about), "http://lxde.org/");
    gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(about), authors);
    gtk_about_dialog_set_translator_credits(GTK_ABOUT_DIALOG(about), translators);
    gtk_dialog_run(GTK_DIALOG(about));
    gtk_widget_destroy(about); 
}

void panel_apply_icon( GtkWindow *w )
{
    gtk_window_set_icon_from_file(w, PACKAGE_DATA_DIR "/lxpanel/images/my-computer.png", NULL);
}

GtkMenu* lxpanel_get_panel_menu( Panel* panel, Plugin* plugin, gboolean use_sub_menu )
{
    GtkWidget  *menu_item, *img;
    GtkMenu *ret,*menu;
    
    char* tmp;
    ret = menu = GTK_MENU(gtk_menu_new());

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

    menu_item = gtk_separator_menu_item_new();
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);

    img = gtk_image_new_from_stock( GTK_STOCK_ABOUT, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("About"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_about), panel );

    if( use_sub_menu )
    {
        ret = GTK_MENU(gtk_menu_new());
        menu_item = gtk_image_menu_item_new_with_label(_("Panel"));
        gtk_menu_shell_append(GTK_MENU_SHELL(ret), menu_item);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), GTK_WIDGET(menu) );

        gtk_widget_show_all(GTK_WIDGET(ret));
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

    gtk_widget_show_all(GTK_WIDGET(menu));

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

static void panel_set_visibility(Panel *p, gboolean visible)
{
    if ( ! visible) gtk_widget_hide(p->box);
    p->visible = visible;
    calculate_position(p);
    gtk_widget_set_size_request(p->topgwin, p->aw, p->ah);
    gdk_window_move(p->topgwin->window, p->ax, p->ay);
    if (visible) gtk_widget_show(p->box);
    panel_set_wm_strut(p);
}

static gboolean panel_leave_real(Panel *p)
{
    /* If the pointer is grabbed by this application, leave the panel displayed.
     * There is no way to determine if it is grabbed by another application, such as an application that has a systray icon. */
    if (gdk_display_pointer_is_grabbed(p->display))
        return TRUE;

    /* If the pointer is inside the panel, leave the panel displayed. */
    gint x, y;
    gdk_display_get_pointer(p->display, NULL, &x, &y, NULL);
    if ((p->cx <= x) && (x <= (p->cx + p->cw)) && (p->cy <= y) && (y <= (p->cy + p->ch)))
        return TRUE;

    /* If the panel is configured to autohide and if it is visible, hide the panel. */
    if ((p->autohide) && (p->visible))
        panel_set_visibility(p, FALSE);

    /* Clear the timer. */
    p->hide_timeout = 0;
    return FALSE;
}

static gboolean panel_enter(GtkImage *widget, GdkEventCrossing *event, Panel *p)
{
    /* We may receive multiple enter-notify events when the pointer crosses into the panel.
     * Do extra tests to make sure this does not cause misoperation such as blinking.
     * If the pointer is inside the panel, unhide it. */
    gint x, y;
    gdk_display_get_pointer(p->display, NULL, &x, &y, NULL);
    if ((p->cx <= x) && (x <= (p->cx + p->cw)) && (p->cy <= y) && (y <= (p->cy + p->ch)))
    {
        /* If the pointer is inside the panel and we have not already unhidden it, do so and
         * set a timer to recheck it in a half second. */
        if (p->hide_timeout == 0)
        {
            p->hide_timeout = g_timeout_add(500, (GSourceFunc) panel_leave_real, p);
            panel_set_visibility(p, TRUE);
        }
    }
    else
    {
        /* If the pointer is not inside the panel, simulate a timer expiration. */
        panel_leave_real(p);
    }
    return TRUE;
}

static gboolean panel_drag_motion(GtkWidget *widget, GdkDragContext *drag_context, gint x,
      gint y, guint time, Panel *p)
{
    panel_enter(NULL, NULL, p);
    return TRUE;
}

void panel_establish_autohide(Panel *p)
{
    if (p->autohide)
    {
        gtk_widget_add_events(p->topgwin, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(G_OBJECT(p->topgwin), "enter-notify-event", G_CALLBACK(panel_enter), p);
        g_signal_connect(G_OBJECT(p->topgwin), "drag-motion", (GCallback) panel_drag_motion, p);
        gtk_drag_dest_set(p->topgwin, GTK_DEST_DEFAULT_MOTION, NULL, 0, 0);
        gtk_drag_dest_set_track_motion(p->topgwin, TRUE);
        panel_enter(NULL, NULL, p);
    }
    else if ( ! p->visible)
    {
	gtk_widget_show(p->box);
        p->visible = TRUE;
    }
}

/* Set an image from a file with scaling to the panel icon size. */
void panel_image_set_from_file(Panel * p, GtkWidget * image, char * file)
{
    GdkPixbuf * pixbuf = gdk_pixbuf_new_from_file_at_scale(file, p->icon_size, p->icon_size, TRUE, NULL);
    if (pixbuf != NULL)
    {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
        g_object_unref(pixbuf);
    }
}

/* Set an image from a icon theme with scaling to the panel icon size. */
gboolean panel_image_set_icon_theme(Panel * p, GtkWidget * image, const gchar * icon)
{
    if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), icon))
    {
        GdkPixbuf * pixbuf = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), icon, p->icon_size, 0, NULL);
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
        g_object_unref(pixbuf);
        return TRUE;
    }
    return FALSE;
}

static void
panel_start_gui(Panel *p)
{
    Atom state[3];
    XWMHints wmhints;
    guint32 val;

    ENTER;

    p->curdesk = get_net_current_desktop();
    p->desknum = get_net_number_of_desktops();
    p->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->wa_len);

    /* main toplevel window */
    /* p->topgwin =  gtk_window_new(GTK_WINDOW_TOPLEVEL); */
    p->topgwin = (GtkWidget*)g_object_new(PANEL_TOPLEVEL_TYPE, NULL);
    gtk_widget_set_name(p->topgwin, "PanelToplevel");
    p->display = gdk_display_get_default();
    gtk_container_set_border_width(GTK_CONTAINER(p->topgwin), 0);
    gtk_window_set_resizable(GTK_WINDOW(p->topgwin), FALSE);
    gtk_window_set_wmclass(GTK_WINDOW(p->topgwin), "panel", "lxpanel");
    gtk_window_set_title(GTK_WINDOW(p->topgwin), "panel");
    gtk_window_set_position(GTK_WINDOW(p->topgwin), GTK_WIN_POS_NONE);
    gtk_window_set_decorated(GTK_WINDOW(p->topgwin), FALSE);

    gtk_window_group_add_window( win_grp, (GtkWindow*)p->topgwin );

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
          (GCallback) panel_button_press_event_with_panel, p);

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
    panel_establish_autohide(p);

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

    RET();
}

/* Exchange the "width" and "height" terminology for vertical and horizontal panels. */
void panel_adjust_geometry_terminology(Panel * p)
{
    if ((p->height_label != NULL) && (p->width_label != NULL)
    && (p->alignment_left_label != NULL) && (p->alignment_right_label != NULL))
    {
        if ((p->edge == EDGE_TOP) || (p->edge == EDGE_BOTTOM))
        {
            gtk_label_set_text(GTK_LABEL(p->height_label), _("Height:"));
            gtk_label_set_text(GTK_LABEL(p->width_label), _("Width:"));
            gtk_button_set_label(GTK_BUTTON(p->alignment_left_label), _("Left"));
            gtk_button_set_label(GTK_BUTTON(p->alignment_right_label), _("Right"));
        }
        else
        {
            gtk_label_set_text(GTK_LABEL(p->height_label), _("Width:"));
            gtk_label_set_text(GTK_LABEL(p->width_label), _("Height:"));
            gtk_button_set_label(GTK_BUTTON(p->alignment_left_label), _("Top"));
            gtk_button_set_label(GTK_BUTTON(p->alignment_right_label), _("Bottom"));
        }
    }
}

/* Draw text into a label, with the user preference color and optionally bold. */
void panel_draw_label_text(Panel * p, GtkWidget * label, char * text, gboolean bold, gboolean custom_color)
{
    if (text == NULL)
    {
        /* Null string. */
        gtk_label_set_text(GTK_LABEL(label), NULL);
    }

    else
    {
        /* Compute an appropriate size so the font will scale with the panel's icon size. */
        int font_desc;
        if (p->icon_size < 20) 
            font_desc = 9;
        else if (p->icon_size >= 20 && p->icon_size < 36)
            font_desc = 10;
        else
            font_desc = 12;

        /* Check the string for characters that need to be escaped.
         * If any are found, create the properly escaped string and use it instead. */
        char * valid_markup = text;
        char * escaped_text = NULL;
        char * q;
        for (q = text; *q != '\0'; q += 1)
        {
            if ((*q == '<') || (*q == '>') || (*q == '&'))
            {
                escaped_text = g_markup_escape_text(text, -1);
                valid_markup = escaped_text;
                break;
            }
        }

        if ((custom_color) && (p->usefontcolor))
        {
            /* Color, optionally bold. */
            gchar * text = g_strdup_printf("<span font_desc=\"%d\" color=\"#%06x\">%s%s%s</span>",
                font_desc,
                gcolor2rgb24(&p->gfontcolor),
                ((bold) ? "<b>" : ""),
                valid_markup,
                ((bold) ? "</b>" : ""));
            gtk_label_set_markup(GTK_LABEL(label), text);
            g_free(text);
        }
        else
        {
            /* No color, optionally bold. */
            gchar * text = g_strdup_printf("<span font_desc=\"%d\">%s%s%s</span>",
                font_desc,
                ((bold) ? "<b>" : ""),
                valid_markup,
                ((bold) ? "</b>" : ""));
            gtk_label_set_markup(GTK_LABEL(label), text);
            g_free(text);
        }
        g_free(escaped_text);
    }
}

void panel_set_panel_configuration_changed(Panel *p)
{
    GList* l;

    int previous_orientation = p->orientation;
    p->orientation = (p->edge == EDGE_TOP || p->edge == EDGE_BOTTOM)
        ? ORIENT_HORIZ : ORIENT_VERT;

    if (previous_orientation != p->orientation)
    {
        panel_adjust_geometry_terminology(p);
        if (previous_orientation != ORIENT_NONE)
            p->height = ((p->orientation == ORIENT_HORIZ) ? PANEL_HEIGHT_DEFAULT : PANEL_WIDTH_DEFAULT);
        if (p->height_control != NULL)
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->height_control), p->height);
        if ((p->widthtype == WIDTH_PIXEL) && (p->width_control != NULL))
        {
            int value = ((p->orientation == ORIENT_HORIZ) ? gdk_screen_width() : gdk_screen_height());
            gtk_spin_button_set_range(GTK_SPIN_BUTTON(p->width_control), 0, value);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->width_control), value);
        }

    }

    if (p->orientation == ORIENT_HORIZ) {
        p->my_box_new = gtk_hbox_new;
        p->my_separator_new = gtk_vseparator_new;
    } else {
        p->my_box_new = gtk_vbox_new;
        p->my_separator_new = gtk_hseparator_new;
    }

    /* recreate the main layout box */
    if (p->box != NULL)
    {
#if GTK_CHECK_VERSION(2,16,0)
        GtkOrientation bo = (p->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
        gtk_orientable_set_orientation(GTK_ORIENTABLE(p->box), bo);
#else
        GtkBox * newbox = GTK_BOX(recreate_box(GTK_BOX(p->box), p->orientation));
        if (GTK_WIDGET(newbox) != p->box)
        {
            p->box = GTK_WIDGET(newbox);
            gtk_container_add(GTK_CONTAINER(p->topgwin), GTK_WIDGET(newbox));
        }
#endif
    }

    /* NOTE: This loop won't be executed when panel started since
       plugins are not loaded at that time.
       This is used when the orientation of the panel is changed
       from the config dialog, and plugins should be re-layout.
    */
    for( l = p->plugins; l; l = l->next ) {
        Plugin* pl = (Plugin*)l->data;
        if( pl->class->panel_configuration_changed ) {
            pl->class->panel_configuration_changed( pl );
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
                } else if (!g_ascii_strcasecmp(s.t[0], "AutoHide")) {
                    p->autohide = str2num(bool_pair, s.t[1], 0);
                } else if (!g_ascii_strcasecmp(s.t[0], "HeightWhenHidden")) {
                    p->height_when_hidden = atoi(s.t[1]);
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
                } else if (!g_ascii_strcasecmp(s.t[0], "IconSize")) {
                    p->icon_size = atoi(s.t[1]);
                } else {
                    ERR( "lxpanel: %s - unknown var in Global section\n", s.t[0]);
                }
            } else if (s.type == LINE_BLOCK_END) {
                break;
            } else {
                ERR( "lxpanel: illegal in this context %s\n", s.str);
                RET(0);
            }
        }
    }

    panel_normalize_configuration(p);

    return 1;
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
    if (plug->class->expand_available) plug->expand = expand;
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
    if (plug != NULL)
        plugin_unload(plug);
    g_free(type);
    RET(0);
}

int panel_start( Panel *p, char **fp )
{
    line s;

    /* parse global section */
    ENTER;
    s.len = 256;

    if ((lxpanel_get_line(fp, &s) != LINE_BLOCK_START) || g_ascii_strcasecmp(s.t[0], "Global")) {
        ERR( "lxpanel: config file must start from Global section\n");
        RET(0);
    }
    if (!panel_parse_global(p, fp))
        RET(0);

    panel_start_gui(p);

    while (lxpanel_get_line(fp, &s) != LINE_NONE) {
        if ((s.type  != LINE_BLOCK_START) || g_ascii_strcasecmp(s.t[0], "Plugin")) {
            ERR( "lxpanel: expecting Plugin section\n");
            RET(0);
        }
        panel_parse_plugin(p, fp);
    }

    /* update backgrond of panel and all plugins */
    panel_update_background( p );
    return 1;
}

static void
delete_plugin(gpointer data, gpointer udata)
{
    plugin_delete((Plugin *)data);
}

void panel_destroy(Panel *p)
{
    ENTER;

    if (p->pref_dialog != NULL)
        gtk_widget_destroy(p->pref_dialog);
    if (p->plugin_pref_dialog != NULL)
    {
        gtk_widget_destroy(p->plugin_pref_dialog);
        p->plugin_pref_dialog = NULL;
    }

    if (p->bg != NULL)
    {
        g_signal_handlers_disconnect_by_func(G_OBJECT(p->bg), on_root_bg_changed, p);
        g_object_unref(p->bg);
    }

    if( p->config_changed )
        panel_config_save( p );

    g_list_foreach(p->plugins, delete_plugin, NULL);
    g_list_free(p->plugins);
    p->plugins = NULL;

    if( p->system_menus ){
        do{
        } while ( g_source_remove_by_user_data( p->system_menus ) );
    }

    gtk_window_group_remove_window( win_grp, GTK_WINDOW(  p->topgwin ) );

    if( p->topgwin )
        gtk_widget_destroy(p->topgwin);
    g_free(p->workarea);
    g_free( p->background_file );
    g_slist_free( p->system_menus );
    gdk_flush();
    XFlush(GDK_DISPLAY());
    XSync(GDK_DISPLAY(), True);

    g_free( p->name );
    g_free(p);
    RET();
}

Panel* panel_new( const char* config_file, const char* config_name )
{
    char *fp, *pfp; /* point to current position of profile data in memory */
    Panel* panel = NULL;

    if( G_LIKELY(config_file) )
    {
        g_file_get_contents( config_file, &fp, NULL, NULL );
        if( fp )
        {
            panel = panel_allocate();
            panel->orientation = ORIENT_NONE;
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
//    g_print(_(" --configure -- launch configuration utility\n"));
    g_print(_(" --profile name -- use specified profile\n"));
    g_print("\n");
    g_print(_(" -h  -- same as --help\n"));
    g_print(_(" -p  -- same as --profile\n"));
    g_print(_(" -v  -- same as --version\n"));
 //   g_print(_(" -C  -- same as --configure\n"));
    g_print(_("\nVisit http://lxde.org/ for detail.\n\n"));
}

int panel_handle_x_error(Display * d, XErrorEvent * ev)
{
    char buf[256];

    if (log_level >= LOG_WARN) {
        XGetErrorText(GDK_DISPLAY(), ev->error_code, buf, 256);
        LOG(LOG_WARN, "lxpanel : X error: %s\n", buf);
    }
    return 0;	/* Ignored */
}

int panel_handle_x_error_swallow_BadWindow_BadDrawable(Display * d, XErrorEvent * ev)
{
    if ((ev->error_code != BadWindow) && (ev->error_code != BadDrawable))
        panel_handle_x_error(d, ev);
    return 0;	/* Ignored */
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
        const gchar* name;

        if( ! dir )
        {
            g_free( panel_dir );
            continue;
        }

        while((name = g_dir_read_name(dir)) != NULL)
        {
            char* panel_config = g_build_filename( panel_dir, name, NULL );
            if (strchr(panel_config, '~') == NULL)	/* Skip editor backup files in case user has hand edited in this directory */
            {
                Panel* panel = panel_new( panel_config, name );
                if( panel )
                    all_panels = g_slist_prepend( all_panels, panel );
            }
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
    const char* desktop_name;

    setlocale(LC_CTYPE, "");

	g_thread_init(NULL);
	gdk_threads_init();

    gtk_init(&argc, &argv);

#ifdef ENABLE_NLS
    bindtextdomain ( GETTEXT_PACKAGE, PACKAGE_LOCALE_DIR );
    bind_textdomain_codeset ( GETTEXT_PACKAGE, "UTF-8" );
    textdomain ( GETTEXT_PACKAGE );
#endif

    XSetLocaleModifiers("");
    XSetErrorHandler((XErrorHandler) panel_handle_x_error);

    resolve_atoms();

    desktop_name = g_getenv("XDG_CURRENT_DESKTOP");
    is_in_lxde = desktop_name && (0 == strcmp(desktop_name, "LXDE"));

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
        printf("There is already an instance of LXPanel.  Now to exit\n");
        exit(1);
    }

    /* Add our own icons to the search path of icon theme */
    gtk_icon_theme_append_search_path( gtk_icon_theme_get_default(),
                                       PACKAGE_DATA_DIR "/lxpanel/images" );

    fbev = fb_ev_new();
    win_grp = gtk_window_group_new();

restart:
    is_restarting = FALSE;

    load_global_config();

	/* NOTE: StructureNotifyMask is required by XRandR
	 * See init_randr_support() in gdkscreen-x11.c of gtk+ for detail.
	 */
    XSelectInput (GDK_DISPLAY(), GDK_ROOT_WINDOW(), StructureNotifyMask|SubstructureNotifyMask|PropertyChangeMask);
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

    g_object_unref(win_grp);
    g_object_unref(fbev);

    return 0;
}
