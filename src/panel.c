/*
 * Copyright (c) 2006-2014 LxDE Developers, see the file AUTHORS for details.
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
#include <libfm/fm-gtk.h>

#define __LXPANEL_INTERNALS__

#include "private.h"
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

gboolean is_in_lxde = FALSE;

static void panel_start_gui(LXPanel *p);
static void ah_start(LXPanel *p);
static void ah_stop(LXPanel *p);
static void on_root_bg_changed(FbBg *bg, LXPanel* p);
static void _panel_update_background(LXPanel * p);

G_DEFINE_TYPE(PanelToplevel, lxpanel, GTK_TYPE_WINDOW);

static void lxpanel_finalize(GObject *object)
{
    LXPanel *self = LXPANEL(object);
    Panel *p = self->priv;

    if( p->config_changed )
        lxpanel_config_save( self );
    config_destroy(p->config);

    XFree(p->workarea);
    g_free( p->background_file );
    g_slist_free( p->system_menus );

    g_free( p->name );
    g_free(p);

    G_OBJECT_CLASS(lxpanel_parent_class)->finalize(object);
}

static void lxpanel_destroy(GtkObject *object)
{
    LXPanel *self = LXPANEL(object);
    Panel *p = self->priv;
    Display *xdisplay;

    if (p->autohide)
        ah_stop(self);

    if (p->pref_dialog != NULL)
        gtk_widget_destroy(p->pref_dialog);
    p->pref_dialog = NULL;

    if (p->plugin_pref_dialog != NULL)
        /* just close the dialog, it will do all required cleanup */
        gtk_dialog_response(GTK_DIALOG(p->plugin_pref_dialog), GTK_RESPONSE_CLOSE);

    if (p->bg != NULL)
    {
        g_signal_handlers_disconnect_by_func(G_OBJECT(p->bg), on_root_bg_changed, self);
        g_object_unref(p->bg);
        p->bg = NULL;
    }

    if (p->initialized)
    {
        gtk_window_group_remove_window(win_grp, GTK_WINDOW(self));
        xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
        gdk_flush();
        XFlush(xdisplay);
        XSync(xdisplay, True);
        p->initialized = FALSE;
    }

    if (p->background_update_queued)
    {
        g_source_remove(p->background_update_queued);
        p->background_update_queued = 0;
    }

    GTK_OBJECT_CLASS(lxpanel_parent_class)->destroy(object);
}

static gboolean idle_update_background(gpointer p)
{
    LXPanel *panel = LXPANEL(p);

    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;

    /* Panel could be destroyed while background update scheduled */
#if GTK_CHECK_VERSION(2, 20, 0)
    if (gtk_widget_get_realized(p))
#else
    if (GTK_WIDGET_REALIZED(p))
#endif
    {
        gdk_display_sync( gtk_widget_get_display(p) );
        _panel_update_background(panel);
    }
    panel->priv->background_update_queued = 0;

    return FALSE;
}

void _panel_queue_update_background(LXPanel *panel)
{
    if (panel->priv->background_update_queued)
        return;
    panel->priv->background_update_queued = g_idle_add_full(G_PRIORITY_HIGH,
                                                            idle_update_background,
                                                            panel, NULL);
}

static void lxpanel_realize(GtkWidget *widget)
{
    GTK_WIDGET_CLASS(lxpanel_parent_class)->realize(widget);

    _panel_queue_update_background(LXPANEL(widget));
}

static void lxpanel_style_set(GtkWidget *widget, GtkStyle* prev)
{
    GTK_WIDGET_CLASS(lxpanel_parent_class)->style_set(widget, prev);

    /* FIXME: This dirty hack is used to fix the background of systray... */
    _panel_queue_update_background(LXPANEL(widget));
}

static void lxpanel_size_request(GtkWidget *widget, GtkRequisition *req)
{
    Panel *p = LXPANEL(widget)->priv;

    GTK_WIDGET_CLASS(lxpanel_parent_class)->size_request(widget, req);

    if (!p->visible)
        /* When the panel is in invisible state, the content box also got hidden, thus always
         * report 0 size.  Ask the content box instead for its size. */
        gtk_widget_size_request(p->box, req);

    /* FIXME: is this ever required? */
    if (p->widthtype == WIDTH_REQUEST)
        p->width = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? req->width : req->height;
    if (p->heighttype == HEIGHT_REQUEST)
        p->height = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? req->height : req->width;
    calculate_position(p);

    gtk_widget_set_size_request( widget, p->aw, p->ah );
}

static void lxpanel_size_allocate(GtkWidget *widget, GtkAllocation *a)
{
    Panel *p = LXPANEL(widget)->priv;

    GTK_WIDGET_CLASS(lxpanel_parent_class)->size_allocate(widget, a);

    if (p->widthtype == WIDTH_REQUEST)
        p->width = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? a->width : a->height;
    if (p->heighttype == HEIGHT_REQUEST)
        p->height = (p->orientation == GTK_ORIENTATION_HORIZONTAL) ? a->height : a->width;
    calculate_position(p);

    if (a->width != p->aw || a->height != p->ah || a->x != p->ax || a->y != p->ay)
    {
        gtk_window_move(GTK_WINDOW(widget), p->ax, p->ay);
        _panel_set_wm_strut(LXPANEL(widget));
    }
    else if (p->background_update_queued)
    {
        g_source_remove(p->background_update_queued);
        p->background_update_queued = 0;
#if GTK_CHECK_VERSION(2, 20, 0)
        if (gtk_widget_get_realized(widget))
#else
        if (GTK_WIDGET_REALIZED(widget))
#endif
            _panel_update_background(LXPANEL(widget));
    }
}

static gboolean lxpanel_configure_event (GtkWidget *widget, GdkEventConfigure *e)
{
    Panel *p = LXPANEL(widget)->priv;

    if (e->width == p->cw && e->height == p->ch && e->x == p->cx && e->y == p->cy)
        goto ok;
    p->cw = e->width;
    p->ch = e->height;
    p->cx = e->x;
    p->cy = e->y;

    if (p->transparent)
        fb_bg_notify_changed_bg(p->bg);
ok:
    return GTK_WIDGET_CLASS(lxpanel_parent_class)->configure_event(widget, e);
}

static gboolean lxpanel_map_event(GtkWidget *widget, GdkEventAny *event)
{
    Panel *p = PLUGIN_PANEL(widget)->priv;

    if (p->autohide)
        ah_start(LXPANEL(widget));
    return GTK_WIDGET_CLASS(lxpanel_parent_class)->map_event(widget, event);
}

