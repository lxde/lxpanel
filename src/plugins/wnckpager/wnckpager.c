/* pager.c -- pager module of lxpanel project
 *
 * Copyright (C) 2002-2003 Anatoly Asviyan <aanatoly@users.sf.net>
 *                         Joe MacDonald   <joe@deserted.net>
 *
 * This file is part of lxpanel.
 *
 * lxpanel is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * lxpanel is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with sawfish; see the file COPYING.   If not, write to
 * the Free Software Foundation, 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>
#ifndef WNCK_I_KNOW_THIS_IS_UNSTABLE
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#endif
#include <libwnck/libwnck.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

/* managed window: all related info that wm holds about its managed windows */
typedef struct task {
    Window win;
    int x, y;
    guint w, h;
    gint refcount;
    guint stacking;
    guint desktop;
    char *name, *iname;
    int ws;
    NetWMState nws;
    NetWMWindowType nwwt;
    guint focused:1;
} task;

typedef struct _desk   desk;
typedef struct _pager  pager;

#define MAX_DESK_NUM   20
/* map of a desktop */
struct _desk {
    GtkWidget *da;
    GdkPixmap *pix;
    int no, dirty, first;
    gfloat scalew, scaleh;
    pager *pg;
};

struct _pager {
    Plugin* plugin;
    GtkWidget *box, *eb;
    desk *desks[MAX_DESK_NUM];
    guint desknum;
    guint curdesk;
    int dw, dh;
    gfloat scalex, scaley, ratio;
    Window *wins;
    int winnum, dirty;
    GHashTable* htable;
    task *focusedtask;
};


#define TASK_VISIBLE(tk)                            \
 (!( (tk)->nws.hidden || (tk)->nws.skip_pager ))
//if (t->nws.skip_pager || t->nwwt.desktop /*|| t->nwwt.dock || t->nwwt.splash*/ )

static void pager_rebuild_all(FbEv *ev, pager *pg);

static inline void desk_set_dirty_by_win(pager *p, task *t);
static inline void desk_set_dirty(desk *d);
static inline void desk_set_dirty_all(pager *pg);
/*
static void desk_clear_pixmap(desk *d);
static gboolean task_remove_stale(Window *win, task *t, pager *p);
static gboolean task_remove_all(Window *win, task *t, pager *p);
*/




/*****************************************************************
 * Task Management Routines                                      *
 *****************************************************************/


/* tell to remove element with zero refcount */
static gboolean
task_remove_stale(Window *win, task *t, pager *p)
{
    if (t->refcount-- == 0) {
        desk_set_dirty_by_win(p, t);
        if (p->focusedtask == t)
            p->focusedtask = NULL;
        DBG("del %x\n", t->win);
        g_free(t);
        return TRUE;
    }
    return FALSE;
}

/* tell to remove element with zero refcount */
static gboolean
task_remove_all(Window *win, task *t, pager *p)
{
    g_free(t);
    return TRUE;
}


static void
task_get_sizepos(task *t)
{
    Window root, junkwin;
    int rx, ry;
    guint dummy;
    XWindowAttributes win_attributes;

    ENTER;
    if (!XGetWindowAttributes(GDK_DISPLAY(), t->win, &win_attributes)) {
        if (!XGetGeometry (GDK_DISPLAY(), t->win, &root, &t->x, &t->y, &t->w, &t->h,
                  &dummy, &dummy)) {
            t->x = t->y = t->w = t->h = 2;
        }

    } else {
        XTranslateCoordinates (GDK_DISPLAY(), t->win, win_attributes.root,
              -win_attributes.border_width,
              -win_attributes.border_width,
              &rx, &ry, &junkwin);
        t->x = rx;
        t->y = ry;
        t->w = win_attributes.width;
        t->h = win_attributes.height;
        DBG("win=0x%x WxH=%dx%d\n", t->win,t->w, t->h);
    }
    RET();
}


