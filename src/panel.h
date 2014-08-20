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

#ifndef __PANEL_H__
#define __PANEL_H__ 1

#include <X11/Xlib.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define LX_TYPE_PANEL                  (lxpanel_get_type())
#define LXPANEL(obj)                   (G_TYPE_CHECK_INSTANCE_CAST((obj), \
                                        LX_TYPE_PANEL, LXPanel))
#define LXPANEL_CLASS(klass)           (G_TYPE_CHECK_CLASS_CAST((klass), \
                                        LX_TYPE_PANEL, LXPanelClass))
#define LX_IS_PANEL(obj)               (G_TYPE_CHECK_INSTANCE_TYPE ((obj), \
                                        LX_TYPE_PANEL))

extern GType lxpanel_get_type          (void) G_GNUC_CONST;

/* A little trick to be compatible with some themes which rely on the
   PanelToplevel class, so we use LXPanel as alias for PanelToplevel */
typedef struct _LXPanel LXPanel;
typedef struct _LXPanel PanelToplevel;
typedef struct _LXPanelClass PanelToplevelClass;

typedef struct _Panel Panel;

struct _LXPanel
{
    GtkWindow window;
    Panel *priv;
};

struct _LXPanelClass
{
    GtkWindowClass parent_class;
};

/**
 * panel_apply_icon
 * @w: a window to apply
 *
 * Sets appropriate icon as the window icon for @w.
 */
extern void panel_apply_icon(GtkWindow *w);

/**
 * lxpanel_draw_label_text
 * @p: a panel instance
 * @label: a label widget
 * @text: (allow-none): text for the label
 * @bold: %TRUE if text should be bold
 * @custom_size_factor: scale factor for font size
 * @custom_color: %TRUE to use font color from panel settings
 *
 * Changes @label to contain @text with appropriate attributes using the
 * panel @p settings.
 */
extern void lxpanel_draw_label_text(LXPanel * p, GtkWidget * label, const char * text,
                                    gboolean bold, float custom_size_factor,
                                    gboolean custom_color);

/**
 * lxpanel_image_set_from_file
 * @p: a panel instance
 * @image: a #GtkImage widget
 * @file: image file path
 *
 * Applies icon from @file to @image in accordance with icon size setting
 * on panel @p.
 */
extern void lxpanel_image_set_from_file(LXPanel * p, GtkWidget * image, const char * file);

/**
 * lxpanel_image_set_icon_theme
 * @p: a panel instance
 * @image: a #GtkImage widget
 * @icon: icon name
 *
 * Applies icon size and theme from settings of @p to @image using @icon
 * name to select icon.
 */
extern gboolean lxpanel_image_set_icon_theme(LXPanel * p, GtkWidget * image, const gchar * icon);

/**
 * lxpanel_config_save
 * @p: a panel instance
 *
 * Immediately saves current configuration for panel @p.
 */
void lxpanel_config_save(LXPanel *p); /* defined in configurator.c */

/* Accessors APIs for Panel* */
extern GtkOrientation panel_get_orientation(LXPanel *panel);
extern gint panel_get_icon_size(LXPanel *panel);
extern gint panel_get_height(LXPanel *panel);
extern Window panel_get_xwindow(LXPanel *panel);
extern gint panel_get_monitor(LXPanel *panel);
extern GtkStyle *panel_get_defstyle(LXPanel *panel);
extern GtkIconTheme *panel_get_icon_theme(LXPanel *panel);
extern gboolean panel_is_at_bottom(LXPanel *panel);
extern gboolean panel_is_dynamic(LXPanel *panel);
extern GtkWidget *panel_box_new(LXPanel *panel, gboolean homogeneous, gint spacing);
extern GtkWidget *panel_separator_new(LXPanel *panel);

G_END_DECLS

#endif