/* Handler for "button_press_event" signal with Panel as parameter. */
static gboolean lxpanel_button_press(GtkWidget *widget, GdkEventButton *event)
{
    if (event->button == 3) /* right button */
    {
        GtkMenu* popup = (GtkMenu*) lxpanel_get_plugin_menu(LXPANEL(widget), NULL, FALSE);
        gtk_menu_popup(popup, NULL, NULL, NULL, NULL, event->button, event->time);
        return TRUE;
    }
    return FALSE;
}

static void lxpanel_class_init(PanelToplevelClass *klass)
{
    GObjectClass *gobject_class = (GObjectClass *)klass;
    GtkObjectClass *gtk_object_class = (GtkObjectClass *)klass;
    GtkWidgetClass *widget_class = (GtkWidgetClass *)klass;

    gobject_class->finalize = lxpanel_finalize;
    gtk_object_class->destroy = lxpanel_destroy;
    widget_class->realize = lxpanel_realize;
    widget_class->size_request = lxpanel_size_request;
    widget_class->size_allocate = lxpanel_size_allocate;
    widget_class->configure_event = lxpanel_configure_event;
    widget_class->style_set = lxpanel_style_set;
    widget_class->map_event = lxpanel_map_event;
    widget_class->button_press_event = lxpanel_button_press;
}

static void lxpanel_init(PanelToplevel *self)
{
    Panel *p = g_new0(Panel, 1);

    self->priv = p;
    p->topgwin = self;
    p->allign = ALLIGN_CENTER;
    p->edge = EDGE_NONE;
    p->widthtype = WIDTH_PERCENT;
    p->width = 100;
    p->heighttype = HEIGHT_PIXEL;
    p->height = PANEL_HEIGHT_DEFAULT;
    p->monitor = 0;
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
    p->usefontsize = 0;
    p->fontsize = 10;
    p->spacing = 0;
    p->icon_size = PANEL_ICON_SIZE;
    p->icon_theme = gtk_icon_theme_get_default();
    p->config = config_new();
    p->defstyle = gtk_widget_get_default_style();
    gtk_window_set_type_hint(GTK_WINDOW(self), GDK_WINDOW_TYPE_HINT_DOCK);
}

/* Allocate and initialize new Panel structure. */
static LXPanel* panel_allocate(void)
{
    return g_object_new(LX_TYPE_PANEL, NULL);
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
    if (p->monitor < 0)
        p->monitor = 0;
    if (p->background)
        p->transparent = 0;
}

/****************************************************
 *         panel's handlers for WM events           *
 ****************************************************/

void panel_set_wm_strut(Panel *p)
{
    _panel_set_wm_strut(p->topgwin);
}

void _panel_set_wm_strut(LXPanel *panel)
{
    int index;
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Panel *p = panel->priv;
    gulong strut_size;
    gulong strut_lower;
    gulong strut_upper;

#if GTK_CHECK_VERSION(2, 20, 0)
    if (!gtk_widget_get_mapped(GTK_WIDGET(panel)))
#else
    if (!GTK_WIDGET_MAPPED(panel))
#endif
        return;
    /* most wm's tend to ignore struts of unmapped windows, and that's how
     * lxpanel hides itself. so no reason to set it. */
    if (p->autohide && p->height_when_hidden <= 0)
        return;

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
    if ((p->strut_size != strut_size) || (p->strut_lower != strut_lower) || (p->strut_upper != strut_upper) || (p->strut_edge != p->edge))
    {
        p->strut_size = strut_size;
        p->strut_lower = strut_lower;
        p->strut_upper = strut_upper;
        p->strut_edge = p->edge;

        /* If window manager supports STRUT_PARTIAL, it will ignore STRUT.
         * Set STRUT also for window managers that do not support STRUT_PARTIAL. */
        if (strut_size != 0)
        {
            XChangeProperty(xdisplay, p->topxwin, a_NET_WM_STRUT_PARTIAL,
                XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) desired_strut, 12);
            XChangeProperty(xdisplay, p->topxwin, a_NET_WM_STRUT,
                XA_CARDINAL, 32, PropModeReplace,  (unsigned char *) desired_strut, 4);
        }
        else
        {
            XDeleteProperty(xdisplay, p->topxwin, a_NET_WM_STRUT);
            XDeleteProperty(xdisplay, p->topxwin, a_NET_WM_STRUT_PARTIAL);
        }
    }
}

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
                LXPanel* p = (LXPanel*)l->data;
                GList *plugins, *pl;

                plugins = gtk_container_get_children(GTK_CONTAINER(p->priv->box));
                for (pl = plugins; pl; pl = pl->next)
                {
                    const LXPanelPluginInit *init = PLUGIN_CLASS(pl->data);
                    if (init->show_system_menu)
                        /* queue to show system menu */
                        init->show_system_menu(pl->data);
                }
                g_list_free(plugins);
            }
            break;
        }