static void
task_update_pix(task *t, desk *d)
{
    int x, y, w, h;
    GtkWidget *widget;
    Panel* p;

    ENTER;
    g_return_if_fail(d->pix != NULL);
    if (!TASK_VISIBLE(t))
        RET();;

    p = d->pg->plugin->panel;
    if (t->desktop < p->desknum &&
          t->desktop != d->no)
        RET();

    x = (gfloat)t->x * d->scalew;
    y = (gfloat)t->y * d->scaleh;
    w = (gfloat)t->w * d->scalew;
    //h = (gfloat)t->h * d->scaleh;
    h = (t->nws.shaded) ? 3 : (gfloat)t->h * d->scaleh;
    if (w < 3 || h < 3)
        RET();
    widget = GTK_WIDGET(d->da);
    gdk_draw_rectangle (d->pix,
          (d->pg->focusedtask == t) ?
          widget->style->bg_gc[GTK_STATE_SELECTED] :
          widget->style->bg_gc[GTK_STATE_NORMAL],
          TRUE,
          x+1, y+1, w-1, h-1);
    gdk_draw_rectangle (d->pix,
          (d->pg->focusedtask == t) ?
          widget->style->fg_gc[GTK_STATE_SELECTED] :
          widget->style->fg_gc[GTK_STATE_NORMAL],
          FALSE,
          x, y, w, h);
    RET();
}


/*****************************************************************
 * Desk Functions                                                *
 *****************************************************************/
static void
desk_clear_pixmap(desk *d)
{
    GtkWidget *widget;

    ENTER;
    DBG("d->no=%d\n", d->no);
    if (!d->pix)
        RET();
    widget = GTK_WIDGET(d->da);
    gdk_draw_rectangle (d->pix,
          ((d->no == d->pg->curdesk) ?
                widget->style->dark_gc[GTK_STATE_SELECTED] :
                widget->style->dark_gc[GTK_STATE_NORMAL]),
          TRUE,
          0, 0,
          widget->allocation.width,
          widget->allocation.height);

    RET();
}



static inline void
desk_set_dirty(desk *d)
{
    ENTER;
    d->dirty = 1;
    gtk_widget_queue_draw(d->da);
    RET();
}

static inline void
desk_set_dirty_all(pager *pg)
{
    int i;
    ENTER;
    for (i = 0; i < pg->desknum; i++)
        desk_set_dirty(pg->desks[i]);
    RET();
}

static inline void
desk_set_dirty_by_win(pager *p, task *t)
{
    ENTER;
    if (t->nws.skip_pager || t->nwwt.desktop /*|| t->nwwt.dock || t->nwwt.splash*/ )
        RET();
    if (t->desktop < p->desknum)
        desk_set_dirty(p->desks[t->desktop]);
    else
        desk_set_dirty_all(p);
    RET();
}

/* Redraw the screen from the backing pixmap */
static gint
desk_expose_event (GtkWidget *widget, GdkEventExpose *event, desk *d)
{
    ENTER;
    DBG("d->no=%d\n", d->no);

    if (d->dirty) {
        pager *pg = d->pg;
        task *t;
        int j;

        d->dirty = 0;
        desk_clear_pixmap(d);
        for (j = 0; j < pg->winnum; j++) {
            if (!(t = g_hash_table_lookup(pg->htable, &pg->wins[j])))
                continue;
            task_update_pix(t, d);
        }
    }
    gdk_draw_drawable(widget->window,
          widget->style->fg_gc[GTK_WIDGET_STATE (widget)],
          d->pix,
          event->area.x, event->area.y,
          event->area.x, event->area.y,
          event->area.width, event->area.height);
    RET(FALSE);
}

/* Upon realize and every resize creates a new backing pixmap of the appropriate size */
static gint
desk_configure_event (GtkWidget *widget, GdkEventConfigure *event, desk *d)
{
    Panel* p;
    int w, h;
    ENTER;
    DBG("d->no=%d %dx%d\n", d->no, widget->allocation.width, widget->allocation.height);
    if (d->pix)
        g_object_unref(d->pix);

    d->pix = gdk_pixmap_new(widget->window,
          widget->allocation.width,
          widget->allocation.height,
          -1);

    d->scalew = (gfloat)widget->allocation.height / (gfloat)gdk_screen_height();
    d->scaleh = (gfloat)widget->allocation.width  / (gfloat)gdk_screen_width();
    desk_set_dirty(d);

    p = d->pg->plugin->panel;
    //request best size
    if (p->orientation != ORIENT_HORIZ) {
        w = widget->allocation.width;
        h = (gfloat) w / d->pg->ratio;
    } else {
        h = widget->allocation.height;
        w = (gfloat) h * d->pg->ratio;
    }
    DBG("requesting %dx%d\n", w, h);
    gtk_widget_set_size_request(widget, w, h);

    RET(FALSE);
}

