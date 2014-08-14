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

#ifndef MISC_H
#define MISC_H

#include <X11/Xatom.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <stdio.h>
#include <libfm/fm.h>

#include "panel.h"

/* Decoded value of WM_STATE property. */
typedef struct {
    unsigned int modal : 1;
    unsigned int sticky : 1;
    unsigned int maximized_vert : 1;
    unsigned int maximized_horz : 1;
    unsigned int shaded : 1;
    unsigned int skip_taskbar : 1;
    unsigned int skip_pager : 1;
    unsigned int hidden : 1;
    unsigned int fullscreen : 1;
    unsigned int above : 1;
    unsigned int below : 1;
} NetWMState;

/* Decoded value of _NET_WM_WINDOW_TYPE property. */
typedef struct {
    unsigned int desktop : 1;
    unsigned int dock : 1;
    unsigned int toolbar : 1;
    unsigned int menu : 1;
    unsigned int utility : 1;
    unsigned int splash : 1;
    unsigned int dialog : 1;
    unsigned int normal : 1;
} NetWMWindowType;

void Xclimsg(Window win, Atom type, long l0, long l1, long l2, long l3, long l4);
void Xclimsgwm(Window win, Atom type, Atom arg);
void *get_xaproperty (Window win, Atom prop, Atom type, int *nitems);
char *get_textproperty(Window win, Atom prop);
void *get_utf8_property(Window win, Atom atom);
char **get_utf8_property_list(Window win, Atom atom, int *count);

void resolve_atoms();
//Window Select_Window(Display *dpy);
int get_net_number_of_desktops();
int get_net_current_desktop ();
int get_net_wm_desktop(Window win);
int get_wm_state (Window win);
void get_net_wm_state(Window win, NetWMState *nws);
void get_net_wm_window_type(Window win, NetWMWindowType *nwwt);
GPid get_net_wm_pid(Window win);

gchar *expand_tilda(const gchar *file);

void get_button_spacing(GtkRequisition *req, GtkContainer *parent, gchar *name);
guint32 gcolor2rgb24(GdkColor *color);
GtkWidget *lxpanel_button_new_for_icon(LXPanel *panel, const gchar *name, GdkColor *color, const gchar *label);
GtkWidget *lxpanel_button_new_for_fm_icon(LXPanel *panel, FmIcon *icon, GdkColor *color, const gchar *label);
void lxpanel_button_set_icon(GtkWidget* btn, const gchar *name, gint size);
void lxpanel_button_update_icon(GtkWidget* btn, FmIcon *icon, gint size);

typedef enum {
    CONF_TYPE_STR,
    CONF_TYPE_INT,
    CONF_TYPE_BOOL,
    CONF_TYPE_FILE,
    CONF_TYPE_FILE_ENTRY,
    CONF_TYPE_DIRECTORY_ENTRY,
    CONF_TYPE_TRIM
} PluginConfType;

/* Parameters: const char* name, gpointer ret_value, PluginConfType type, ....NULL */
extern GtkWidget *lxpanel_generic_config_dlg(const char *title, LXPanel *panel,
                                             GSourceFunc apply_func,
                                             GtkWidget *plugin,
                                             const char *name, ...);

#endif