#endif
        case LXPANEL_CMD_RUN:
            gtk_run();
            break;
        case LXPANEL_CMD_CONFIG:
            {
            LXPanel * p = ((all_panels != NULL) ? all_panels->data : NULL);
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
                ((LXPanel*)l->data)->priv->curdesk = get_net_current_desktop();
            fb_ev_emit(fbev, EV_CURRENT_DESKTOP);
        }
        else if (at == a_NET_NUMBER_OF_DESKTOPS)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
                ((LXPanel*)l->data)->priv->desknum = get_net_number_of_desktops();
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
                LXPanel* p = (LXPanel*)l->data;
                if (p->priv->transparent) {
                    fb_bg_notify_changed_bg(p->priv->bg);
                }
            }
        }
        else if (at == a_NET_WORKAREA)
        {
            GSList* l;
            for( l = all_panels; l; l = l->next )
            {
                LXPanel* p = (LXPanel*)l->data;
                XFree( p->priv->workarea );
                p->priv->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->priv->wa_len);
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


static void
on_root_bg_changed(FbBg *bg, LXPanel* p)
{
    _panel_update_background( p );
}

void panel_determine_background_pixmap(Panel * panel, GtkWidget * widget, GdkWindow * window)
{
    _panel_determine_background_pixmap(panel->topgwin, widget);
}

void _panel_determine_background_pixmap(LXPanel * panel, GtkWidget * widget)
{
    GdkPixmap * pixmap = NULL;
    GdkWindow * window = gtk_widget_get_window(widget);
    Panel * p = panel->priv;

    /* Free p->bg if it is not going to be used. */
    if (( ! p->transparent) && (p->bg != NULL))
    {
        g_signal_handlers_disconnect_by_func(G_OBJECT(p->bg), on_root_bg_changed, panel);
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
            g_signal_connect(G_OBJECT(p->bg), "changed", G_CALLBACK(on_root_bg_changed), panel);
        }
        pixmap = fb_bg_get_xroot_pix_for_win(p->bg, widget);
        if ((pixmap != NULL) && (pixmap != GDK_NO_BG) && (p->alpha != 0))
            fb_bg_composite(pixmap, &p->gtintcolor, p->alpha);
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
    _panel_update_background(p->topgwin);
}

static void _panel_update_background(LXPanel * p)
{
    GtkWidget *w = GTK_WIDGET(p);
    GList *plugins, *l;

    /* Redraw the top level widget. */
    _panel_determine_background_pixmap(p, w);
    gdk_window_clear(gtk_widget_get_window(w));
    gtk_widget_queue_draw(w);

    /* Loop over all plugins redrawing each plugin. */
    plugins = gtk_container_get_children(GTK_CONTAINER(p->priv->box));
    for (l = plugins; l != NULL; l = l->next)
        plugin_widget_set_background(l->data, p);
    g_list_free(plugins);
}

/****************************************************
 *         autohide : borrowed from fbpanel         *
 ****************************************************/

/* Autohide is behaviour when panel hides itself when mouse is "far enough"
 * and pops up again when mouse comes "close enough".
 * Formally, it's a state machine with 3 states that driven by mouse
 * coordinates and timer:
 * 1. VISIBLE - ensures that panel is visible. When/if mouse goes "far enough"
 *      switches to WAITING state
 * 2. WAITING - starts timer. If mouse comes "close enough", stops timer and
 *      switches to VISIBLE.  If timer expires, switches to HIDDEN
 * 3. HIDDEN - hides panel. When mouse comes "close enough" switches to VISIBLE
 *
 * Note 1
 * Mouse coordinates are queried every PERIOD milisec
 *
 * Note 2
 * If mouse is less then GAP pixels to panel it's considered to be close,
 * otherwise it's far
 */

#define GAP 2
#define PERIOD 300

typedef enum
{
    AH_STATE_VISIBLE,
    AH_STATE_WAITING,
    AH_STATE_HIDDEN
} PanelAHState;

static void ah_state_set(LXPanel *p, PanelAHState ah_state);

static gboolean
mouse_watch(LXPanel *panel)
{
    Panel *p = panel->priv;
    gint x, y;

    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;

    ENTER;
    gdk_display_get_pointer(gdk_display_get_default(), NULL, &x, &y, NULL);

/*  Reduce sensitivity area
    p->ah_far = ((x < p->cx - GAP) || (x > p->cx + p->cw + GAP)
        || (y < p->cy - GAP) || (y > p->cy + p->ch + GAP));
*/

    gint cx, cy, cw, ch, gap;

    cx = p->ax;
    cy = p->ay;
    cw = p->cw;
    ch = p->ch;

    if (cw == 1) cw = 0;
    if (ch == 1) ch = 0;
    /* reduce area which will raise panel so it does not interfere with apps */
    if (p->ah_state == AH_STATE_HIDDEN) {
        gap = MAX(p->height_when_hidden, GAP);
        switch (p->edge) {
        case EDGE_LEFT:
            cw = gap;
            break;
        case EDGE_RIGHT:
            cx = cx + cw - gap;
            cw = gap;
            break;
        case EDGE_TOP:
            ch = gap;
            break;
        case EDGE_BOTTOM:
            cy = cy + ch - gap;
            ch = gap;
            break;
       }
    }
    p->ah_far = ((x < cx) || (x > cx + cw) || (y < cy) || (y > cy + ch));

    ah_state_set(panel, p->ah_state);
    RET(TRUE);
}

static gboolean ah_state_hide_timeout(gpointer p)
{
    if (!g_source_is_destroyed(g_main_current_source()))
    {
        ah_state_set(p, AH_STATE_HIDDEN);
        ((LXPanel *)p)->priv->hide_timeout = 0;
    }
    return FALSE;
}

static void ah_state_set(LXPanel *panel, PanelAHState ah_state)
{
    Panel *p = panel->priv;

    ENTER;
    if (p->ah_state != ah_state) {
        p->ah_state = ah_state;
        switch (ah_state) {
        case AH_STATE_VISIBLE:
            gtk_widget_show(GTK_WIDGET(panel));
            gtk_widget_show(p->box);
            gtk_widget_queue_resize(GTK_WIDGET(panel));
            gtk_window_stick(GTK_WINDOW(panel));
            p->visible = TRUE;
            break;
        case AH_STATE_WAITING:
            if (p->hide_timeout)
                g_source_remove(p->hide_timeout);
            p->hide_timeout = g_timeout_add(2 * PERIOD, ah_state_hide_timeout, panel);
            break;
        case AH_STATE_HIDDEN:
            if (p->height_when_hidden > 0)
                gtk_widget_hide(p->box);
            else
                gtk_widget_hide(GTK_WIDGET(panel));
            p->visible = FALSE;
        }
    } else if (p->autohide && p->ah_far) {
        switch (ah_state) {
        case AH_STATE_VISIBLE:
            ah_state_set(panel, AH_STATE_WAITING);
            break;
        case AH_STATE_WAITING:
            break;
        case AH_STATE_HIDDEN:
            /* configurator might change height_when_hidden value */
            if (p->height_when_hidden > 0)
            {
                if (gtk_widget_get_visible(p->box))
                {
                    gtk_widget_hide(p->box);
                    gtk_widget_show(GTK_WIDGET(panel));
                }
            }
            else
                if (gtk_widget_get_visible(GTK_WIDGET(panel)))
                {
                    gtk_widget_hide(GTK_WIDGET(panel));
                    gtk_widget_show(p->box);
                }
        }
    } else {
        switch (ah_state) {
        case AH_STATE_VISIBLE:
            break;
        case AH_STATE_WAITING:
            if (p->hide_timeout)
                g_source_remove(p->hide_timeout);
            p->hide_timeout = 0;
            /* continue with setting visible */
        case AH_STATE_HIDDEN:
            ah_state_set(panel, AH_STATE_VISIBLE);
        }
    }
    RET();
}

/* starts autohide behaviour */
static void ah_start(LXPanel *p)
{
    ENTER;
    if (!p->priv->mouse_timeout)
        p->priv->mouse_timeout = g_timeout_add(PERIOD, (GSourceFunc) mouse_watch, p);
    RET();
}

/* stops autohide */
static void ah_stop(LXPanel *p)
{
    ENTER;
    if (p->priv->mouse_timeout) {
        g_source_remove(p->priv->mouse_timeout);
        p->priv->mouse_timeout = 0;
    }
    if (p->priv->hide_timeout) {
        g_source_remove(p->priv->hide_timeout);
        p->priv->hide_timeout = 0;
    }
    RET();
}
/* end of the autohide code
 * ------------------------------------------------------------- */

static gint
panel_popupmenu_configure(GtkWidget *widget, gpointer user_data)
{
    panel_configure( (LXPanel*)user_data, 0 );
    return TRUE;
}

static void panel_popupmenu_config_plugin( GtkMenuItem* item, GtkWidget* plugin )
{
    Panel *panel = PLUGIN_PANEL(plugin)->priv;

    lxpanel_plugin_show_config_dialog(plugin);

    /* FIXME: this should be more elegant */
    panel->config_changed = TRUE;
}

static void panel_popupmenu_add_item( GtkMenuItem* item, LXPanel* panel )
{
    /* panel_add_plugin( panel, panel->topgwin ); */
    panel_configure( panel, 2 );
}

static void panel_popupmenu_remove_item( GtkMenuItem* item, GtkWidget* plugin )
{
    Panel* panel = PLUGIN_PANEL(plugin)->priv;

    /* If the configuration dialog is open, there will certainly be a crash if the
     * user manipulates the Configured Plugins list, after we remove this entry.
     * Close the configuration dialog if it is open. */
    if (panel->pref_dialog != NULL)
    {
        gtk_widget_destroy(panel->pref_dialog);
        panel->pref_dialog = NULL;
    }
    config_setting_destroy(g_object_get_qdata(G_OBJECT(plugin), lxpanel_plugin_qconf));
    /* reset conf pointer because the widget still may be referenced by configurator */
    g_object_set_qdata(G_OBJECT(plugin), lxpanel_plugin_qconf, NULL);

    lxpanel_config_save(PLUGIN_PANEL(plugin));
    gtk_widget_destroy(plugin);
}

/* FIXME: Potentially we can support multiple panels at the same edge,
 * but currently this cannot be done due to some positioning problems. */
static char* gen_panel_name( int edge, gint monitor )
{
    const char* edge_str = num2str( edge_pair, edge, "" );
    char* name = NULL;
    char* dir = _user_config_file_name("panels", NULL);
    int i;
    for( i = 0; i < G_MAXINT; ++i )
    {
        char* f;
        if(monitor != 0)
            name = g_strdup_printf( "%s-m%d-%d", edge_str, monitor, i );
        else if( G_LIKELY( i > 0 ) )
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
static void panel_popupmenu_create_panel( GtkMenuItem* item, LXPanel* panel )
{
    gint m, e, monitors;
    GdkScreen *screen;
    LXPanel *new_panel = panel_allocate();
    Panel *p = new_panel->priv;
    config_setting_t *global;

    /* Allocate the edge. */
    screen = gdk_screen_get_default();
    g_assert(screen);
    monitors = gdk_screen_get_n_monitors(screen);
    for(m=0; m<monitors; ++m)
    {
        /* try each of the four edges */
        for(e=1; e<5; ++e)
        {
            if(panel_edge_available(p,e,m)) {
                p->edge = e;
                p->monitor = m;
                goto found_edge;
            }
        }
    }

    gtk_widget_destroy(GTK_WIDGET(new_panel));
    g_warning("Error adding panel: There is no room for another panel. All the edges are taken.");
    fm_show_error(NULL, NULL, _("There is no room for another panel. All the edges are taken."));
    return;

found_edge:
    p->name = gen_panel_name(p->edge, p->monitor);

    /* create new config with first group "Global" */
    global = config_group_add_subgroup(config_root_setting(p->config), "Global");
    config_group_set_string(global, "edge", num2str(edge_pair, p->edge, "none"));
    config_group_set_int(global, "monitor", p->monitor);
    panel_configure(new_panel, 0);
    panel_normalize_configuration(p);
    panel_start_gui(new_panel);
    gtk_widget_show_all(GTK_WIDGET(new_panel));

    lxpanel_config_save(new_panel);
    all_panels = g_slist_prepend(all_panels, new_panel);
}

static void panel_popupmenu_delete_panel( GtkMenuItem* item, LXPanel* panel )
{
    GtkWidget* dlg;
    gboolean ok;
    dlg = gtk_message_dialog_new_with_markup( GTK_WINDOW(panel),
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
        gchar *fname;
        all_panels = g_slist_remove( all_panels, panel );

        /* delete the config file of this panel */
        fname = _user_config_file_name("panels", panel->priv->name);
        g_unlink( fname );
        g_free(fname);
        panel->priv->config_changed = 0;
        gtk_widget_destroy(GTK_WIDGET(panel));
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
        "Martin Bagge <brother@bsnet.se>",
        "Andriy Grytsenko <andrej@rep.kiev.ua>",
        "Giuseppe Penone <giuspen@gmail.com>",
        "Piotr Sipika <piotr.sipika@gmail.com>",
        NULL
    };
    /* TRANSLATORS: Replace this string with your names, one name per line. */
    gchar *translators = _( "translator-credits" );

    about = gtk_about_dialog_new();
    panel_apply_icon(GTK_WINDOW(about));
    gtk_about_dialog_set_version(GTK_ABOUT_DIALOG(about), VERSION);
    gtk_about_dialog_set_program_name(GTK_ABOUT_DIALOG(about), _("LXPanel"));

    if(gtk_icon_theme_has_icon(panel->icon_theme, "video-display"))
    {
         gtk_about_dialog_set_logo( GTK_ABOUT_DIALOG(about),
                                    gtk_icon_theme_load_icon(panel->icon_theme, "video-display", 48, 0, NULL));
    }
    else if (gtk_icon_theme_has_icon(panel->icon_theme, "start-here"))
    {
         gtk_about_dialog_set_logo( GTK_ABOUT_DIALOG(about),
                                    gtk_icon_theme_load_icon(panel->icon_theme, "start-here", 48, 0, NULL));
    }
    else
    {
        gtk_about_dialog_set_logo(  GTK_ABOUT_DIALOG(about),
                                    gdk_pixbuf_new_from_file(PACKAGE_DATA_DIR "/images/my-computer.png", NULL));
    }

    gtk_about_dialog_set_copyright(GTK_ABOUT_DIALOG(about), _("Copyright (C) 2008-2014"));
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
    GdkPixbuf* window_icon;

    if(gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "video-display"))
    {
        window_icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "video-display", 24, 0, NULL);
    }
    else if(gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "start-here"))
    {
        window_icon = gtk_icon_theme_load_icon(gtk_icon_theme_get_default(), "start-here", 24, 0, NULL);
    }
    else
    {
        window_icon = gdk_pixbuf_new_from_file(PACKAGE_DATA_DIR "/images/my-computer.png", NULL);
    }
    gtk_window_set_icon(w, window_icon);
}