static gint
desk_button_press_event(GtkWidget * widget, GdkEventButton * event, desk *d)
{
    ENTER;
    if( event->button == 3 ) { /* right button */
        GtkMenu* popup =(GtkMenu*) lxpanel_get_panel_menu
                ( d->pg->plugin->panel, d->pg->plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, event->button, event->time );
        return TRUE;
    }

    Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, d->no, 0, 0, 0, 0);
    RET(TRUE);
}

/*
static gint
desk_button_release_event(GtkWidget * widget, GdkEventButton * event, desk *d)
{
    ENTER;
    DBG("t=%d\n", d->no);
    Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, d->no, 0, 0, 0, 0);
    RET(TRUE);
}
*/

static gint
desk_scroll_event (GtkWidget *widget, GdkEventScroll *event, desk *d)
{
    int i;

    ENTER;
    DBG("scroll direction = %d\n", event->direction);
    i = d->pg->curdesk;
    if (event->direction == GDK_SCROLL_UP ||event->direction == GDK_SCROLL_LEFT) {
        i--;
        if (i < 0)
            i = d->pg->desknum - 1;
    } else {
        i++;
        if (i >= d->pg->desknum)
            i = 0;
    }
    Xclimsg(GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, i, 0, 0, 0, 0);
    RET(TRUE);
}

static void
desk_new(pager *pg, int i)
{
    desk *d;

    ENTER;
    g_assert(i < pg->desknum);
    d = pg->desks[i] = g_new0(desk, 1);
    d->pg = pg;
    d->pix = NULL;
    d->dirty = 0;
    d->first = 1;
    d->no = i;

    d->da = gtk_drawing_area_new();
    //gtk_widget_set_size_request(GTK_WIDGET(d->da), 10, 10);
    gtk_box_pack_start(GTK_BOX(pg->box), d->da, TRUE, TRUE, 0);
    gtk_widget_add_events (d->da, GDK_EXPOSURE_MASK
          | GDK_BUTTON_PRESS_MASK
          | GDK_BUTTON_RELEASE_MASK);
    g_signal_connect (G_OBJECT (d->da), "expose_event",
          (GCallback) desk_expose_event, (gpointer)d);
    g_signal_connect (G_OBJECT (d->da), "configure_event",
          (GCallback) desk_configure_event, (gpointer)d);
    g_signal_connect (G_OBJECT (d->da), "scroll-event",
          (GCallback) desk_scroll_event, (gpointer)d);
    g_signal_connect (G_OBJECT (d->da), "button_press_event",
         (GCallback) desk_button_press_event, (gpointer)d);
    //g_signal_connect (G_OBJECT (d->da), "button_release_event",
    //     (GCallback) desk_button_release_event, (gpointer)d);
    gtk_widget_show(d->da);
    DBG("before pack\n");

    DBG("after show\n");
    RET();
}

static void
desk_free(pager *pg, int i)
{
    desk *d;

    ENTER;
    d = pg->desks[i];
    DBG("i=%d d->no=%d d->da=%p d->pix=%p\n",
          i, d->no, d->da, d->pix);
    if (d->pix)
        g_object_unref(d->pix);
    gtk_widget_destroy(d->da);
    g_free(d);
    RET();
}


/*****************************************************************
 * Netwm/WM Interclient Communication                            *
 *****************************************************************/

static void
do_net_active_window(FbEv *ev, pager *p)
{
    Window *fwin;
    task *t;

    ENTER;
    fwin = get_xaproperty(GDK_ROOT_WINDOW(), a_NET_ACTIVE_WINDOW, XA_WINDOW, 0);
    DBG("win=%x\n", fwin ? *fwin : 0);
    if (fwin) {
        t = g_hash_table_lookup(p->htable, fwin);
        if (t != p->focusedtask) {
            if (p->focusedtask)
                desk_set_dirty_by_win(p, p->focusedtask);
            p->focusedtask = t;
            if (t)
                desk_set_dirty_by_win(p, t);
        }
        XFree(fwin);
    } else {
        if (p->focusedtask) {
            desk_set_dirty_by_win(p, p->focusedtask);
            p->focusedtask = NULL;
        }
    }
    RET();
}

static void
do_net_current_desktop(FbEv *ev, pager *p)
{
    ENTER;
    desk_set_dirty(p->desks[p->curdesk]);
    p->curdesk =  get_net_current_desktop ();
    if (p->curdesk >= p->desknum)
        p->curdesk = 0;
    desk_set_dirty(p->desks[p->curdesk]);
    RET();
}


