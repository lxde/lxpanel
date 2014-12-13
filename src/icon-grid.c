/**
 * Copyright (c) 2009-2014 LxDE Developers, see the file AUTHORS for details.
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
#include <string.h>

#include "icon-grid.h"
#include "gtk-compat.h"

/* Properties */
enum {
  PROP_0,
  PROP_ORIENTATION,
  PROP_SPACING,
  PROP_CONSTRAIN_WIDTH,
  PROP_ASPECT_WIDTH
  //PROP_FILL_WIDTH
};

/* Representative of an icon grid.  This is a manager that packs widgets into a rectangular grid whose size adapts to conditions. */
struct _PanelIconGrid
{
    GtkContainer container;			/* Parent widget */
    GList * children;				/* List of icon grid elements */
    GtkOrientation orientation;			/* Desired orientation */
    gint child_width;				/* Desired child width */
    gint child_height;				/* Desired child height */
    gint spacing;				/* Desired spacing between grid elements */
    gint target_dimension;			/* Desired dimension perpendicular to orientation */
    gboolean constrain_width : 1;		/* True if width should be constrained by allocated space */
    gboolean aspect_width : 1;			/* True if children should maintain aspect */
    gboolean fill_width : 1;			/* True if children should fill unused width */
    int rows;					/* Computed layout rows */
    int columns;				/* Computed layout columns */
    GdkWindow *event_window;			/* Event window if NO_WINDOW is set */
};

struct _PanelIconGridClass
{
    GtkContainerClass parent_class;
};

static void icon_grid_element_check_requisition(PanelIconGrid *ig,
                                                GtkRequisition *requisition)
{
    if (ig->aspect_width && !ig->constrain_width &&
        requisition->width > 1 && requisition->height > 1)
    {
        /* calculate width from aspect */
        gdouble ratio = (gdouble)requisition->width / requisition->height;
        requisition->width = ig->child_height * ratio;
    }
    else
    {
        requisition->width = ig->child_width;
    }
    requisition->height = ig->child_height;
}

