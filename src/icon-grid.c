/**
 * Copyright (c) 2009 LxDE Developers, see the file AUTHORS for details.
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

#include <gtk/gtk.h>
#include <gtk/gtkprivate.h>
#include <string.h>

#include "icon-grid.h"
#include "panel.h"

static gboolean icon_grid_placement(IconGrid * ig);
static void icon_grid_geometry(IconGrid * ig, gboolean layout);
static void icon_grid_element_size_request(GtkWidget * widget, GtkRequisition * requisition, IconGridElement * ige);
static void icon_grid_size_request(GtkWidget * widget, GtkRequisition * requisition, IconGrid * ig);
static void icon_grid_size_allocate(GtkWidget * widget, GtkAllocation * allocation, IconGrid * ig);
static void icon_grid_demand_resize(IconGrid * ig);

/* Establish the widget placement of an icon grid. */
static gboolean icon_grid_placement(IconGrid * ig)
{
    gtk_widget_show(ig->container);

    GdkWindow * bin_window = gtk_layout_get_bin_window(GTK_LAYOUT(ig->widget));
    if (bin_window != NULL)
    {
        panel_determine_background_pixmap(ig->panel, ig->widget, bin_window);
	gdk_window_clear(bin_window);
    }

    /* Reposition each visible child. */
    int x = ig->border;
    int y = ig->border;
    int limit = ig->border + ((ig->orientation == GTK_ORIENTATION_HORIZONTAL)
        ?  (ig->rows * (ig->child_height + ig->spacing))
        :  (ig->columns * (ig->child_width + ig->spacing)));
    IconGridElement * ige;
    for (ige = ig->child_list; ige != NULL; ige = ige->flink)
    {
        if (ige->visible)
        {
            gtk_widget_show(ige->widget);
            gtk_layout_move(GTK_LAYOUT(ig->widget), ige->widget, x, y);
            gtk_widget_queue_draw(ige->widget);
            if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
            {
                y += ig->child_height + ig->spacing;
                if (y >= limit)
                {
                    y = ig->border;
                    x += ig->child_width + ig->spacing;
                }
            }
            else
            {
                x += ig->child_width + ig->spacing;
                if (x >= limit)
                {
                    x = ig->border;
                    y += ig->child_height + ig->spacing;
                }
            }
        }
    }

    /* Redraw the container. */
    if (bin_window != NULL)
	gdk_window_invalidate_rect(bin_window, NULL, TRUE);
    gtk_widget_queue_draw(ig->container);
    return FALSE;
}

/* Establish the geometry of an icon grid. */
static void icon_grid_geometry(IconGrid * ig, gboolean layout)
{
    /* Count visible children. */
    int visible_children = 0;
    IconGridElement * ige;
    for (ige = ig->child_list; ige != NULL; ige = ige->flink)
        if (ige->visible)
            visible_children += 1;

   int original_rows = ig->rows;
   int original_columns = ig->columns;
   int target_dimension = ig->target_dimension;
   if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        /* In horizontal orientation, fit as many rows into the available height as possible.
         * Then allocate as many columns as necessary.  Guard against zerodivides. */
        if (ig->container->allocation.height > 1)
            target_dimension = ig->container->allocation.height;
        ig->rows = 0;
        if ((ig->child_height + ig->spacing) != 0)
            ig->rows = (target_dimension + ig->spacing - ig->border * 2) / (ig->child_height + ig->spacing);
        if (ig->rows == 0)
            ig->rows = 1;
        ig->columns = (visible_children + (ig->rows - 1)) / ig->rows;
        if ((ig->columns == 1) && (ig->rows > visible_children))
            ig->rows = visible_children;
    }
    else
    {
        /* In vertical orientation, fit as many columns into the available width as possible.
         * Then allocate as many rows as necessary.  Guard against zerodivides. */
        if (ig->container->allocation.width > 1)
            target_dimension = ig->container->allocation.width;
        ig->columns = 0;
        if ((ig->child_width + ig->spacing) != 0)
            ig->columns = (target_dimension + ig->spacing - ig->border * 2) / (ig->child_width + ig->spacing);
        if (ig->columns == 0)
            ig->columns = 1;
        ig->rows = (visible_children + (ig->columns - 1)) / ig->columns;
        if ((ig->rows == 1) && (ig->columns > visible_children))
            ig->columns = visible_children;
    }

    /* If the table geometry or child composition changed, redo the placement of children in table cells.
     * This is gated by having a valid table allocation and by the "layout" parameter, which prevents a recursive loop.
     * We do the placement later, also to prevent a recursive loop. */
    if ((layout)
    && (( ! ig->actual_dimension)
      || (ig->rows != original_rows) || (ig->columns != original_columns)
      || (ig->children_changed)))
        {
        ig->actual_dimension = TRUE;
        ig->children_changed = FALSE;
        g_idle_add((GSourceFunc) icon_grid_placement, ig);
        }
}