static void
do_net_client_list_stacking(FbEv *ev, pager *p)
{
    int i;
    task *t;

    ENTER;
    if (p->wins)
        XFree(p->wins);
    p->wins = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST_STACKING,
          XA_WINDOW, &p->winnum);
    if (!p->wins || !p->winnum)
        RET();

    /* refresh existing tasks and add new */
    for (i = 0; i < p->winnum; i++) {
        if ((t = g_hash_table_lookup(p->htable, &p->wins[i]))) {
            t->refcount++;
            if (t->stacking != i) {
                t->stacking = i;
                desk_set_dirty_by_win(p, t);
            }
        } else {
            t = g_new0(task, 1);
            t->refcount++;
            t->win = p->wins[i];
            t->ws = get_wm_state (t->win);
            t->desktop = get_net_wm_desktop(t->win);
            get_net_wm_state(t->win, &t->nws);
            get_net_wm_window_type(t->win, &t->nwwt);
            task_get_sizepos(t);
            if (!FBPANEL_WIN(t->win))
                XSelectInput (GDK_DISPLAY(), t->win, PropertyChangeMask | StructureNotifyMask);
            g_hash_table_insert(p->htable, &t->win, t);
            DBG("add %x\n", t->win);
            desk_set_dirty_by_win(p, t);
        }
    }
    /* pass throu hash table and delete stale windows */
    g_hash_table_foreach_remove(p->htable, (GHRFunc) task_remove_stale, (gpointer)p);
    RET();
}


/*****************************************************************
 * Pager Functions                                               *
 *****************************************************************/
/*
static void
pager_unmapnotify(pager *p, XEvent *ev)
{
    Window win = ev->xunmap.window;
    task *t;
    if (!(t = g_hash_table_lookup(p->htable, &win)))
        RET();
    DBG("pager_unmapnotify: win=0x%x\n", win);
    RET();
    t->ws = WithdrawnState;
    desk_set_dirty_by_win(p, t);
    RET();
}
*/
static void
pager_configurenotify(pager *p, XEvent *ev)
{
    Window win = ev->xconfigure.window;
    task *t;

    ENTER;

    if (!(t = g_hash_table_lookup(p->htable, &win)))
        RET();
    DBG("win=0x%x\n", win);
    task_get_sizepos(t);
    desk_set_dirty_by_win(p, t);
    RET();
}

static void
pager_propertynotify(pager *p, XEvent *ev)
{
    Atom at = ev->xproperty.atom;
    Window win = ev->xproperty.window;
    task *t;

    ENTER;
    if ((win == GDK_ROOT_WINDOW()) || !(t = g_hash_table_lookup(p->htable, &win)))
        RET();

    /* The property is deleted */
    if( ((XPropertyEvent*)ev)->state == 1 )
        return;

    DBG("window=0x%x\n", t->win);
    if (at == a_WM_STATE)    {
        DBG("event=WM_STATE\n");
        t->ws = get_wm_state (t->win);
    } else if (at == a_NET_WM_STATE) {
        DBG("event=NET_WM_STATE\n");
        get_net_wm_state(t->win, &t->nws);
    } else if (at == a_NET_WM_DESKTOP) {
        DBG("event=NET_WM_DESKTOP\n");
        desk_set_dirty_by_win(p, t); // to clean up desks where this task was
        t->desktop = get_net_wm_desktop(t->win);
    } else {
        RET();
    }
    desk_set_dirty_by_win(p, t);
    RET();
}

static GdkFilterReturn
pager_event_filter( XEvent *xev, GdkEvent *event, pager *pg)
{
    ENTER;
    if (xev->type == PropertyNotify )
        pager_propertynotify(pg, xev);
    else if (xev->type == ConfigureNotify )
        pager_configurenotify(pg, xev);
    RET(GDK_FILTER_CONTINUE);
}





static void
pager_rebuild_all(FbEv *ev, pager *pg)
{
    int desknum, curdesk, dif, i;

    ENTER;
    desknum = pg->desknum;
    curdesk = pg->curdesk;

    pg->desknum = get_net_number_of_desktops();
    if (pg->desknum < 1)
        pg->desknum = 1;
    else if (pg->desknum > MAX_DESK_NUM) {
        pg->desknum = MAX_DESK_NUM;
        ERR("pager: max number of supported desks is %d\n", MAX_DESK_NUM);
    }
    pg->curdesk = get_net_current_desktop();
    if (pg->curdesk >= pg->desknum)
        pg->curdesk = 0;
    DBG("desknum=%d curdesk=%d\n", desknum, curdesk);
    DBG("pg->desknum=%d pg->curdesk=%d\n", pg->desknum, pg->curdesk);
    dif = pg->desknum - desknum;

    if (dif == 0)
        RET();

    if (dif < 0) {
        /* if desktops were deleted then delete their maps also */
        for (i = pg->desknum; i < desknum; i++)
            desk_free(pg, i);
    } else {
        for (i = desknum; i < pg->desknum; i++)
            desk_new(pg, i);
    }
    do_net_client_list_stacking(NULL, pg);
    RET();
}


