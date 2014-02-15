/* pager.c -- pager module of lxpanel project
 *
 * Copyright (C) 2009 Dongxu Li <song6song@sourceforge.net>
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

#include <gtk/gtk.h>

#include <glib/gi18n.h>
#ifndef WNCK_I_KNOW_THIS_IS_UNSTABLE
#define WNCK_I_KNOW_THIS_IS_UNSTABLE
#endif
#include <libwnck/libwnck.h>

#include "plugin.h"


static void on_realize(GtkWidget *p, Panel *panel)
{
    WnckPager *pager = WNCK_PAGER(gtk_bin_get_child(GTK_BIN(p)));
    int rows, r, h = panel_get_height(panel);

    /* set geometry */
    wnck_pager_set_orientation(pager, panel_get_orientation(panel));
    if (panel_get_orientation(panel) == GTK_ORIENTATION_VERTICAL)
        h *= ((gfloat) gdk_screen_height() / (gfloat) gdk_screen_width());
    rows = h / (panel_get_icon_size(panel) * 2) + 1; /* min */
    r = (h - 4) / panel_get_icon_size(panel); /* max */
    /* g_debug("pager for height %d and icon size %d: %d to %d",panel_get_height(panel),panel_get_icon_size(panel),r,rows); */
    rows = MAX(rows, r);
    wnck_pager_set_n_rows(pager, rows);
}

static void on_size_allocate(GtkWidget *p, GdkRectangle *allocation, Panel *panel)
{
    /* g_debug("pager: on_size_allocate(): %dx%d", allocation->width, allocation->height); */
    on_realize(p, panel);
}

static GtkWidget *pager_constructor(Panel *panel, config_setting_t *settings)
{
    GtkWidget *p, *w;

    w = wnck_pager_new(NULL);
    g_return_val_if_fail(w != NULL, 0);
    p = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS(p, GTK_NO_WINDOW);

    /* we cannot configure pager until it added into widgets hierarchy */
    g_signal_connect(p, "realize", G_CALLBACK(on_realize), panel);
    g_signal_connect(p, "size-allocate", G_CALLBACK(on_size_allocate), panel);
    wnck_pager_set_display_mode(WNCK_PAGER(w), WNCK_PAGER_DISPLAY_CONTENT);

    gtk_widget_show(w);

    gtk_container_set_border_width(GTK_CONTAINER(p), 2);
    gtk_container_add(GTK_CONTAINER(p), w);

    return p;
}

static void pager_panel_configuration_changed(Panel *panel, GtkWidget *p)
{
    on_realize(p, panel);
}

LXPanelPluginInit lxpanel_static_plugin_pager = {
    .name = N_("Desktop Pager"),
    .description = N_("Simple pager plugin"),

    .new_instance = pager_constructor,
    .reconfigure = pager_panel_configuration_changed
};
