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

#include "panel.h"
#include "misc.h"
#include "private.h"

#include "dbg.h"

typedef struct _pager {
    Plugin* plugin;
    GtkWidget *box;
} pager;


static int
pager_constructor(Plugin *plug, char **fp)
{
    pager *pg;

    ENTER;
    pg = g_new0(pager, 1);
    g_return_val_if_fail(pg != NULL, 0);
    plug->priv = pg;
    pg->plugin = plug;

    plug->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( plug->pwid, GTK_NO_WINDOW );

    pg->box = wnck_pager_new(NULL);
    g_return_val_if_fail(pg->box != NULL, 0);
    //set orientation
    wnck_pager_set_orientation (WNCK_PAGER (pg->box),pg->plugin->panel->orientation);
    wnck_pager_set_n_rows (WNCK_PAGER (pg->box), 1); //pager->rows);
    wnck_pager_set_display_mode (WNCK_PAGER (pg->box),WNCK_PAGER_DISPLAY_CONTENT);
    //pager->show_names ? WNCK_PAGER_DISPLAY_NAME : WNCK_PAGER_DISPLAY_CONTENT);

    //gtk_container_set_border_width (GTK_CONTAINER (pg->box), 2);
    gtk_widget_show(pg->box);

    gtk_container_set_border_width (GTK_CONTAINER (plug->pwid), 1);
    gtk_container_add(GTK_CONTAINER(plug->pwid), pg->box);

    RET(1);
}

static void
pager_destructor(Plugin *p)
{
    pager *pg = (pager *)p->priv;

    ENTER;
    gtk_widget_destroy(p->pwid);
    g_free(pg);
    RET();
}

static void pager_panel_configuration_changed(Plugin * p)
{
    pager *pg = (pager *)p->priv;

    wnck_pager_set_orientation (WNCK_PAGER (pg->box),pg->plugin->panel->orientation);
    wnck_pager_set_n_rows (WNCK_PAGER (pg->box), pg->plugin->panel->height / 48 + 1);
}

PluginClass pager_plugin_class = {

    PLUGINCLASS_VERSIONING,

    .type = "pager",
    .name = N_("Desktop Pager"),
    .version = "1.0",
    .description = N_("Simple pager plugin"),

    .constructor = pager_constructor,
    .destructor  = pager_destructor,
    .config = NULL,
    .save = NULL,
    .panel_configuration_changed = pager_panel_configuration_changed
};