/* Establish the widget placement of an icon grid. */
static void panel_icon_grid_size_allocate(GtkWidget *widget,
                                          GtkAllocation *allocation)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);
    GtkRequisition req;
    GtkAllocation child_allocation;
    int child_width;
    int child_height;
    GtkTextDirection direction;
    guint border;
    int x_delta;
    guint next_coord;
    guint x, y;
    GList *ige;
    GtkWidget *child;

    /* Apply given allocation */
    gtk_widget_set_allocation(widget, allocation);
    border = gtk_container_get_border_width(GTK_CONTAINER(widget));
    child_allocation.width = MAX(allocation->width - border * 2, 0);
    child_allocation.height = MAX(allocation->height - border * 2, 0);
    if (gtk_widget_get_realized(widget))
    {
        if (!gtk_widget_get_has_window(widget))
        {
            child_allocation.x = allocation->x + border;
            child_allocation.y = allocation->y + border;
        }
        else
        {
            child_allocation.x = 0;
            child_allocation.y = 0;
        }
        if (ig->event_window != NULL)
            gdk_window_move_resize(ig->event_window,
                                   child_allocation.x,
                                   child_allocation.y,
                                   child_allocation.width,
                                   child_allocation.height);
        if (gtk_widget_get_has_window(widget))
            gdk_window_move_resize(gtk_widget_get_window(widget),
                                   allocation->x + border,
                                   allocation->y + border,
                                   child_allocation.width,
                                   child_allocation.height);
    }

    /* Get and save the desired container geometry. */
    if (ig->orientation == GTK_ORIENTATION_HORIZONTAL && allocation->height > 1)
        ig->target_dimension = allocation->height;
    else if (ig->orientation == GTK_ORIENTATION_VERTICAL && allocation->width > 1)
        ig->target_dimension = allocation->width;
    child_width = ig->child_width;
    child_height = ig->child_height;

    /* FIXME: is there any sense to recheck rows and columns again?
       GTK+ should have it done right before this call. */

    /* Get the constrained child geometry if the allocated geometry is insufficient.
     * All children are still the same size and share equally in the deficit. */
    if ((ig->columns != 0) && (ig->rows != 0) && (child_allocation.width > 0))
    {
        if (ig->constrain_width &&
            (x_delta = (child_allocation.width + ig->spacing) / ig->columns - ig->spacing) < child_width)
            child_width = MAX(2, x_delta);
        /* fill vertical space evenly in horisontal orientation */
        if (ig->orientation == GTK_ORIENTATION_HORIZONTAL &&
            (x_delta = (child_allocation.height + ig->spacing) / ig->rows - ig->spacing) > child_height)
            child_height = MAX(2, x_delta);
    }

    /* Initialize parameters to control repositioning each visible child. */
    direction = gtk_widget_get_direction(widget);
    x = (direction == GTK_TEXT_DIR_RTL) ? allocation->width - border : border;
    y = border;
    x_delta = 0;
    next_coord = border;

    /* Reposition each visible child. */
    for (ige = ig->children; ige != NULL; ige = ige->next)
    {
        child = ige->data;
        if (gtk_widget_get_visible(child))
        {
            /* Do necessary operations on the child. */
            gtk_widget_get_child_requisition(child, &req);
            icon_grid_element_check_requisition(ig, &req);
            child_allocation.width = MIN(req.width, child_width);
            child_allocation.height = MIN(req.height, child_height);

            /* Check this grid position */
            if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
            {
                y = next_coord;
                if (y + child_height > allocation->height - border && y > border)
                {
                    y = border;
                    if (direction == GTK_TEXT_DIR_RTL)
                        x -= (x_delta + ig->spacing);
                    else
                        x += (x_delta + ig->spacing);
                    x_delta = 0;
                    // FIXME: if fill_width and rows = 1 then allocate whole column
                }
                next_coord = y + child_height + ig->spacing;
                x_delta = MAX(x_delta, child_allocation.width);
            }
            else
            {
                // FIXME: if fill_width then use aspect to check delta
                if (direction == GTK_TEXT_DIR_RTL)
                {
                    next_coord = x - child_allocation.width;
                    if (x < allocation->width - border)
                    {
                        next_coord -= ig->spacing;
                        if (next_coord < border)
                        {
                            next_coord = allocation->width - border;
                            y += child_height + ig->spacing;
                        }
                    }
                    x = next_coord;
                }
                else
                {
                    x = next_coord;
                    if (x + child_allocation.width > allocation->width - border && x > border)
                    {
                        x = border;
                        y += child_height + ig->spacing;
                    }
                    next_coord = x + child_allocation.width + ig->spacing;
                }
            }
            child_allocation.x = x;
            if (req.height < child_height - 1)
                y += (child_height - req.height) / 2;
            child_allocation.y = y;

            if (!gtk_widget_get_has_window (widget))
            {
                child_allocation.x += allocation->x;
                child_allocation.y += allocation->y;
            }
            // FIXME: if fill_width and rows > 1 then delay allocation
            gtk_widget_size_allocate(child, &child_allocation);
        }
    }
}

/* Establish the geometry of an icon grid. */
static void panel_icon_grid_size_request(GtkWidget *widget,
                                         GtkRequisition *requisition)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);
    GList *ige;
    int target_dimension = ig->target_dimension;
    guint border = gtk_container_get_border_width(GTK_CONTAINER(widget));
    gint old_rows = ig->rows;
    gint old_columns = ig->columns;
    gint row = 0, w = 0;
    GtkRequisition child_requisition;

    requisition->width = 0;
    requisition->height = 0;
    ig->rows = 0;
    ig->columns = 0;
    if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
    {
        /* In horizontal orientation, fit as many rows into the available height as possible.
         * Then allocate as many columns as necessary.  Guard against zerodivides. */
        if ((ig->child_height + ig->spacing) != 0)
            ig->rows = (target_dimension + ig->spacing - border * 2) / (ig->child_height + ig->spacing);
        if (ig->rows == 0)
            ig->rows = 1;
        /* Count visible children and columns. */
        for (ige = ig->children; ige != NULL; ige = ige->next)
            if (gtk_widget_get_visible(ige->data))
            {
                gtk_widget_size_request(ige->data, &child_requisition);
                icon_grid_element_check_requisition(ig, &child_requisition);
                if (row == 0)
                    ig->columns++;
                w = MAX(w, child_requisition.width);
                row++;
                if (row == ig->rows)
                {
                    row = 0;
                    if (requisition->width > 0)
                        requisition->width += ig->spacing;
                    requisition->width += w;
                    row = w = 0;
                }
            }
        if (row > 0)
            requisition->width += w;
        /* if ((ig->columns == 1) && (ig->rows > visible_children))
            ig->rows = visible_children; */
    }
    else
    {
        /* In vertical orientation, fit as many columns into the available width as possible.
         * Then allocate as many rows as necessary.  Guard against zerodivides. */
        if ((ig->child_width + ig->spacing) != 0)
            ig->columns = (target_dimension + ig->spacing - border * 2) / (ig->child_width + ig->spacing);
        if (ig->columns == 0)
            ig->columns = 1;
        /* Count visible children and rows. */
        for (ige = ig->children; ige != NULL; ige = ige->next)
            if (gtk_widget_get_visible(ige->data))
            {
                gtk_widget_size_request(ige->data, &child_requisition);
                icon_grid_element_check_requisition(ig, &child_requisition);
                if (w > 0 && w + child_requisition.width > target_dimension)
                {
                    w = 0;
                    ig->rows++;
                }
                if (w > 0)
                    w += ig->spacing;
                w += child_requisition.width;
                requisition->width = MAX(requisition->width, w);
            }
        if (w > 0)
            ig->rows++;
    }

    /* Compute the requisition. */
    if ((ig->columns == 0) || (ig->rows == 0))
        requisition->height = 0;
    else
        requisition->height = (ig->child_height + ig->spacing) * ig->rows - ig->spacing + 2 * border;
    if (requisition->width > 0)
        requisition->width += 2 * border;

    if (ig->rows != old_rows || ig->columns != old_columns)
        gtk_widget_queue_resize(widget);
}