GtkMenu* lxpanel_get_plugin_menu( LXPanel* panel, GtkWidget* plugin, gboolean use_sub_menu )
{
    GtkWidget  *menu_item, *img;
    GtkMenu *ret,*menu;
    const LXPanelPluginInit *init;
    char* tmp;

    ret = menu = GTK_MENU(gtk_menu_new());

    if (plugin)
    {
        init = PLUGIN_CLASS(plugin);
        /* create single item - plugin instance settings */
        img = gtk_image_new_from_stock( GTK_STOCK_PREFERENCES, GTK_ICON_SIZE_MENU );
        tmp = g_strdup_printf( _("\"%s\" Settings"), _(init->name) );
        menu_item = gtk_image_menu_item_new_with_label( tmp );
        g_free( tmp );
        gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
        gtk_menu_shell_prepend(GTK_MENU_SHELL(ret), menu_item);
        if( init->config )
            g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_config_plugin), plugin );
        else
            gtk_widget_set_sensitive( menu_item, FALSE );
        /* add custom items by plugin if requested */
        if (init->update_context_menu != NULL)
            use_sub_menu = init->update_context_menu(plugin, ret);
        /* append a separator */
        menu_item = gtk_separator_menu_item_new();
        gtk_menu_shell_append(GTK_MENU_SHELL(ret), menu_item);
    }
    if (use_sub_menu)
        menu = GTK_MENU(gtk_menu_new());

    img = gtk_image_new_from_stock( GTK_STOCK_EDIT, GTK_ICON_SIZE_MENU );
    menu_item = gtk_image_menu_item_new_with_label(_("Add / Remove Panel Items"));
    gtk_image_menu_item_set_image( (GtkImageMenuItem*)menu_item, img );
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_add_item), panel );

    if( plugin )
    {
        img = gtk_image_new_from_stock( GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU );
        tmp = g_strdup_printf( _("Remove \"%s\" From Panel"), _(init->name) );
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
    g_signal_connect( menu_item, "activate", G_CALLBACK(panel_popupmenu_about), panel->priv );

    if( use_sub_menu )
    {
        menu_item = gtk_image_menu_item_new_with_label(_("Panel"));
        gtk_menu_shell_append(GTK_MENU_SHELL(ret), menu_item);
        gtk_menu_item_set_submenu(GTK_MENU_ITEM(menu_item), GTK_WIDGET(menu) );
    }

    gtk_widget_show_all(GTK_WIDGET(ret));

    g_signal_connect( ret, "selection-done", G_CALLBACK(gtk_widget_destroy), NULL );
    return ret;
}

