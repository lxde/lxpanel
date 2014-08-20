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

#ifndef __ICON_GRID_H__
#define __ICON_GRID_H__ 1

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define PANEL_TYPE_ICON_GRID               (panel_icon_grid_get_type())
#define PANEL_ICON_GRID(obj)               (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                                            PANEL_TYPE_ICON_GRID, PanelIconGrid))
#define PANEL_ICON_GRID_CLASS(klass)       (G_TYPE_CHECK_CLASS_CAST((klass), \
                                            PANEL_TYPE_ICON_GRID, PanelIconGridClass))
#define PANEL_IS_ICON_GRID(obj)            (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                                            PANEL_TYPE_ICON_GRID))
#define PANEL_IS_ICON_GRID_CLASS(klass)    (G_TYPE_CHECK_CLASS_TYPE ((klass), \
                                            PANEL_TYPE_ICON_GRID))

extern GType panel_icon_grid_get_type   (void) G_GNUC_CONST;

typedef struct _PanelIconGrid           PanelIconGrid;
typedef struct _PanelIconGridClass      PanelIconGridClass;

extern GtkWidget * panel_icon_grid_new(
    GtkOrientation orientation, gint child_width, gint child_height, gint spacing, gint border, gint target_dimension);
						/* Create an icon grid */
extern void panel_icon_grid_set_constrain_width(PanelIconGrid * ig, gboolean constrain_width);
						/* Set the constrain-width property */
/* extern void panel_icon_grid_set_fill_width(PanelIconGrid * ig, gboolean fill_width);
						 Set the fill-width property */
extern void panel_icon_grid_set_geometry(PanelIconGrid * ig,
    GtkOrientation orientation, gint child_width, gint child_height, gint spacing, gint border, gint target_dimension);
						/* Change the geometry of an icon grid */
extern gint panel_icon_grid_get_child_position(PanelIconGrid * ig, GtkWidget * child);
						/* Get the index of an icon grid element. */
extern void panel_icon_grid_reorder_child(PanelIconGrid * ig, GtkWidget * child, gint position);
						/* Reorder the position of a child in the icon grid */

G_END_DECLS

#endif