#if GTK_CHECK_VERSION(3, 0, 0)
static void panel_icon_grid_get_preferred_width(GtkWidget *widget,
                                                gint *minimal_width,
                                                gint *natural_width)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);
    GtkRequisition requisition;

    if (ig->orientation == GTK_ORIENTATION_VERTICAL)
    {
        if (minimal_width)
            *minimal_width = MIN(ig->target_dimension, ig->child_width);
        if (natural_width)
            *natural_width = ig->target_dimension;
        return;
    }
    panel_icon_grid_size_request(widget, &requisition);
    if (minimal_width)
        *minimal_width = requisition.width;
    if (natural_width)
        *natural_width = requisition.width;
}

static void panel_icon_grid_get_preferred_height(GtkWidget *widget,
                                                 gint *minimal_height,
                                                 gint *natural_height)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);
    GtkRequisition requisition;

    if (ig->orientation == GTK_ORIENTATION_HORIZONTAL)
        requisition.height = ig->target_dimension;
    else
        panel_icon_grid_size_request(widget, &requisition);
    if (minimal_height)
        *minimal_height = requisition.height;
    if (natural_height)
        *natural_height = requisition.height;
}
#endif

/* Add an icon grid element and establish its initial visibility. */
static void panel_icon_grid_add(GtkContainer *container, GtkWidget *widget)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(container);

    /* Insert at the tail of the child list.  This keeps the graphics in the order they were added. */
    ig->children = g_list_append(ig->children, widget);

    /* Add the widget to the layout container. */
    gtk_widget_set_parent(widget, GTK_WIDGET(container));
//    gtk_widget_queue_resize(GTK_WIDGET(container));
}

void panel_icon_grid_set_constrain_width(PanelIconGrid * ig, gboolean constrain_width)
{
    g_return_if_fail(PANEL_IS_ICON_GRID(ig));

    if ((!ig->constrain_width && !constrain_width) ||
        (ig->constrain_width && constrain_width))
        return;

    ig->constrain_width = !!constrain_width;
    gtk_widget_queue_resize(GTK_WIDGET(ig));
}

void panel_icon_grid_set_aspect_width(PanelIconGrid * ig, gboolean aspect_width)
{
    g_return_if_fail(PANEL_IS_ICON_GRID(ig));

    if ((!ig->aspect_width && !aspect_width) || (ig->aspect_width && aspect_width))
        return;

    ig->aspect_width = !!aspect_width;
    gtk_widget_queue_resize(GTK_WIDGET(ig));
}

/* void panel_icon_grid_set_fill_width(PanelIconGrid * ig, gboolean fill_width)
{
    g_return_if_fail(PANEL_IS_ICON_GRID(ig));

    if ((!ig->fill_width && !fill_width) || (ig->fill_width && fill_width))
        return;

    ig->fill_width = !!fill_width;
    gtk_widget_queue_resize(GTK_WIDGET(ig));
} */