/* for old plugins compatibility */
GtkMenu* lxpanel_get_panel_menu( Panel* panel, Plugin* plugin, gboolean use_sub_menu )
{
    return lxpanel_get_plugin_menu(panel->topgwin, plugin->pwid, use_sub_menu);
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
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

    if (p->setdocktype) {
        Atom state = a_NET_WM_WINDOW_TYPE_DOCK;
        XChangeProperty(xdisplay, p->topxwin,
                        a_NET_WM_WINDOW_TYPE, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *) &state, 1);
    }
    else {
        XDeleteProperty( xdisplay, p->topxwin, a_NET_WM_WINDOW_TYPE );
    }
}

void panel_establish_autohide(Panel *p)
{
    _panel_establish_autohide(p->topgwin);
}

void _panel_establish_autohide(LXPanel *p)
{
    if (p->priv->autohide)
        ah_start(p);
    else
    {
        ah_stop(p);
        ah_state_set(p, AH_STATE_VISIBLE);
    }
}

/* Set an image from a file with scaling to the panel icon size. */
void panel_image_set_from_file(Panel * p, GtkWidget * image, const char * file)
{
    GdkPixbuf * pixbuf = gdk_pixbuf_new_from_file_at_scale(file, p->icon_size, p->icon_size, TRUE, NULL);
    if (pixbuf != NULL)
    {
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
        g_object_unref(pixbuf);
    }
}

void lxpanel_image_set_from_file(LXPanel * p, GtkWidget * image, const char * file)
{
    panel_image_set_from_file(p->priv, image, file);
}

/* Set an image from a icon theme with scaling to the panel icon size. */
gboolean panel_image_set_icon_theme(Panel * p, GtkWidget * image, const gchar * icon)
{
    if (gtk_icon_theme_has_icon(p->icon_theme, icon))
    {
        GdkPixbuf * pixbuf = gtk_icon_theme_load_icon(p->icon_theme, icon, p->icon_size, 0, NULL);
        gtk_image_set_from_pixbuf(GTK_IMAGE(image), pixbuf);
        g_object_unref(pixbuf);
        return TRUE;
    }
    return FALSE;
}

gboolean lxpanel_image_set_icon_theme(LXPanel * p, GtkWidget * image, const gchar * icon)
{
    return panel_image_set_icon_theme(p->priv, image, icon);
}

static void
panel_start_gui(LXPanel *panel)
{
    Atom state[3];
    XWMHints wmhints;
    gulong val;
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    Panel *p = panel->priv;
    GtkWidget *w = GTK_WIDGET(panel);

    ENTER;

    p->curdesk = get_net_current_desktop();
    p->desknum = get_net_number_of_desktops();
    p->workarea = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_WORKAREA, XA_CARDINAL, &p->wa_len);

    /* main toplevel window */
    /* p->topgwin =  gtk_window_new(GTK_WINDOW_TOPLEVEL); */
    gtk_widget_set_name(w, "PanelToplevel");
    p->display = gdk_display_get_default();
    gtk_container_set_border_width(GTK_CONTAINER(panel), 0);
    gtk_window_set_resizable(GTK_WINDOW(panel), FALSE);
    gtk_window_set_wmclass(GTK_WINDOW(panel), "panel", "lxpanel");
    gtk_window_set_title(GTK_WINDOW(panel), "panel");
    gtk_window_set_position(GTK_WINDOW(panel), GTK_WIN_POS_NONE);
    gtk_window_set_decorated(GTK_WINDOW(panel), FALSE);

    gtk_window_group_add_window( win_grp, (GtkWindow*)panel );

    gtk_widget_add_events( w, GDK_BUTTON_PRESS_MASK );

    gtk_widget_realize(w);
    //gdk_window_set_decorations(gtk_widget_get_window(p->topgwin), 0);

    // main layout manager as a single child of panel
    p->box = panel_box_new(panel, FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(p->box), 0);
    gtk_container_add(GTK_CONTAINER(panel), p->box);
    gtk_widget_show(p->box);
    if (p->round_corners)
        make_round_corners(p);

    p->topxwin = GDK_WINDOW_XWINDOW(gtk_widget_get_window(w));
    DBG("topxwin = %x\n", p->topxwin);

    /* the settings that should be done before window is mapped */
    wmhints.flags = InputHint;
    wmhints.input = 0;
    XSetWMHints (xdisplay, p->topxwin, &wmhints);