/* Handler for "size-request" event on the icon grid element. */
static void icon_grid_element_size_request(GtkWidget * widget, GtkRequisition * requisition, IconGridElement * ige)
{
    /* This is our opportunity to request space for the element. */
    IconGrid * ig = ige->ig;
    requisition->width = ig->child_width;
    requisition->height = ig->child_height;
}

/* Handler for "size-request" event on the icon grid's container. */
static void icon_grid_size_request(GtkWidget * widget, GtkRequisition * requisition, IconGrid * ig)
{
    /* This is our opportunity to request space for the layout container.
     * Compute the geometry.  Do not attach children at this time to avoid a recursive loop. */
    icon_grid_geometry(ig, FALSE);

    /* Compute the requisition. */
    if ((ig->columns == 0) || (ig->rows == 0))
    {
        requisition->width = 1;
        requisition->height = 1;
        gtk_widget_hide(ig->widget);	/* Necessary to get the plugin to disappear */
    }
    else
    {
        int column_spaces = ig->columns - 1;
        int row_spaces = ig->rows - 1;
        if (column_spaces < 0) column_spaces = 0;
        if (row_spaces < 0) row_spaces = 0;
        requisition->width = ig->child_width * ig->columns + column_spaces * ig->spacing + 2 * ig->border;
        requisition->height = ig->child_height * ig->rows + row_spaces * ig->spacing + 2 * ig->border;
        gtk_widget_show(ig->widget);
    }
}

/* Handler for "size-allocate" event on the icon grid's container. */
static void icon_grid_size_allocate(GtkWidget * widget, GtkAllocation * allocation, IconGrid * ig)
{
    /* This is our notification that there is a resize of the entire panel.
     * Compute the geometry and recompute layout if the geometry changed. */
    icon_grid_geometry(ig, TRUE);
}

/* Initiate a resize. */
static void icon_grid_demand_resize(IconGrid * ig)
{
    ig->children_changed = TRUE;
    GtkRequisition req;
    icon_grid_size_request(NULL, &req, ig);
    gtk_layout_set_size(GTK_LAYOUT(ig->widget), req.width, req.height);

    if ((ig->rows != 0) || (ig->columns != 0))
        icon_grid_placement(ig);
}

/* Establish an icon grid in a specified container widget.
 * The icon grid manages the contents of the container.
 * The orientation, geometry of the elements, and spacing can be varied.  All elements are the same size. */
IconGrid * icon_grid_new(
    Panel * panel, GtkWidget * container, GtkOrientation orientation,
    gint child_width, gint child_height, gint spacing, gint border, gint target_dimension)
{
    /* Create a structure representing the icon grid and collect the parameters. */
    IconGrid * ig = g_new0(IconGrid, 1);
    ig->panel = panel;
    ig->container = container;
    ig->orientation = orientation;
    ig->child_width = child_width;
    ig->child_height = child_height;
    ig->spacing = spacing;
    ig->border = border;
    ig->target_dimension = target_dimension;

    /* Create a layout container. */
    ig->widget = gtk_layout_new(NULL, NULL);
    GTK_WIDGET_SET_FLAGS(ig->widget, GTK_NO_WINDOW);
    gtk_container_add(GTK_CONTAINER(ig->container), ig->widget);
    gtk_widget_show(ig->widget);

    /* Connect signals. */
    g_signal_connect(G_OBJECT(ig->widget), "size-request", G_CALLBACK(icon_grid_size_request), (gpointer) ig);
    g_signal_connect(G_OBJECT(container), "size-request", G_CALLBACK(icon_grid_size_request), (gpointer) ig);
    g_signal_connect(G_OBJECT(container), "size-allocate", G_CALLBACK(icon_grid_size_allocate), (gpointer) ig);
    return ig;
}

