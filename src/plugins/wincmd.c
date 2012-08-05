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

/* Commands that can be issued. */
typedef enum {
    WC_NONE,
    WC_ICONIFY,
    WC_SHADE
} WindowCommand;

/* Private context for window command plugin. */
typedef struct {
    char * image;				/* Main icon */
    WindowCommand button_1_command;		/* Command for mouse button 1 */
    WindowCommand button_2_command;		/* Command for mouse button 2 */
    gboolean toggle_preference;			/* User preference: toggle iconify/shade and map */
    gboolean toggle_state;			/* State of toggle */
} WinCmdPlugin;

static pair wincmd_pair [] = {
    { WC_NONE,    "none" },
    { WC_ICONIFY, "iconify" },
    { WC_SHADE,   "shade" },
    { 0, NULL },
};

static void wincmd_adjust_toggle_state(WinCmdPlugin * wc);
static void wincmd_execute(WinCmdPlugin * wc, WindowCommand command);
static gboolean wincmd_button_clicked(GtkWidget * widget, GdkEventButton * event, Plugin * plugin);
static int wincmd_constructor(Plugin * p, char ** fp);
static void wincmd_destructor(Plugin * p);
static void wincmd_apply_configuration(Plugin * p);
static void wincmd_configure(Plugin * p, GtkWindow * parent);
static void wincmd_save_configuration(Plugin * p, FILE * fp);
static void wincmd_panel_configuration_changed(Plugin * p);

/* Adjust the toggle state after a window command. */
static void wincmd_adjust_toggle_state(WinCmdPlugin * wc)
{
    /* Ensure that if the user changes the preference from "unconditional" to "toggle", we do a raise on the next click. */
    if (wc->toggle_preference)
        wc->toggle_state = ! wc->toggle_state;
        else wc->toggle_state = TRUE;
}

/* Execute a window command. */
static void wincmd_execute(WinCmdPlugin * wc, WindowCommand command)
{
    /* Get the list of all windows. */
    int client_count;
    Window * client_list = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST, XA_WINDOW, &client_count);
    if (client_list != NULL)
    {
        /* Loop over all windows. */
        guint current_desktop = get_net_current_desktop();
        int i;
        for (i = 0; i < client_count; i++)
        {
            /* Get the desktop and window type properties. */
            NetWMWindowType nwwt;
            guint task_desktop = get_net_wm_desktop(client_list[i]);
            get_net_wm_window_type(client_list[i], &nwwt);

            /* If the task is visible on the current desktop and it is an ordinary window,
             * execute the requested Iconify or Shade change. */
            if (((task_desktop == -1) || (task_desktop == current_desktop))
            && (( ! nwwt.dock) && ( ! nwwt.desktop) && ( ! nwwt.splash)))
            {
                switch (command)
                {
                    case WC_NONE:
                        break;

                    case WC_ICONIFY:
                        if (( ! wc->toggle_preference) || ( ! wc->toggle_state))
                            XIconifyWindow(GDK_DISPLAY(), client_list[i], DefaultScreen(GDK_DISPLAY()));
                        else
                            XMapWindow (GDK_DISPLAY(), client_list[i]);
                        break;

                    case WC_SHADE:
                        Xclimsg(client_list[i], a_NET_WM_STATE,
                            ((( ! wc->toggle_preference) || ( ! wc->toggle_state)) ? a_NET_WM_STATE_ADD : a_NET_WM_STATE_REMOVE),
                            a_NET_WM_STATE_SHADED, 0, 0, 0);
                        break;
                }
            }
        }
        XFree(client_list);

	/* Adjust toggle state. */
        wincmd_adjust_toggle_state(wc);
    }
}

/* Handler for "clicked" signal on main widget. */
static gboolean wincmd_button_clicked(GtkWidget * widget, GdkEventButton * event, Plugin * plugin)
{
    WinCmdPlugin * wc = (WinCmdPlugin *) plugin->priv;

    /* Standard right-click handling. */
    if (plugin_button_press_event(widget, event, plugin))
        return TRUE;

    /* Left-click to iconify. */
    if (event->button == 1)
    {
        GdkScreen* screen = gtk_widget_get_screen(widget);
        static GdkAtom atom = 0;
        if( G_UNLIKELY(0 == atom) )
            atom = gdk_atom_intern("_NET_SHOWING_DESKTOP", FALSE);

        /* If window manager supports _NET_SHOWING_DESKTOP, use it.
         * Otherwise, fall back to iconifying windows individually. */
        if (gdk_x11_screen_supports_net_wm_hint(screen, atom))
        {
            int showing_desktop = ((( ! wc->toggle_preference) || ( ! wc->toggle_state)) ? 1 : 0);
            Xclimsg(DefaultRootWindow(GDK_DISPLAY()), a_NET_SHOWING_DESKTOP, showing_desktop, 0, 0, 0, 0);
            wincmd_adjust_toggle_state(wc);
        }
        else
            wincmd_execute(wc, WC_ICONIFY);
    }

    /* Middle-click to shade. */
    else if (event->button == 2)
        wincmd_execute(wc, WC_SHADE);

    return TRUE;
}

