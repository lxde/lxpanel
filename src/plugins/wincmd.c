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

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "dbg.h"

typedef struct {
    GtkTooltips *tips;
    char* image;
    int button1, button2;
    int action1, action2;
} wincmd;

enum { WC_NONE = 1, WC_ICONIFY, WC_SHADE };
static pair wincmd_pair [] = {
    { WC_NONE,    "none" },
    { WC_ICONIFY, "iconify" },
    { WC_SHADE,   "shade" },
    { 0, NULL },
};

static void
toggle_shaded(wincmd *wc, guint32 action)
{
    Window *win = NULL;
    int num, i;
    guint32 tmp2, dno;
    NetWMWindowType nwwt;

    ENTER;
    win = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST, XA_WINDOW, &num);
    if (!win)
    RET();
    if (!num)
        goto end;
    //tmp = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, XA_CARDINAL, 0);
    //dno = *tmp;
    dno = get_net_current_desktop();
    DBG("wincmd: #desk=%d\n", dno);
    //XFree(tmp);
    for (i = 0; i < num; i++) {
        int skip;

        tmp2 = get_net_wm_desktop(win[i]);
        DBG("wincmd: win=0x%x dno=%d...", win[i], tmp2);
        if ((tmp2 != -1) && (tmp2 != dno)) {
            DBG("skip - not cur desk\n");
            continue;
        }
        get_net_wm_window_type(win[i], &nwwt);
        skip = (nwwt.dock || nwwt.desktop || nwwt.splash);
        if (skip) {
            DBG("skip - omnipresent window type\n");
            continue;
        }
        Xclimsg(win[i], a_NET_WM_STATE,
              action ? a_NET_WM_STATE_ADD : a_NET_WM_STATE_REMOVE,
              a_NET_WM_STATE_SHADED, 0, 0, 0);
        DBG("ok\n");
    }

 end:
    XFree(win);
    RET();
}



static void
toggle_iconify(wincmd *wc, guint32 action)
{
    Window *win = NULL;
    int num, i;
    guint32 tmp2, dno;
    NetWMWindowType nwwt;

    ENTER;
    win = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST, XA_WINDOW, &num);
    if (!win)
    RET();
    if (!num)
        goto end;
    //tmp = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, XA_CARDINAL, 0);
    dno = get_net_current_desktop();
    DBG("wincmd: #desk=%d\n", dno);
    //XFree(tmp);
    for (i = 0; i < num; i++) {
        int skip;

        tmp2 = get_net_wm_desktop(win[i]);
        DBG("wincmd: win=0x%x dno=%d...", win[i], tmp2);
        if ((tmp2 != -1) && (tmp2 != dno)) {
            DBG("skip - not cur desk\n");
            continue;
        }
        get_net_wm_window_type(win[i], &nwwt);
        skip = (nwwt.dock || nwwt.desktop || nwwt.splash);
        if (skip) {
            DBG("skip - omnipresent window type\n");
            continue;
        }
        if (action)
            XIconifyWindow(GDK_DISPLAY(), win[i], DefaultScreen(GDK_DISPLAY()));
        else
            XMapWindow (GDK_DISPLAY(), win[i]);
        DBG("ok\n");
    }

 end:
    XFree(win);
    RET();
}

static gint
clicked (GtkWidget *widget, GdkEventButton *event, Plugin* plugin)
{
    wincmd *wc = (wincmd *)plugin->priv;

    ENTER;
    if (event->type != GDK_BUTTON_PRESS)
        RET(FALSE);

    ENTER2;

    if (event->button == 1) {
        wc->action1 = 1 - wc->action1;
        toggle_iconify(wc, wc->action1);
        DBG("wincmd: iconify all\n");
    } else if (event->button == 2) {
        wc->action2 = 1 - wc->action2;
        toggle_shaded(wc, wc->action2);
        DBG("wincmd: shade all\n");
    } else if( event->button == 3 ) { /* right button */
        GtkMenu* popup = lxpanel_get_panel_menu( plugin->panel, plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, event->button, event->time );
        return TRUE;
    }
    RET(FALSE);
}

static void
wincmd_destructor(Plugin *p)
{
    wincmd *wc = (wincmd *)p->priv;

    ENTER;
    g_free( wc->image );
    /* g_object_unref( wc->tips ); */
    g_free(wc);
    RET();
}



static int
wincmd_constructor(Plugin *p, char **fp)
{
    line s;
    gchar *fname;
    wincmd *wc;
    //GdkPixbuf *gp, *gps;
    GtkWidget *button;
    int w, h;

    ENTER;
    s.len = 256;
    wc = g_new0(wincmd, 1);
    g_return_val_if_fail(wc != NULL, 0);

    wc->tips = p->panel->tooltips;
/*
    wc->tips = gtk_tooltips_new();
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( wc->tips );
#else
    g_object_ref( wc->tips );
    gtk_object_sink( wc->tips );
#endif
*/
    p->priv = wc;
    fname = NULL;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "wincmd: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "Button1"))
                    wc->button1 = str2num(wincmd_pair, s.t[1], WC_ICONIFY);
                else if (!g_ascii_strcasecmp(s.t[0], "Button2"))
                    wc->button2 = str2num(wincmd_pair, s.t[1], WC_SHADE);
                else if (!g_ascii_strcasecmp(s.t[0], "image")) {
                    wc->image = g_strdup( s.t[1] );
                    fname = expand_tilda(s.t[1]);
                }
                else {
                    ERR( "wincmd: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "wincmd: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }
    else
    {
        wc->button1 = WC_ICONIFY;
        wc->button2 = WC_SHADE;
    }

    if( !fname ){
        fname = g_strdup("gnome-fs-desktop");
    }

    if (p->panel->orientation == ORIENT_HORIZ) {
        w = 10000;
        h = p->panel->ah;
    } else {
        w = p->panel->aw;
        h = 10000;
    }
    button = fb_button_new_from_file(fname, w, h, 0x202020, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(button), 0);
    g_signal_connect(G_OBJECT(button), "button_press_event",
          G_CALLBACK(clicked), (gpointer)p);

    gtk_widget_show(button);

    g_free(fname);
    gtk_tooltips_set_tip(GTK_TOOLTIPS (wc->tips), button, _("Left click to iconify all windows. Middle click to shade them"), NULL);

    /* store the created plugin widget in plugin->pwid */
    p->pwid = button;

    RET(1);

 error:
    g_free(fname);
    wincmd_destructor(p);
    ERR( "%s - exit\n", __FUNCTION__);
    RET(0);
}

static void save_config( Plugin* p, FILE* fp )
{
    wincmd* wc = (wincmd*)p->priv;

    lxpanel_put_str( fp, "image", wc->image );
    lxpanel_put_str( fp, "Button1", num2str(wincmd_pair, wc->button1, NULL) );
    lxpanel_put_str( fp, "Button2", num2str(wincmd_pair, wc->button2, NULL) );
}


PluginClass wincmd_plugin_class = {
    fname: NULL,
    count: 0,

    type : "wincmd",
    name : N_("Minimize All Windows"),
    version: "1.0",
    description : N_("Sends commands to all desktop windows.\nSupported commnds are 1)toggle iconify and 2) toggle shade"),

    constructor : wincmd_constructor,
    destructor  : wincmd_destructor,
    config : NULL,
    save : save_config
};
