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

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <string.h>

#include <X11/XKBlib.h>

#include "dbg.h"
#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "icon-grid.h"

static const char * on_icons_theme[] = {
    "capslock-on",
    "numlock-on",
    "scrllock-on"
};

static const char * off_icons_theme[] = {
    "capslock-off",
    "numlock-off",
    "scrllock-off"
};

static const char * on_icons[] = {
    "capslock-on.png",
    "numlock-on.png",
    "scrllock-on.png"
};

static const char * off_icons[] = {
    "capslock-off.png",
    "numlock-off.png",
    "scrllock-off.png"
};

static int xkb_event_base = 0;
static int xkb_error_base = 0;

/* Private context for keyboard LED plugin. */
typedef struct {
    Plugin * plugin;				/* Back pointer to plugin */
    IconGrid * icon_grid;			/* Icon grid manager */
    GtkWidget *indicator_image[3];		/* Image for each indicator */
    unsigned int current_state;			/* Current LED state, bit encoded */
    gboolean visible[3];			/* True if control is visible (per user configuration) */
} KeyboardLEDPlugin;

static void kbled_theme_changed(GtkWidget * widget, Plugin * p);
static void kbled_update_image(KeyboardLEDPlugin * kl, int i, unsigned int state);
static void kbled_update_display(Plugin * p, unsigned int state);
static GdkFilterReturn kbled_event_filter(GdkXEvent * gdkxevent, GdkEvent * event, Plugin * p);
static int kbled_constructor(Plugin * p, char ** fp);
static void kbled_destructor(Plugin * p);
static void kbled_apply_configuration(Plugin * p);
static void kbled_configure(Plugin * p, GtkWindow * parent);
static void kbled_save_configuration(Plugin * p, FILE * fp);
static void kbled_panel_configuration_changed(Plugin * p);

static void kbled_theme_changed(GtkWidget * widget, Plugin * p)
{
    /* Set orientation into the icon grid. */
    KeyboardLEDPlugin * kl = (KeyboardLEDPlugin *) p->priv;

    /* Do a full redraw. */
    int current_state = kl->current_state;
    kl->current_state = ~ kl->current_state;
    kbled_update_display(p, current_state);
}

/* Update image to correspond to current state. */
static void kbled_update_image(KeyboardLEDPlugin * kl, int i, unsigned int state)
{
	if(panel_image_set_icon_theme(kl->plugin->panel, kl->indicator_image[i], (state ? on_icons_theme[i] : off_icons_theme[i])) != TRUE) {
		char * file = g_build_filename(
			PACKAGE_DATA_DIR "/lxpanel/images",
			((state) ? on_icons[i] : off_icons[i]),
			NULL);
		panel_image_set_from_file(kl->plugin->panel, kl->indicator_image[i], file);
		g_free(file);
		
	}
}

/* Redraw after Xkb event or initialization. */
static void kbled_update_display(Plugin * p, unsigned int new_state)
{
    KeyboardLEDPlugin * kl = (KeyboardLEDPlugin *) p->priv;
    int i;
    for (i = 0; i < 3; i++)
    {
        /* If the control changed state, redraw it. */
        int current_is_lit = kl->current_state & (1 << i);
        int new_is_lit = new_state & (1 << i);
        if (current_is_lit != new_is_lit)
            kbled_update_image(kl, i, new_is_lit);
    }

    /* Save new state. */
    kl->current_state = new_state;
}

/* GDK event filter. */
static GdkFilterReturn kbled_event_filter(GdkXEvent * gdkxevent, GdkEvent * event, Plugin * p)
{
    /* Look for XkbIndicatorStateNotify events and update the display. */
    XEvent * xev = (XEvent *) gdkxevent;
    if (xev->xany.type == xkb_event_base + XkbEventCode)
    {
        XkbEvent * xkbev = (XkbEvent *) xev;
        if (xkbev->any.xkb_type == XkbIndicatorStateNotify)
            kbled_update_display(p, xkbev->indicators.state);
    }
    return GDK_FILTER_CONTINUE;
}

