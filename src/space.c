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

#include "space.h"

struct _PanelSpace
{
    GtkEventBox parent;
    config_setting_t *settings;
    int size;
};

struct _PanelSpaceClass
{
    GtkEventBoxClass parent_class;
};

#if GTK_CHECK_VERSION(3, 0, 0)
static void panel_space_get_preferred_size(GtkWidget *widget,
                                           gint *minimal_width,
                                           gint *natural_width)
{
    PanelSpace *p = PANEL_SPACE(widget);

    if (minimal_width)
        *minimal_width = p->size;
    if (natural_width)
        *natural_width = p->size;
}
#else
static void panel_space_size_request(GtkWidget *widget,
                                     GtkRequisition *requisition)
{
    PanelSpace *p = PANEL_SPACE(widget);

    requisition->width = requisition->height = p->size;
}
#endif

G_DEFINE_TYPE(PanelSpace, panel_space, GTK_TYPE_EVENT_BOX)

static void panel_space_class_init(PanelSpaceClass *klass)
{
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

#if GTK_CHECK_VERSION(3, 0, 0)
    widget_class->get_preferred_width = panel_space_get_preferred_size;
    widget_class->get_preferred_height = panel_space_get_preferred_size;
#else
    widget_class->size_request = panel_space_size_request;
#endif
}

static void panel_space_init(PanelSpace *self)
{
    gtk_widget_set_has_window(GTK_WIDGET(self), FALSE);
}

/* Plugin constructor. */
GtkWidget *_panel_space_new(LXPanel *panel, config_setting_t *settings)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    PanelSpace * p = g_object_new(PANEL_TYPE_SPACE, NULL);

    /* Load parameters from the configuration file. */
    config_setting_lookup_int(settings, "Size", &p->size);

    /* Save construction pointers */
    p->settings = settings;

    /* Default the size parameter. */
    if (p->size == 0)
        p->size = 2;

    return GTK_WIDGET(p);
}

/* Callback when the configuration dialog has recorded a configuration change. */
static gboolean space_apply_configuration(gpointer user_data)
{
    PanelSpace * p = user_data;

    /* Apply settings. */
    gtk_widget_queue_resize(user_data);
    /* Save config values */
    config_group_set_int(p->settings, "Size", p->size);
    return FALSE;
}

void _panel_space_resize(GtkWidget *spacer, gint size)
{
    PanelSpace * p = PANEL_SPACE(spacer);

    p->size = MAX(0, size);
    space_apply_configuration(p);
}

/* Callback when the configuration dialog is to be shown. */
static GtkWidget *space_configure(LXPanel *panel, GtkWidget *instance)
{
    PanelSpace * p = PANEL_SPACE(instance);
    GtkWidget * dlg;

    dlg = lxpanel_generic_config_dlg(_("Spacer"), panel,
                                     space_apply_configuration, instance,
                                     _("Size"), &p->size, CONF_TYPE_INT, NULL);
    gtk_widget_set_size_request(dlg, 200, -1);	/* Improve geometry */
    return dlg;
}

/* Plugin descriptor. */
LXPanelPluginInit _lxpanel_static_plugin_space = {
    .name = N_("Spacer"),
    .description = N_("Allocate space"),

    /* Stretch is available but not default for this plugin. */
    .expand_available = TRUE,

    .new_instance = _panel_space_new,
    .config = space_configure,
};