#define WIN_HINTS_SKIP_FOCUS      (1<<0)    /* "alt-tab" skips this win */
    val = WIN_HINTS_SKIP_FOCUS;
    XChangeProperty(xdisplay, p->topxwin,
          XInternAtom(xdisplay, "_WIN_HINTS", False), XA_CARDINAL, 32,
          PropModeReplace, (unsigned char *) &val, 1);

    panel_set_dock_type(p);

    /* window mapping point */
    gtk_widget_show_all(w);

    /* the settings that should be done after window is mapped */
    _panel_establish_autohide(panel);

    /* send it to running wm */
    Xclimsg(p->topxwin, a_NET_WM_DESKTOP, G_MAXULONG, 0, 0, 0, 0);
    /* and assign it ourself just for case when wm is not running */
    val = G_MAXULONG;
    XChangeProperty(xdisplay, p->topxwin, a_NET_WM_DESKTOP, XA_CARDINAL, 32,
          PropModeReplace, (unsigned char *) &val, 1);

    state[0] = a_NET_WM_STATE_SKIP_PAGER;
    state[1] = a_NET_WM_STATE_SKIP_TASKBAR;
    state[2] = a_NET_WM_STATE_STICKY;
    XChangeProperty(xdisplay, p->topxwin, a_NET_WM_STATE, XA_ATOM,
          32, PropModeReplace, (unsigned char *) state, 3);

    _calculate_position(panel);
    gdk_window_move_resize(gtk_widget_get_window(w), p->ax, p->ay, p->aw, p->ah);
    _panel_set_wm_strut(panel);
    p->initialized = TRUE;

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
void panel_draw_label_text(Panel * p, GtkWidget * label, const char * text,
                           gboolean bold, float custom_size_factor,
                           gboolean custom_color)
{
    if (text == NULL)
    {
        /* Null string. */
        gtk_label_set_text(GTK_LABEL(label), NULL);
        return;
    }

    /* Compute an appropriate size so the font will scale with the panel's icon size. */
    int font_desc;
    if (p->usefontsize)
        font_desc = p->fontsize;
    else
    {
        GtkStyle *style = gtk_widget_get_style(label);
        font_desc = pango_font_description_get_size(style->font_desc) / PANGO_SCALE;
    }
    font_desc *= custom_size_factor;

    /* Check the string for characters that need to be escaped.
     * If any are found, create the properly escaped string and use it instead. */
    const char * valid_markup = text;
    char * escaped_text = NULL;
    const char * q;
    for (q = text; *q != '\0'; q += 1)
    {
        if ((*q == '<') || (*q == '>') || (*q == '&'))
        {
            escaped_text = g_markup_escape_text(text, -1);
            valid_markup = escaped_text;
            break;
        }
    }

    gchar * formatted_text;
    if ((custom_color) && (p->usefontcolor))
    {
        /* Color, optionally bold. */
        formatted_text = g_strdup_printf("<span font_desc=\"%d\" color=\"#%06x\">%s%s%s</span>",
                font_desc,
                gcolor2rgb24(&p->gfontcolor),
                ((bold) ? "<b>" : ""),
                valid_markup,
                ((bold) ? "</b>" : ""));
    }
    else
    {
        /* No color, optionally bold. */
        formatted_text = g_strdup_printf("<span font_desc=\"%d\">%s%s%s</span>",
                font_desc,
                ((bold) ? "<b>" : ""),
                valid_markup,
                ((bold) ? "</b>" : ""));
    }

    gtk_label_set_markup(GTK_LABEL(label), formatted_text);
    g_free(formatted_text);
    g_free(escaped_text);
}

void lxpanel_draw_label_text(LXPanel * p, GtkWidget * label, const char * text,
                           gboolean bold, float custom_size_factor,
                           gboolean custom_color)
{
    panel_draw_label_text(p->priv, label, text, bold, custom_size_factor, custom_color);
}

void panel_set_panel_configuration_changed(Panel *p)
{
    _panel_set_panel_configuration_changed(p->topgwin);
}

void _panel_set_panel_configuration_changed(LXPanel *panel)
{
    Panel *p = panel->priv;
    GList *plugins, *l;

    GtkOrientation previous_orientation = p->orientation;
    p->orientation = (p->edge == EDGE_TOP || p->edge == EDGE_BOTTOM)
        ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;

    /* either first run or orientation was changed */
    if (!p->initialized || previous_orientation != p->orientation)
    {
        panel_adjust_geometry_terminology(p);
        if (p->initialized)
            p->height = ((p->orientation == GTK_ORIENTATION_HORIZONTAL) ? PANEL_HEIGHT_DEFAULT : PANEL_WIDTH_DEFAULT);
        if (p->height_control != NULL)
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->height_control), p->height);
        if ((p->widthtype == WIDTH_PIXEL) && (p->width_control != NULL))
        {
            int value = ((p->orientation == GTK_ORIENTATION_HORIZONTAL) ? gdk_screen_width() : gdk_screen_height());
            gtk_spin_button_set_range(GTK_SPIN_BUTTON(p->width_control), 0, value);
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(p->width_control), value);
        }
    }

    /* FIXME: it's deprecated, kept for binary compatibility */
    if (p->orientation == GTK_ORIENTATION_HORIZONTAL) {
        p->my_box_new = gtk_hbox_new;
        p->my_separator_new = gtk_vseparator_new;
    } else {
        p->my_box_new = gtk_vbox_new;
        p->my_separator_new = gtk_hseparator_new;
    }

    /* recreate the main layout box */
    if (p->box != NULL)
    {
        gtk_orientable_set_orientation(GTK_ORIENTABLE(p->box), p->orientation);
    }

    /* NOTE: This loop won't be executed when panel started since
       plugins are not loaded at that time.
       This is used when the orientation of the panel is changed
       from the config dialog, and plugins should be re-layout.
    */
    plugins = p->box ? gtk_container_get_children(GTK_CONTAINER(p->box)) : NULL;
    for( l = plugins; l; l = l->next ) {
        GtkWidget *w = (GtkWidget*)l->data;
        const LXPanelPluginInit *init = PLUGIN_CLASS(w);
        if (init->reconfigure)
            init->reconfigure(panel, w);
    }
    g_list_free(plugins);
    /* panel geometry changed? update panel background then */
    _panel_queue_update_background(panel);
}

