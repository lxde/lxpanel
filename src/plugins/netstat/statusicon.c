/**
 * Copyright (c) 2008 LxDE Developers, see the file AUTHORS for details.
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

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include "netstat.h"
#include "statusicon.h"

struct statusicon *create_statusicon(GtkWidget *box, const char *filename,
        const char *tooltips)
{
    struct statusicon *newicon;
    newicon = malloc(sizeof(struct statusicon));

    /* main */
    newicon->main = gtk_event_box_new();

    GTK_WIDGET_SET_FLAGS(newicon->main, GTK_NO_WINDOW);
    gtk_widget_add_events(newicon->main, GDK_BUTTON_PRESS_MASK);
    gtk_widget_set_size_request(newicon->main, 24, 24);
    gtk_box_pack_start(GTK_BOX(box), newicon->main, TRUE, TRUE, 0);

    /* icon */
    newicon->icon = gtk_image_new_from_file(filename);
    gtk_container_add(GTK_CONTAINER(newicon->main), newicon->icon);
    gtk_widget_show_all(newicon->main);

    /* tooltip */
    newicon->tooltips = gtk_tooltips_new();
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink(newicon->tooltips);
#else
    g_object_ref(newicon->tooltips);
    gtk_object_sink(newicon->tooltips);
#endif
    gtk_tooltips_set_tip(newicon->tooltips, newicon->main, tooltips, NULL);

    return newicon;
}

void statusicon_destroy(struct statusicon *icon)
{
	if (icon==NULL)
        return;

    g_object_unref(icon->tooltips);
    gtk_widget_destroy(GTK_WIDGET(icon->icon));
    gtk_widget_destroy(GTK_WIDGET(icon->main));
    g_free(icon);
}


void set_statusicon_image_from_file(struct statusicon *widget, const char *filename)
{
    gtk_image_set_from_file(widget->icon, filename);
}

void set_statusicon_tooltips(struct statusicon *widget, const char *tooltips)
{
    gtk_tooltips_set_tip(widget->tooltips, widget->main, tooltips, NULL);
}

void set_statusicon_visible(struct statusicon *widget, gboolean b)
{
    if (b)
        gtk_widget_show(widget->main);
    else
        gtk_widget_hide(widget->main);
}