static int
pager_wnck_constructor(Plugin *plug, char **fp)
{
    pager *pg;

    ENTER;
    pg = g_new0(pager, 1);
    g_return_val_if_fail(pg != NULL, 0);
    plug->priv = pg;
    pg->plugin = plug;

    plug->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( plug->pwid, GTK_NO_WINDOW );

    pg->htable = g_hash_table_new (g_int_hash, g_int_equal);

    pg->box = wnck_pager_new(NULL);
    g_return_val_if_fail(pg->box != NULL, 0);
    //set orientation
    wnck_pager_set_orientation (WNCK_PAGER (pg->box),pg->plugin->panel->orientation);
    wnck_pager_set_n_rows (WNCK_PAGER (pg->box), 1); //pager->rows);
    wnck_pager_set_display_mode (WNCK_PAGER (pg->box),WNCK_PAGER_DISPLAY_CONTENT);
    //pager->show_names ? WNCK_PAGER_DISPLAY_NAME : WNCK_PAGER_DISPLAY_CONTENT);
    //gtk_widget_show (pg->box);
    //gtk_container_add (GTK_CONTAINER (plugin), pg->box);

    gtk_container_set_border_width (GTK_CONTAINER (pg->box), 2);
    gtk_widget_show(pg->box);

    gtk_container_set_border_width (GTK_CONTAINER (plug->pwid), 1);
    gtk_container_add(GTK_CONTAINER(plug->pwid), pg->box);
    pg->eb = pg->box;

    pg->ratio = (gfloat)gdk_screen_width() / (gfloat)gdk_screen_height();
    pg->scaley = (gfloat)pg->dh / (gfloat)gdk_screen_height();
    pg->scalex = (gfloat)pg->dw / (gfloat)gdk_screen_width();

    pager_rebuild_all(fbev, pg);
    //do_net_current_desktop(fbev, pg);
    //do_net_client_list_stacking(fbev, pg);

    gdk_window_add_filter(NULL, (GdkFilterFunc)pager_event_filter, pg );

    g_signal_connect (G_OBJECT (fbev), "current_desktop",
          G_CALLBACK (do_net_current_desktop), (gpointer) pg);
    g_signal_connect (G_OBJECT (fbev), "active_window",
          G_CALLBACK (do_net_active_window), (gpointer) pg);
    g_signal_connect (G_OBJECT (fbev), "number_of_desktops",
          G_CALLBACK (pager_rebuild_all), (gpointer) pg);
    g_signal_connect (G_OBJECT (fbev), "client_list_stacking",
          G_CALLBACK (do_net_client_list_stacking), (gpointer) pg);
    RET(1);
}

static void
pager_destructor(Plugin *p)
{
    pager *pg = (pager *)p->priv;

    ENTER;
    g_signal_handlers_disconnect_by_func(G_OBJECT (fbev), do_net_current_desktop, pg);
    g_signal_handlers_disconnect_by_func(G_OBJECT (fbev), do_net_active_window, pg);
    g_signal_handlers_disconnect_by_func(G_OBJECT (fbev), pager_rebuild_all, pg);
    g_signal_handlers_disconnect_by_func(G_OBJECT (fbev), do_net_client_list_stacking, pg);
    gdk_window_remove_filter(NULL, (GdkFilterFunc)pager_event_filter, pg);
    while (--pg->desknum) {
        desk_free(pg, pg->desknum);
    }
    g_hash_table_foreach_remove(pg->htable, (GHRFunc) task_remove_all, (gpointer)pg);
    g_hash_table_destroy(pg->htable);
    gtk_widget_destroy(pg->eb);
    g_free(pg);
    RET();
}


PluginClass wnckpager_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "wnckpager",
    name : N_("WNCKPager"),
    version: "1.0",
    description : N_("WNCKpager plugin"),

    /* FIXME: orientation should be handled!! */
    constructor : pager_wnck_constructor,
    destructor  : pager_destructor,
    config : NULL,
    save : NULL,
    panel_configuration_changed : NULL
};