/* Plugin constructor. */
static int wincmd_constructor(Plugin * p, char ** fp)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    WinCmdPlugin * wc = g_new0(WinCmdPlugin, 1);
    p->priv = wc;

    /* Initialize to defaults. */
    wc->button_1_command = WC_ICONIFY;
    wc->button_2_command = WC_SHADE;

    /* Load parameters from the configuration file. */
    line s;
    s.len = 256;
    if(fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR("wincmd: illegal token %s\n", s.str);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "Button1") == 0)
                    wc->button_1_command = str2num(wincmd_pair, s.t[1], WC_ICONIFY);
                else if (g_ascii_strcasecmp(s.t[0], "Button2") == 0)
                    wc->button_2_command = str2num(wincmd_pair, s.t[1], WC_SHADE);
                else if (g_ascii_strcasecmp(s.t[0], "image") == 0)
                    wc->image = expand_tilda(g_strdup(s.t[1]));
                else if (g_ascii_strcasecmp(s.t[0], "Toggle") == 0)
                    wc->toggle_preference = str2num(bool_pair, s.t[1], 0);
                else
                    ERR("wincmd: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR("wincmd: illegal in this context %s\n", s.str);
                return 0;
            }
        }
    }

    /* Default the image if unspecified. */
    if (wc->image == NULL)
        wc->image = g_strdup("window-manager");

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = fb_button_new_from_file(wc->image, p->panel->icon_size, p->panel->icon_size, PANEL_ICON_HIGHLIGHT, TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(p->pwid), 0);
    g_signal_connect(G_OBJECT(p->pwid), "button_press_event", G_CALLBACK(wincmd_button_clicked), (gpointer) p);
    gtk_widget_set_tooltip_text(p->pwid, _("Left click to iconify all windows.  Middle click to shade them."));

    /* Show the widget and return. */
    gtk_widget_show(p->pwid);
    return 1;
}

/* Plugin destructor. */
static void wincmd_destructor(Plugin * p)
{
    WinCmdPlugin * wc = (WinCmdPlugin *) p->priv;
    g_free(wc->image);
    g_free(wc);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static void wincmd_apply_configuration(Plugin * p)
{
}

/* Callback when the configuration dialog is to be shown. */
static void wincmd_configure(Plugin * p, GtkWindow * parent)
{
    WinCmdPlugin * wc = (WinCmdPlugin *) p->priv;
    GtkWidget * dlg = create_generic_config_dlg(
        _(p->class->name),
        GTK_WIDGET(parent),
        (GSourceFunc) wincmd_apply_configuration, (gpointer) p,
        _("Alternately iconify/shade and raise"), &wc->toggle_preference, CONF_TYPE_BOOL,
        NULL);
    gtk_window_present(GTK_WINDOW(dlg));
}


/* Save the configuration to the configuration file. */
static void wincmd_save_configuration(Plugin * p, FILE * fp)
{
    WinCmdPlugin * wc = (WinCmdPlugin *) p->priv;
    lxpanel_put_str(fp, "image", wc->image);
    lxpanel_put_str(fp, "Button1", num2str(wincmd_pair, wc->button_1_command, NULL));
    lxpanel_put_str(fp, "Button2", num2str(wincmd_pair, wc->button_2_command, NULL));
    lxpanel_put_bool(fp, "Toggle", wc->toggle_preference);
}

/* Callback when panel configuration changes. */
static void wincmd_panel_configuration_changed(Plugin * p)
{
    WinCmdPlugin * wc = (WinCmdPlugin *) p->priv;
	fb_button_set_from_file(p->pwid, wc->image, p->panel->icon_size, p->panel->icon_size, TRUE);
}

/* Plugin descriptor. */
PluginClass wincmd_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "wincmd",
    name : N_("Minimize All Windows"),
    version: "1.0",
    description : N_("Sends commands to all desktop windows.\nSupported commands are 1) iconify and 2) shade"),

    constructor : wincmd_constructor,
    destructor  : wincmd_destructor,
    config : wincmd_configure,
    save : wincmd_save_configuration,
    panel_configuration_changed : wincmd_panel_configuration_changed

};
