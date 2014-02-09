/**
 * Copyright (c) 2006 LxDE Developers, see the file AUTHORS for details.
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

#ifndef PANEL_H
#define PANEL_H

#include <X11/Xlib.h>
#include <gtk/gtk.h>

extern gchar *cprofile;

extern Atom a_UTF8_STRING;
extern Atom a_XROOTPMAP_ID;

extern Atom a_WM_STATE;
extern Atom a_WM_CLASS;
extern Atom a_WM_DELETE_WINDOW;
extern Atom a_WM_PROTOCOLS;
extern Atom a_NET_WORKAREA;
extern Atom a_NET_CLIENT_LIST;
extern Atom a_NET_CLIENT_LIST_STACKING;
extern Atom a_NET_NUMBER_OF_DESKTOPS;
extern Atom a_NET_CURRENT_DESKTOP;
extern Atom a_NET_DESKTOP_VIEWPORT;
extern Atom a_NET_DESKTOP_NAMES;
extern Atom a_NET_ACTIVE_WINDOW;
extern Atom a_NET_CLOSE_WINDOW;
extern Atom a_NET_SHOWING_DESKTOP;
extern Atom a_NET_SUPPORTED;
extern Atom a_NET_WM_STATE;
extern Atom a_NET_WM_STATE_SKIP_TASKBAR;
extern Atom a_NET_WM_STATE_SKIP_PAGER;
extern Atom a_NET_WM_STATE_STICKY;
extern Atom a_NET_WM_STATE_HIDDEN;
extern Atom a_NET_WM_STATE_SHADED;

#define a_NET_WM_STATE_REMOVE        0    /* remove/unset property */
#define a_NET_WM_STATE_ADD           1    /* add/set property */
#define a_NET_WM_STATE_TOGGLE        2    /* toggle property  */

extern Atom a_NET_WM_WINDOW_TYPE;
extern Atom a_NET_WM_WINDOW_TYPE_DESKTOP;
extern Atom a_NET_WM_WINDOW_TYPE_DOCK;
extern Atom a_NET_WM_WINDOW_TYPE_TOOLBAR;
extern Atom a_NET_WM_WINDOW_TYPE_MENU;
extern Atom a_NET_WM_WINDOW_TYPE_UTILITY;
extern Atom a_NET_WM_WINDOW_TYPE_SPLASH;
extern Atom a_NET_WM_WINDOW_TYPE_DIALOG;
extern Atom a_NET_WM_WINDOW_TYPE_NORMAL;

extern Atom a_NET_WM_DESKTOP;
extern Atom a_NET_WM_NAME;
extern Atom a_NET_WM_VISIBLE_NAME;
extern Atom a_NET_WM_STRUT;
extern Atom a_NET_WM_STRUT_PARTIAL;
extern Atom a_NET_WM_ICON;
extern Atom a_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR;

extern Atom a_NET_SYSTEM_TRAY_OPCODE;
extern Atom a_NET_SYSTEM_TRAY_MESSAGE_DATA;
extern Atom a_NET_SYSTEM_TRAY_ORIENTATION;
extern Atom a_MANAGER;

extern Atom a_LXPANEL_CMD; /* for private client message */

typedef struct _Panel Panel;

extern void panel_apply_icon(GtkWindow *w);
extern void panel_destroy(Panel *p);
extern void panel_adjust_geometry_terminology(Panel *p);
extern void panel_determine_background_pixmap(Panel * p, GtkWidget * widget, GdkWindow * window);
extern void panel_draw_label_text(Panel * p, GtkWidget * label, char * text, gboolean bold,
        float custom_size_factor, gboolean custom_color);
extern void panel_establish_autohide(Panel *p);
extern void panel_image_set_from_file(Panel * p, GtkWidget * image, const char * file);
extern gboolean panel_image_set_icon_theme(Panel * p, GtkWidget * image, const gchar * icon);
extern void panel_set_wm_strut(Panel *p);
extern void panel_set_dock_type(Panel *p);
extern void panel_set_panel_configuration_changed(Panel *p);
extern void panel_update_background( Panel* p );

extern int panel_handle_x_error(Display * d, XErrorEvent * ev);
extern int panel_handle_x_error_swallow_BadWindow_BadDrawable(Display * d, XErrorEvent * ev);

/* Accessors APIs for Panel* */
extern gboolean panel_is_horizontal(Panel *panel);
extern gint panel_get_icon_size(Panel *panel);
extern gint panel_get_height(Panel *panel);
extern GtkWindow *panel_get_toplevel_window(Panel *panel);
extern GtkStyle *panel_get_defstyle(Panel *panel);
extern GtkIconTheme *panel_get_icon_theme(Panel *panel);
extern GtkWidget *panel_box_new(Panel *panel, gboolean homogeneous, gint spacing);
extern GtkWidget *panel_separator_new(Panel *panel);

#endif