static int
panel_parse_global(Panel *p, config_setting_t *cfg)
{
    const char *str;
    gint i;

    /* check Global config */
    if (!cfg || strcmp(config_setting_get_name(cfg), "Global") != 0)
    {
        g_warning( "lxpanel: Global section not found");
        RET(0);
    }
    if (config_setting_lookup_string(cfg, "edge", &str))
        p->edge = str2num(edge_pair, str, EDGE_NONE);
    if (config_setting_lookup_string(cfg, "allign", &str))
        p->allign = str2num(allign_pair, str, ALLIGN_NONE);
    config_setting_lookup_int(cfg, "monitor", &p->monitor);
    config_setting_lookup_int(cfg, "margin", &p->margin);
    if (config_setting_lookup_string(cfg, "widthtype", &str))
        p->widthtype = str2num(width_pair, str, WIDTH_NONE);
    config_setting_lookup_int(cfg, "width", &p->width);
    if (config_setting_lookup_string(cfg, "heighttype", &str))
        p->heighttype = str2num(height_pair, str, HEIGHT_NONE);
    config_setting_lookup_int(cfg, "height", &p->height);
    if (config_setting_lookup_int(cfg, "spacing", &i) && i > 0)
        p->spacing = i;
    if (config_setting_lookup_int(cfg, "setdocktype", &i))
        p->setdocktype = i != 0;
    if (config_setting_lookup_int(cfg, "setpartialstrut", &i))
        p->setstrut = i != 0;
    if (config_setting_lookup_int(cfg, "RoundCorners", &i))
        p->round_corners = i != 0;
    if (config_setting_lookup_int(cfg, "transparent", &i))
        p->transparent = i != 0;
    if (config_setting_lookup_int(cfg, "alpha", &p->alpha))
    {
        if (p->alpha > 255)
            p->alpha = 255;
    }
    if (config_setting_lookup_int(cfg, "autohide", &i))
        p->autohide = i != 0;
    if (config_setting_lookup_int(cfg, "heightwhenhidden", &i))
        p->height_when_hidden = MAX(0, i);
    if (config_setting_lookup_string(cfg, "tintcolor", &str))
    {
        if (!gdk_color_parse (str, &p->gtintcolor))
            gdk_color_parse ("white", &p->gtintcolor);
        p->tintcolor = gcolor2rgb24(&p->gtintcolor);
            DBG("tintcolor=%x\n", p->tintcolor);
    }
    if (config_setting_lookup_int(cfg, "usefontcolor", &i))
        p->usefontcolor = i != 0;
    if (config_setting_lookup_string(cfg, "fontcolor", &str))
    {
        if (!gdk_color_parse (str, &p->gfontcolor))
            gdk_color_parse ("black", &p->gfontcolor);
        p->fontcolor = gcolor2rgb24(&p->gfontcolor);
            DBG("fontcolor=%x\n", p->fontcolor);
    }
    if (config_setting_lookup_int(cfg, "usefontsize", &i))
        p->usefontsize = i != 0;
    if (config_setting_lookup_int(cfg, "fontsize", &i) && i > 0)
        p->fontsize = i;
    if (config_setting_lookup_int(cfg, "background", &i))
        p->background = i != 0;
    if (config_setting_lookup_string(cfg, "backgroundfile", &str))
        p->background_file = g_strdup(str);
    config_setting_lookup_int(cfg, "iconsize", &p->icon_size);

    panel_normalize_configuration(p);

    return 1;
}

static int
panel_parse_plugin(LXPanel *p, config_setting_t *cfg)
{
    const char *type = NULL;

    ENTER;
    config_setting_lookup_string(cfg, "type", &type);
    DBG("plug %s\n", type);

    if (!type || lxpanel_add_plugin(p, type, cfg, -1) == NULL) {
        g_warning( "lxpanel: can't load %s plugin", type);
        goto error;
    }
    RET(1);

error:
    RET(0);
}

static int panel_start( LXPanel *p )
{
    config_setting_t *list, *s;
    int i;

    /* parse global section */
    ENTER;

    list = config_setting_get_member(config_root_setting(p->priv->config), "");
    if (!list || !panel_parse_global(p->priv, config_setting_get_elem(list, 0)))
        RET(0);

    panel_start_gui(p);

    for (i = 1; (s = config_setting_get_elem(list, i)) != NULL; )
        if (strcmp(config_setting_get_name(s), "Plugin") == 0 &&
            panel_parse_plugin(p, s)) /* success on plugin start */
            i++;
        else /* remove invalid data from config */
            config_setting_remove_elem(list, i);

    /* update backgrond of panel and all plugins */
    _panel_update_background(p);
    return 1;
}

void panel_destroy(Panel *p)
{
    gtk_widget_destroy(GTK_WIDGET(p->topgwin));
}

