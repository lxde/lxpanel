/**
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

#include <stdlib.h>

#include <glib/gi18n.h>

#include "plugin.h"
#include "misc.h"

/* Private context for space plugin. */
typedef struct {
    Panel *panel; /* The panel and settings are required to apply config */
    config_setting_t *settings;
    int size;				/* Size of spacer */
} SpacePlugin;

static gboolean space_apply_configuration(gpointer user_data);

/* Plugin constructor. */
static GtkWidget *space_constructor(Panel *panel, config_setting_t *settings)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    SpacePlugin * sp = g_new0(SpacePlugin, 1);
    GtkWidget * p;

    /* Load parameters from the configuration file. */
    config_setting_lookup_int(settings, "Size", &sp->size);

    /* Save construction pointers */
    sp->panel = panel;
    sp->settings = settings;

    /* Default the size parameter. */
    if (sp->size == 0)
        sp->size = 2;

    /* Allocate top level widget and set into Plugin widget pointer. */
    p = gtk_event_box_new();
    lxpanel_plugin_set_data(p, sp, g_free);
#if GTK_CHECK_VERSION(2,18,0)
    gtk_widget_set_has_window(p,FALSE);
#else
    GTK_WIDGET_SET_FLAGS(p, GTK_NO_WINDOW);
#endif
    gtk_widget_add_events(p, GDK_BUTTON_PRESS_MASK);
    gtk_container_set_border_width(GTK_CONTAINER(p), 0);

    /* Connect signals. */
    g_signal_connect(p, "button-press-event",
                     G_CALLBACK(lxpanel_plugin_button_press_event), p);

    /* Apply the configuration and show the widget. */
    space_apply_configuration(p);
    return p;
}

/* Callback when the configuration dialog has recorded a configuration change. */
static gboolean space_apply_configuration(gpointer user_data)
{
    GtkWidget * p = user_data;
    SpacePlugin * sp = lxpanel_plugin_get_data(p);

    /* Apply settings. */
    if (panel_is_horizontal(sp->panel))
        gtk_widget_set_size_request(p, sp->size, 2);
    else
        gtk_widget_set_size_request(p, 2, sp->size);
    /* Save config values */
    config_setting_set_int(config_setting_add(sp->settings, "Size",
                                              PANEL_CONF_TYPE_INT), sp->size);
    return FALSE;
}

/* Callback when the configuration dialog is to be shown. */
static void space_configure(Panel *panel, GtkWidget *instance, GtkWindow *parent)
{
    SpacePlugin * sp = lxpanel_plugin_get_data(instance);
    GtkWidget * dlg;

    dlg = lxpanel_generic_config_dlg(_("Spacer"), panel,
                                     space_apply_configuration, instance,
                                     _("Size"), &sp->size, CONF_TYPE_INT, NULL);
    gtk_widget_set_size_request(GTK_WIDGET(dlg), 200, -1);	/* Improve geometry */
    gtk_window_present(GTK_WINDOW(dlg));
}

/* Plugin descriptor. */
LXPanelPluginInit lxpanel_static_plugin_space = {
    .name = N_("Spacer"),
    .description = N_("Allocate space"),

    /* Stretch is available but not default for this plugin. */
    .expand_available = TRUE,

    .new_instance = space_constructor,
    .config = space_configure,
};