/* Remove an icon grid element. */
static void panel_icon_grid_remove(GtkContainer *container, GtkWidget *widget)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(container);
    GList *children = ig->children;
    GtkWidget *child;

    while (children)
    {
        child = children->data;
        if (widget == child)
        {
            gboolean was_visible = gtk_widget_get_visible(widget);

            /* The child is found.  Remove from child list and layout container. */
            gtk_widget_unparent (widget);
            ig->children = g_list_remove_link(ig->children, children);
            g_list_free(children);

            /* Do a relayout if needed. */
            if (was_visible)
                gtk_widget_queue_resize(GTK_WIDGET(ig));
            break;
        }
        children = children->next;
    }
}

/* Get the index of an icon grid element. */
gint panel_icon_grid_get_child_position(PanelIconGrid * ig, GtkWidget * child)
{
    g_return_val_if_fail(PANEL_IS_ICON_GRID(ig), -1);

    return g_list_index(ig->children, child);
}

/* Reorder an icon grid element. */
void panel_icon_grid_reorder_child(PanelIconGrid * ig, GtkWidget * child, gint position)
{
    GList *old_link;
    GList *new_link;
    gint old_position;

    g_return_if_fail(PANEL_IS_ICON_GRID(ig));
    g_return_if_fail(GTK_IS_WIDGET(child));

    old_link = ig->children;
    old_position = 0;
    while (old_link)
    {
        if (old_link->data == child)
            break;
        old_link = old_link->next;
        old_position++;
    }

    g_return_if_fail(old_link != NULL);

    if (position == old_position)
        return;

    /* Remove the child from its current position. */
    ig->children = g_list_delete_link(ig->children, old_link);
    if (position < 0)
        new_link = NULL;
    else
        new_link = g_list_nth(ig->children, position);

    /* If the child was found, insert it at the new position. */
    ig->children = g_list_insert_before(ig->children, new_link, child);

    /* Do a relayout. */
    if (gtk_widget_get_visible(child) && gtk_widget_get_visible(GTK_WIDGET(ig)))
        gtk_widget_queue_resize(child);
}

/* Change the geometry of an icon grid. */
void panel_icon_grid_set_geometry(PanelIconGrid * ig,
    GtkOrientation orientation, gint child_width, gint child_height, gint spacing, gint border, gint target_dimension)
{
    g_return_if_fail(PANEL_IS_ICON_GRID(ig));

    gtk_container_set_border_width(GTK_CONTAINER(ig), border);

    if (ig->orientation == orientation && ig->child_width == child_width &&
            ig->child_height == child_height && ig->spacing == spacing &&
            ig->target_dimension == target_dimension)
        return;

    ig->orientation = orientation;
    ig->child_width = child_width;
    ig->child_height = child_height;
    ig->spacing = spacing;
    ig->target_dimension = target_dimension;
    gtk_widget_queue_resize(GTK_WIDGET(ig));
}

G_DEFINE_TYPE_WITH_CODE(PanelIconGrid, panel_icon_grid, GTK_TYPE_CONTAINER,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_ORIENTABLE, NULL));

