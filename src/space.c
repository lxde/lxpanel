/*
 * Copyright (C) 2006-2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2006-2008 Jim Huang <jserv.tw@gmail.com>
 *               2009 Marty Jack <martyj19@comcast.net>
 *               2010 Julien Lavergne <julien.lavergne@gmail.com>
 *               2014 Andriy Grytsenko <andrej@rep.kiev.ua>
 *
 * This file is a part of LXPanel project.
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

/* Private context for space plugin. */
typedef struct {
    LXPanel *panel; /* The panel and settings are required to apply config */
    config_setting_t *settings;
    int size;				/* Size of spacer */
} SpacePlugin;

static gboolean space_apply_configuration(gpointer user_data);

/* Plugin constructor. */
static GtkWidget *space_constructor(LXPanel *panel, config_setting_t *settings)
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
    gtk_widget_set_has_window(p,FALSE);

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
    if (panel_get_orientation(sp->panel) == GTK_ORIENTATION_HORIZONTAL)
        gtk_widget_set_size_request(p, sp->size, 2);
    else
        gtk_widget_set_size_request(p, 2, sp->size);
    /* Save config values */
    config_group_set_int(sp->settings, "Size", sp->size);
    return FALSE;
}

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *space_configure(LXPanel *panel, GtkWidget *instance)
{
    SpacePlugin * sp = lxpanel_plugin_get_data(instance);
    GtkWidget * dlg;

    dlg = lxpanel_generic_config_dlg(_("Spacer"), panel,
                                     space_apply_configuration, instance,
                                     _("Size"), &sp->size, CONF_TYPE_INT, NULL);
    gtk_widget_set_size_request(dlg, 200, -1);	/* Improve geometry */
    return dlg;
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