/* Plugin constructor. */
static int kbled_constructor(Plugin * p, char ** fp)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    KeyboardLEDPlugin * kl = g_new0(KeyboardLEDPlugin, 1);
    kl->plugin = p;
    kl->visible[0] = FALSE;
    kl->visible[1] = TRUE;
    kl->visible[2] = TRUE;
    p->priv = kl;

    /* Load parameters from the configuration file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "kbled: illegal token %s\n", s.str);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "ShowCapsLock") == 0)
                    kl->visible[0] = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "ShowNumLock") == 0)
                    kl->visible[1] = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "ShowScrollLock") == 0)
                    kl->visible[2] = str2num(bool_pair, s.t[1], 0);
                else
                    ERR("kbled: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR("kbled: illegal in this context %s\n", s.str);
                return 0;
            }
        }
    }

    /* Allocate top level widget and set into Plugin widget pointer. */
    p->pwid = gtk_event_box_new();
    gtk_widget_add_events(p->pwid, GDK_BUTTON_PRESS_MASK);
    g_signal_connect(p->pwid, "button-press-event", G_CALLBACK(plugin_button_press_event), p);
    g_signal_connect(p->panel->icon_theme, "changed", G_CALLBACK(kbled_theme_changed), p);

    /* Allocate an icon grid manager to manage the container.
     * Then allocate three images for the three indications, but make them visible only when the configuration requests. */
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    kl->icon_grid = icon_grid_new(p->panel, p->pwid, bo, p->panel->icon_size, p->panel->icon_size, 0, 0, p->panel->height); 
    int i;
    for (i = 0; i < 3; i++)
    {
        kl->indicator_image[i] = gtk_image_new();
        icon_grid_add(kl->icon_grid, kl->indicator_image[i], kl->visible[i]);
    }

    /* Initialize Xkb extension if not yet done. */
    if (xkb_event_base == 0)
    {
        int opcode;
        int maj = XkbMajorVersion;
        int min = XkbMinorVersion;
        if ( ! XkbLibraryVersion(&maj, &min))
            return 0;
        if ( ! XkbQueryExtension(GDK_DISPLAY(), &opcode, &xkb_event_base, &xkb_error_base, &maj, &min))
            return 0;
    }

    /* Add GDK event filter and enable XkbIndicatorStateNotify events. */
    gdk_window_add_filter(NULL, (GdkFilterFunc) kbled_event_filter, p);
    if ( ! XkbSelectEvents(GDK_DISPLAY(), XkbUseCoreKbd, XkbIndicatorStateNotifyMask, XkbIndicatorStateNotifyMask))
        return 0;

    /* Get current indicator state and update display.
     * Force current state to differ in all bits so a full redraw will occur. */
    unsigned int current_state;
    XkbGetIndicatorState(GDK_DISPLAY(), XkbUseCoreKbd, &current_state);
    kl->current_state = ~ current_state;
    kbled_update_display(p, current_state);

    /* Show the widget. */
    gtk_widget_show(p->pwid);
    return 1;
}

/* Plugin destructor. */
static void kbled_destructor(Plugin * p)
{
    KeyboardLEDPlugin * kl = (KeyboardLEDPlugin *) p->priv;

    /* Remove GDK event filter. */
    gdk_window_remove_filter(NULL, (GdkFilterFunc) kbled_event_filter, p);
    icon_grid_free(kl->icon_grid);
    g_free(kl);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static void kbled_apply_configuration(Plugin * p)
{
    KeyboardLEDPlugin * kl = (KeyboardLEDPlugin *) p->priv;
    int i;
    for (i = 0; i < 3; i++)
        icon_grid_set_visible(kl->icon_grid, kl->indicator_image[i], kl->visible[i]);
}

/* Callback when the configuration dialog is to be shown. */
static void kbled_configure(Plugin * p, GtkWindow * parent)
{
    KeyboardLEDPlugin * kl = (KeyboardLEDPlugin *) p->priv;
    GtkWidget * dlg = create_generic_config_dlg(
        _(p->class->name),
        GTK_WIDGET(parent),
        (GSourceFunc) kbled_apply_configuration, (gpointer) p,
        _("Show CapsLock"), &kl->visible[0], CONF_TYPE_BOOL,
        _("Show NumLock"), &kl->visible[1], CONF_TYPE_BOOL,
        _("Show ScrollLock"), &kl->visible[2], CONF_TYPE_BOOL,
        NULL);
    gtk_widget_set_size_request(GTK_WIDGET(dlg), 200, -1);	/* Improve geometry */
    gtk_window_present(GTK_WINDOW(dlg));
}

/* Callback when the configuration is to be saved. */
static void kbled_save_configuration(Plugin * p, FILE * fp)
{
    KeyboardLEDPlugin * kl = (KeyboardLEDPlugin *) p->priv;
    lxpanel_put_int(fp, "ShowCapsLock", kl->visible[0]);
    lxpanel_put_int(fp, "ShowNumLock", kl->visible[1]);
    lxpanel_put_int(fp, "ShowScrollLock", kl->visible[2]);
}

/* Callback when panel configuration changes. */
static void kbled_panel_configuration_changed(Plugin * p)
{
    /* Set orientation into the icon grid. */
    KeyboardLEDPlugin * kl = (KeyboardLEDPlugin *) p->priv;
    GtkOrientation bo = (p->panel->orientation == ORIENT_HORIZ) ? GTK_ORIENTATION_HORIZONTAL : GTK_ORIENTATION_VERTICAL;
    icon_grid_set_geometry(kl->icon_grid, bo, p->panel->icon_size, p->panel->icon_size, 0, 0, p->panel->height);

    /* Do a full redraw. */
    int current_state = kl->current_state;
    kl->current_state = ~ kl->current_state;
    kbled_update_display(p, current_state);
}

/* Plugin descriptor. */
PluginClass kbled_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "kbled",
    name : N_("Keyboard LED"),
    version: "1.0",
    description : N_("Indicators for CapsLock, NumLock, and ScrollLock keys"),

    constructor : kbled_constructor,
    destructor  : kbled_destructor,
    config : kbled_configure,
    save : kbled_save_configuration,
    panel_configuration_changed : kbled_panel_configuration_changed
};