static void panel_icon_grid_set_property(GObject *object, guint prop_id,
                                         const GValue *value, GParamSpec *pspec)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(object);
    gint spacing;
    GtkOrientation orientation;

    switch (prop_id)
    {
    case PROP_ORIENTATION:
        orientation = g_value_get_enum(value);
        if (orientation != ig->orientation)
        {
            ig->orientation = orientation;
            gtk_widget_queue_resize(GTK_WIDGET(ig));
        }
        break;
    case PROP_SPACING:
        spacing = g_value_get_int(value);
        if (spacing != ig->spacing)
        {
            ig->spacing = spacing;
            g_object_notify(object, "spacing");
            gtk_widget_queue_resize(GTK_WIDGET(ig));
        }
        break;
    case PROP_CONSTRAIN_WIDTH:
        panel_icon_grid_set_constrain_width(ig, g_value_get_boolean(value));
        break;
    case PROP_ASPECT_WIDTH:
        panel_icon_grid_set_aspect_width(ig, g_value_get_boolean(value));
        break;
    /* case PROP_FILL_WIDTH:
        panel_icon_grid_set_fill_width(ig, g_value_get_boolean(value));
        break; */
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void panel_icon_grid_get_property(GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(object);

    switch (prop_id)
    {
    case PROP_ORIENTATION:
        g_value_set_enum(value, ig->orientation);
        break;
    case PROP_SPACING:
        g_value_set_int(value, ig->spacing);
        break;
    case PROP_CONSTRAIN_WIDTH:
        g_value_set_boolean(value, ig->constrain_width);
        break;
    case PROP_ASPECT_WIDTH:
        g_value_set_boolean(value, ig->aspect_width);
        break;
    /* case PROP_FILL_WIDTH:
        g_value_set_boolean(value, ig->fill_width);
        break; */
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

/* realize()...expose() are taken from GtkEventBox implementation */
static void panel_icon_grid_realize(GtkWidget *widget)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);
    GdkWindow *window;
    GtkStyle *style;
    GtkAllocation allocation;
    GdkWindowAttr attributes;
    guint border = gtk_container_get_border_width(GTK_CONTAINER(widget));
    gint attributes_mask;
    gboolean visible_window;

#if GTK_CHECK_VERSION(2, 20, 0)
    gtk_widget_set_realized(widget, TRUE);
#else
    GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);
#endif

    gtk_widget_get_allocation(widget, &allocation);
    attributes.x = allocation.x + border;
    attributes.y = allocation.y + border;
    attributes.width = allocation.width - 2*border;
    attributes.height = allocation.height - 2*border;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events(widget)
                            | GDK_BUTTON_MOTION_MASK
                            | GDK_BUTTON_PRESS_MASK
                            | GDK_BUTTON_RELEASE_MASK
                            | GDK_EXPOSURE_MASK
                            | GDK_ENTER_NOTIFY_MASK
                            | GDK_LEAVE_NOTIFY_MASK;

    visible_window = gtk_widget_get_has_window(widget);
    if (visible_window)
    {
        attributes.visual = gtk_widget_get_visual(widget);
#if !GTK_CHECK_VERSION(3, 0, 0)
        attributes.colormap = gtk_widget_get_colormap(widget);
#endif
        attributes.wclass = GDK_INPUT_OUTPUT;

#if GTK_CHECK_VERSION(3, 0, 0)
        attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL;
#else
        attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
#endif

        window = gdk_window_new(gtk_widget_get_parent_window(widget),
                                &attributes, attributes_mask);
        gtk_widget_set_window(widget, window);
        gdk_window_set_user_data(window, widget);
    }
    else
    {
        window = gtk_widget_get_parent_window(widget);
        gtk_widget_set_window(widget, window);
        g_object_ref(window);

        attributes.wclass = GDK_INPUT_ONLY;
        attributes_mask = GDK_WA_X | GDK_WA_Y;

        ig->event_window = gdk_window_new(window, &attributes, attributes_mask);
        gdk_window_set_user_data(ig->event_window, widget);
    }

    style = gtk_style_attach(gtk_widget_get_style(widget), window);
    gtk_widget_set_style(widget, style);

    if (visible_window)
        gtk_style_set_background(style, window, GTK_STATE_NORMAL);
}

static void panel_icon_grid_unrealize(GtkWidget *widget)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);

    if (ig->event_window != NULL)
    {
        gdk_window_set_user_data(ig->event_window, NULL);
        gdk_window_destroy(ig->event_window);
        ig->event_window = NULL;
    }

    GTK_WIDGET_CLASS(panel_icon_grid_parent_class)->unrealize(widget);
}

static void panel_icon_grid_map(GtkWidget *widget)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);

    if (ig->event_window != NULL)
        gdk_window_show(ig->event_window);
    GTK_WIDGET_CLASS(panel_icon_grid_parent_class)->map(widget);
}

static void panel_icon_grid_unmap(GtkWidget *widget)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(widget);

    if (ig->event_window != NULL)
        gdk_window_hide(ig->event_window);
    GTK_WIDGET_CLASS(panel_icon_grid_parent_class)->unmap(widget);
}

#if GTK_CHECK_VERSION(3, 0, 0)
static gboolean panel_icon_grid_draw(GtkWidget *widget, cairo_t *cr)
#else
static gboolean panel_icon_grid_expose(GtkWidget *widget, GdkEventExpose *event)
#endif
{
    if (gtk_widget_is_drawable(widget))
    {
        if (gtk_widget_get_has_window(widget) &&
            !gtk_widget_get_app_paintable(widget))
#if GTK_CHECK_VERSION(3, 0, 0)
            gtk_render_background(gtk_widget_get_style_context(widget), cr, 0, 0,
                                  gtk_widget_get_allocated_width(widget),
                                  gtk_widget_get_allocated_height(widget));
#else
            gtk_paint_flat_box(gtk_widget_get_style(widget),
                               gtk_widget_get_window(widget),
                               gtk_widget_get_state(widget), GTK_SHADOW_NONE,
                               &event->area, widget, "panelicongrid",
                               0, 0, -1, -1);
#endif

#if GTK_CHECK_VERSION(3, 0, 0)
        GTK_WIDGET_CLASS(panel_icon_grid_parent_class)->draw(widget, cr);
#else
        GTK_WIDGET_CLASS(panel_icon_grid_parent_class)->expose_event(widget, event);
#endif
    }
    return FALSE;
}