static LXPanel* panel_new( const char* config_file, const char* config_name )
{
    LXPanel* panel = NULL;

    if (G_LIKELY(config_file))
    {
        panel = panel_allocate();
        panel->priv->name = g_strdup(config_name);
        g_debug("starting panel from file %s",config_file);
        if (!config_read_file(panel->priv->config, config_file) ||
            !panel_start(panel))
        {
            g_warning( "lxpanel: can't start panel");
            gtk_widget_destroy(GTK_WIDGET(panel));
            panel = NULL;
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
//    g_print(_(" --log <number> -- set log level 0-5. 0 - none 5 - chatty\n"));
//    g_print(_(" --configure -- launch configuration utility\n"));
    g_print(_(" --profile name -- use specified profile\n"));
    g_print("\n");
    g_print(_(" -h  -- same as --help\n"));
    g_print(_(" -p  -- same as --profile\n"));
    g_print(_(" -v  -- same as --version\n"));
 //   g_print(_(" -C  -- same as --configure\n"));
    g_print(_("\nVisit http://lxde.org/ for detail.\n\n"));
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
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

    atom = gdk_x11_get_xatom_by_name(CLIPBOARD_NAME);

    XGrabServer(xdisplay);

    if (XGetSelectionOwner(xdisplay, atom) != None)
        goto out;

    clipboard = gtk_clipboard_get(gdk_atom_intern(CLIPBOARD_NAME, FALSE));

    if (gtk_clipboard_set_with_data(clipboard, targets,
                                    G_N_ELEMENTS (targets),
                                    clipboard_get_func,
                                    clipboard_clear_func, NULL))
        retval = TRUE;

out:
    XUngrabServer (xdisplay);
    gdk_flush ();

    return retval;
}
#undef CLIPBOARD_NAME

static void _start_panels_from_dir(const char *panel_dir)
{
    GDir* dir = g_dir_open( panel_dir, 0, NULL );
    const gchar* name;

    if( ! dir )
    {
        return;
    }

    while((name = g_dir_read_name(dir)) != NULL)
    {
        char* panel_config = g_build_filename( panel_dir, name, NULL );
        if (strchr(panel_config, '~') == NULL)    /* Skip editor backup files in case user has hand edited in this directory */
        {
            LXPanel* panel = panel_new( panel_config, name );
            if( panel )
                all_panels = g_slist_prepend( all_panels, panel );
        }
        g_free( panel_config );
    }
    g_dir_close( dir );
}

static gboolean start_all_panels( )
{
    char *panel_dir;
    const gchar * const * dir;

    /* try user panels */
    panel_dir = _user_config_file_name("panels", NULL);
    _start_panels_from_dir(panel_dir);
    g_free(panel_dir);
    if (all_panels != NULL)
        return TRUE;
    /* else try XDG fallbacks */
    dir = g_get_system_config_dirs();
    if (dir) while (dir[0])
    {
        panel_dir = _system_config_file_name(dir[0], "panels");
        _start_panels_from_dir(panel_dir);
        g_free(panel_dir);
        if (all_panels != NULL)
            return TRUE;
        dir++;
    }
    /* last try at old fallback for compatibility reasons */
    panel_dir = _old_system_config_file_name("panels");
    _start_panels_from_dir(panel_dir);
    g_free(panel_dir);
    return all_panels != NULL;
}

void load_global_config();
void free_global_config();

static void _ensure_user_config_dirs(void)
{
    char *dir = g_build_filename(g_get_user_config_dir(), "lxpanel", cprofile,
                                 "panels", NULL);

    /* make sure the private profile and panels dir exists */
    g_mkdir_with_parents(dir, 0700);
    g_free(dir);
}

int main(int argc, char *argv[], char *env[])
{
    int i;
    const char* desktop_name;
    char *file;

    setlocale(LC_CTYPE, "");

    g_thread_init(NULL);
/*    gdk_threads_init();
    gdk_threads_enter(); */

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
                g_critical( "lxpanel: missing log level");
                usage();
                exit(1);
            } else {
                /* deprecated */
            }
        } else if (!strcmp(argv[i], "--configure") || !strcmp(argv[i], "-C")) {
            config = 1;
        } else if (!strcmp(argv[i], "--profile") || !strcmp(argv[i], "-p")) {
            i++;
            if (i == argc) {
                g_critical( "lxpanel: missing profile name");
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

    /* Add a gtkrc file to be parsed too. */
    file = _user_config_file_name("gtkrc", NULL);
    gtk_rc_parse(file);
    g_free(file);

    /* Check for duplicated lxpanel instances */
    if (!check_main_lock() && !config) {
        printf("There is already an instance of LXPanel.  Now to exit\n");
        exit(1);
    }

    _ensure_user_config_dirs();

    /* Add our own icons to the search path of icon theme */
    gtk_icon_theme_append_search_path( gtk_icon_theme_get_default(), PACKAGE_DATA_DIR "/images" );

    fbev = fb_ev_new();
    win_grp = gtk_window_group_new();

    is_restarting = FALSE;

    /* init LibFM */
    fm_gtk_init(NULL);

    /* prepare modules data */
    _prepare_modules();

    load_global_config();

    /* NOTE: StructureNotifyMask is required by XRandR
     * See init_randr_support() in gdkscreen-x11.c of gtk+ for detail.
     */
    gdk_window_set_events(gdk_get_default_root_window(), GDK_STRUCTURE_MASK |
            GDK_SUBSTRUCTURE_MASK | GDK_PROPERTY_CHANGE_MASK);
    gdk_window_add_filter(gdk_get_default_root_window (), (GdkFilterFunc)panel_event_filter, NULL);

    if( G_UNLIKELY( ! start_all_panels() ) )
        g_warning( "Config files are not found.\n" );
/*
 * FIXME: configure??
    if (config)
        configure();
*/
    gtk_main();

    XSelectInput (GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), GDK_ROOT_WINDOW(), NoEventMask);
    gdk_window_remove_filter(gdk_get_default_root_window (), (GdkFilterFunc)panel_event_filter, NULL);

    /* destroy all panels */
    g_slist_foreach( all_panels, (GFunc) gtk_widget_destroy, NULL );
    g_slist_free( all_panels );
    all_panels = NULL;
    g_free( cfgfile );

    free_global_config();

    _unload_modules();
    fm_gtk_finalize();

    /* gdk_threads_leave(); */

    g_object_unref(win_grp);
    g_object_unref(fbev);

    if (!is_restarting)
        return 0;
    if (strchr(argv[0], G_DIR_SEPARATOR))
        execve(argv[0], argv, env);
    else
        execve(g_find_program_in_path(argv[0]), argv, env);
    return 1;
}

GtkOrientation panel_get_orientation(LXPanel *panel)
{
    return panel->priv->orientation;
}

gint panel_get_icon_size(LXPanel *panel)
{
    return panel->priv->icon_size;
}

gint panel_get_height(LXPanel *panel)
{
    return panel->priv->height;
}

Window panel_get_xwindow(LXPanel *panel)
{
    return panel->priv->topxwin;
}

gint panel_get_monitor(LXPanel *panel)
{
    return panel->priv->monitor;
}

GtkStyle *panel_get_defstyle(LXPanel *panel)
{
    return panel->priv->defstyle;
}

GtkIconTheme *panel_get_icon_theme(LXPanel *panel)
{
    return panel->priv->icon_theme;
}

gboolean panel_is_at_bottom(LXPanel *panel)
{
    return panel->priv->edge == EDGE_BOTTOM;
}

gboolean panel_is_dynamic(LXPanel *panel)
{
    return panel->priv->widthtype == WIDTH_REQUEST;
}

GtkWidget *panel_box_new(LXPanel *panel, gboolean homogeneous, gint spacing)
{
    if (panel->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        return gtk_hbox_new(homogeneous, spacing);
    return gtk_vbox_new(homogeneous, spacing);
}

GtkWidget *panel_separator_new(LXPanel *panel)
{
    if (panel->priv->orientation == GTK_ORIENTATION_HORIZONTAL)
        return gtk_vseparator_new();
    return gtk_hseparator_new();
}

gboolean _class_is_present(const LXPanelPluginInit *init)
{
    GSList *sl;

    for (sl = all_panels; sl; sl = sl->next )
    {
        LXPanel *panel = (LXPanel*)sl->data;
        GList *plugins, *p;

        plugins = gtk_container_get_children(GTK_CONTAINER(panel->priv->box));
        for (p = plugins; p; p = p->next)
            if (PLUGIN_CLASS(p->data) == init)
            {
                g_list_free(plugins);
                return TRUE;
            }
        g_list_free(plugins);
    }
    return FALSE;
}