/* Add an icon grid element and establish its initial visibility. */
void icon_grid_add(IconGrid * ig, GtkWidget * child, gboolean visible)
{
    /* Create and initialize a structure representing the child. */
    IconGridElement * ige = g_new0(IconGridElement, 1);
    ige->ig = ig;
    ige->widget = child;
    ige->visible = visible;

    /* Insert at the tail of the child list.  This keeps the graphics in the order they were added. */
    if (ig->child_list == NULL)
        ig->child_list = ige;
    else
    {
        IconGridElement * ige_cursor;
        for (ige_cursor = ig->child_list; ige_cursor->flink != NULL; ige_cursor = ige_cursor->flink) ;
        ige_cursor->flink = ige;
    }

    /* Add the widget to the layout container. */
    gtk_widget_show(ige->widget);
    gtk_layout_put(GTK_LAYOUT(ig->widget), ige->widget, 0, 0);
    g_signal_connect(G_OBJECT(child), "size-request", G_CALLBACK(icon_grid_element_size_request), (gpointer) ige);

    /* Do a relayout. */
    icon_grid_demand_resize(ig);
}

/* Remove an icon grid element. */
void icon_grid_remove(IconGrid * ig, GtkWidget * child)
{
    IconGridElement * ige_pred = NULL;
    IconGridElement * ige;
    for (ige = ig->child_list; ige != NULL; ige_pred = ige, ige = ige->flink)
    {
        if (ige->widget == child)
        {
            /* The child is found.  Remove from child list and layout container. */
            gtk_widget_hide(ige->widget);
            gtk_container_remove(GTK_CONTAINER(ig->widget), ige->widget);

            if (ige_pred == NULL)
                ig->child_list = ige->flink;
            else
                ige_pred->flink = ige->flink;

            /* Do a relayout. */
            icon_grid_demand_resize(ig);
            break;
        }
    }
}

/* Reorder an icon grid element. */
extern void icon_grid_reorder_child(IconGrid * ig, GtkWidget * child, gint position)
{
    /* Remove the child from its current position. */
    IconGridElement * ige_pred = NULL;
    IconGridElement * ige;
    for (ige = ig->child_list; ige != NULL; ige_pred = ige, ige = ige->flink)
    {
        if (ige->widget == child)
        {
            if (ige_pred == NULL)
                ig->child_list = ige->flink;
            else
                ige_pred->flink = ige->flink;
            break;
        }
    }

    /* If the child was found, insert it at the new position. */
    if (ige != NULL)
    {
        if (ig->child_list == NULL)
        {
            ige->flink = NULL;
            ig->child_list = ige;
        }
        else if (position == 0)
        {
            ige->flink = ig->child_list;
            ig->child_list = ige;
        }
        else
            {
            gint local_position = position - 1;
            IconGridElement * ige_cursor;
            for (
              ige_cursor = ig->child_list;
              ((ige_cursor->flink != NULL) && (local_position > 0));
              local_position -= 1, ige_cursor = ige_cursor->flink) ;
            ige->flink = ige_cursor->flink;
            ige_cursor->flink = ige;
            }

        /* Do a relayout. */
        if (ige->visible)
            icon_grid_demand_resize(ig);
    }
}

/* Change the orientation of an icon grid. */
void icon_grid_set_orientation(IconGrid * ig, GtkOrientation orientation, gint target_dimension)
{
    ig->orientation = orientation;
    ig->target_dimension = target_dimension;
    icon_grid_demand_resize(ig);
}

/* Change the visibility of an icon grid element. */
void icon_grid_set_visible(IconGrid * ig, GtkWidget * child, gboolean visible)
{
    IconGridElement * ige;
    for (ige = ig->child_list; ige != NULL; ige = ige->flink)
    {
        if (ige->widget == child)
        {
            if (ige->visible != visible)
            {
                /* Found, and the visibility changed.  Do a relayout. */
                ige->visible = visible;
                if ( ! ige->visible)
                    gtk_widget_hide(ige->widget);
                icon_grid_demand_resize(ig);
            }
            break;
        }
    }
}

/* Deallocate the icon grid structures. */
void icon_grid_free(IconGrid * ig)
{
    if (ig->widget != NULL)
        gtk_widget_hide(ig->widget);

    /* Remove all child elements. */
    IconGridElement * ige;
    for (ige = ig->child_list; ige != NULL; ige = ige->flink)
        icon_grid_remove(ig, ige->widget);

    /* Get the empty widget redrawn. */
    icon_grid_demand_resize(ig);
    while (gtk_events_pending()) gtk_main_iteration();

    /* Free all memory. */
    while (ig->child_list != NULL)
    {
        IconGridElement * ige_succ = ig->child_list->flink;
        g_free(ig->child_list);
        ig->child_list = ige_succ;
    }
    g_free(ig);
}