static void panel_icon_grid_forall(GtkContainer *container,
                                   gboolean      include_internals,
                                   GtkCallback   callback,
                                   gpointer      callback_data)
{
    PanelIconGrid *ig = PANEL_ICON_GRID(container);
    GList *children = ig->children;
    GtkWidget *child;

    while (children)
    {
        child = children->data;
        children = children->next;
        (* callback)(child, callback_data);
    }
}

static GType panel_icon_grid_child_type(GtkContainer *container)
{
    return GTK_TYPE_WIDGET;
}

static void panel_icon_grid_class_init(PanelIconGridClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
    GtkContainerClass *container_class = GTK_CONTAINER_CLASS(klass);

    object_class->set_property = panel_icon_grid_set_property;
    object_class->get_property = panel_icon_grid_get_property;

#if GTK_CHECK_VERSION(3, 0, 0)
    widget_class->get_preferred_width = panel_icon_grid_get_preferred_width;
    widget_class->get_preferred_height = panel_icon_grid_get_preferred_height;
#else
    widget_class->size_request = panel_icon_grid_size_request;
#endif
    widget_class->size_allocate = panel_icon_grid_size_allocate;
    widget_class->realize = panel_icon_grid_realize;
    widget_class->unrealize = panel_icon_grid_unrealize;
    widget_class->map = panel_icon_grid_map;
    widget_class->unmap = panel_icon_grid_unmap;
#if GTK_CHECK_VERSION(3, 0, 0)
    widget_class->draw = panel_icon_grid_draw;
#else
    widget_class->expose_event = panel_icon_grid_expose;
#endif

    container_class->add = panel_icon_grid_add;
    container_class->remove = panel_icon_grid_remove;
    container_class->forall = panel_icon_grid_forall;
    container_class->child_type = panel_icon_grid_child_type;

    g_object_class_override_property(object_class,
                                     PROP_ORIENTATION,
                                     "orientation");
    g_object_class_install_property(object_class,
                                    PROP_SPACING,
                                    g_param_spec_int("spacing",
                                                     "Spacing",
                                                     "The amount of space between children",
                                                     0,
                                                     G_MAXINT,
                                                     0,
                                                     G_PARAM_READWRITE));
    g_object_class_install_property(object_class,
                                    PROP_CONSTRAIN_WIDTH,
                                    g_param_spec_boolean("constrain-width",
                                                         "Constrain width",
                                                         "Whether to constrain width by allocated space",
                                                         FALSE, G_PARAM_READWRITE));
    g_object_class_install_property(object_class,
                                    PROP_ASPECT_WIDTH,
                                    g_param_spec_boolean("aspect-width",
                                                         "Maintain children aspect",
                                                         "Whether to set children width to maintain their aspect",
                                                         FALSE, G_PARAM_READWRITE));
}

static void panel_icon_grid_init(PanelIconGrid *ig)
{
    gtk_widget_set_has_window(GTK_WIDGET(ig), FALSE);
    gtk_widget_set_redraw_on_allocate(GTK_WIDGET(ig), FALSE);

    ig->orientation = GTK_ORIENTATION_HORIZONTAL;
}

/* Establish an icon grid in a specified container widget.
 * The icon grid manages the contents of the container.
 * The orientation, geometry of the elements, and spacing can be varied.  All elements are the same size. */
GtkWidget * panel_icon_grid_new(
    GtkOrientation orientation, gint child_width, gint child_height, gint spacing, gint border, gint target_dimension)
{
    /* Create a structure representing the icon grid and collect the parameters. */
    PanelIconGrid * ig = g_object_new(PANEL_TYPE_ICON_GRID,
                                      "orientation", orientation,
                                      "spacing", spacing,
                                      "border-width", border,
                                      NULL);

    ig->child_width = child_width;
    ig->child_height = child_height;
    ig->target_dimension = target_dimension;

    return (GtkWidget *)ig;
}
