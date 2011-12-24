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

#include "panel.h"
#include "plugin.h"

enum _PluginConfType {
    CONF_TYPE_STR,
    CONF_TYPE_INT,
    CONF_TYPE_BOOL,
    CONF_TYPE_FILE,
    CONF_TYPE_FILE_ENTRY,
    CONF_TYPE_DIRECTORY_ENTRY,
    CONF_TYPE_TRIM
};

enum { LINE_NONE, LINE_BLOCK_START, LINE_BLOCK_END, LINE_VAR };

typedef struct {
    int num, len, type;
    gchar str[256];
    gchar *t[3];
} line;


typedef struct {
    int num;
    gchar *str;
} pair;

extern pair allign_pair[];
extern pair edge_pair[];
extern pair width_pair[];
extern pair height_pair[];
extern pair bool_pair[];
extern pair pos_pair[];

int str2num(pair *p, gchar *str, int defval);
gchar *num2str(pair *p, int num, gchar *defval);

extern int lxpanel_get_line(char **fp, line *s);
extern int lxpanel_put_line(FILE* fp, const char* format, ...);
#define lxpanel_put_str(fp, name, val) (G_UNLIKELY( !(val) || !*(val) )) ? 0 : lxpanel_put_line(fp, "%s=%s", name, val)
#define lxpanel_put_bool(fp, name, val) lxpanel_put_line(fp, "%s=%c", name, (val) ? '1' : '0')
#define lxpanel_put_int(fp, name, val) lxpanel_put_line(fp, "%s=%d", name, val)
//extern int lxpanel_put_str( FILE* fp, const char* name, const char* val );
//extern int lxpanel_put_bool( FILE* fp, const char* name, gboolean val );
//extern int lxpanel_put_int( FILE* fp, const char* name, int val );
int get_line_as_is(char **fp, line *s);

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

void calculate_position(Panel *np);
gchar *expand_tilda(gchar *file);

GtkWidget *_gtk_image_new_from_file_scaled(const gchar *file, gint width,
                                           gint height, gboolean keep_ratio);
void get_button_spacing(GtkRequisition *req, GtkContainer *parent, gchar *name);
guint32 gcolor2rgb24(GdkColor *color);
GtkWidget * fb_button_new_from_file(
    gchar * image_file, int width, int height, gulong highlight_color, gboolean keep_ratio);
GtkWidget * fb_button_new_from_file_with_label(
    gchar * image_file, int width, int height, gulong highlight_color, gboolean keep_ratio, Panel * panel, gchar * label);

char* translate_exec_to_cmd( const char* exec, const char* icon,
                             const char* title, const char* fpath );

/*
 This function is used to re-create a new box with different
 orientation from the old one, add all children of the old one to
 the new one, and then destroy the old box.
 It's mainly used when we need to change the orientation of the panel or
 any plugin with a layout box. Since GtkHBox cannot be changed to GtkVBox,
 recreating a new box to replace the old one is required.
*/
GtkWidget* recreate_box( GtkBox* oldbox, GtkOrientation orientation );

void show_error( GtkWindow* parent_win, const char* msg );

/* Parameters: const char* name, gpointer ret_value, GType type, ....NULL */
GtkWidget* create_generic_config_dlg( const char* title, GtkWidget* parent,
                              GSourceFunc apply_func, Plugin * plugin,
                      const char* name, ... );


char* get_config_file( const char* profile, const char* file_name, gboolean is_global );

extern GtkMenu* lxpanel_get_panel_menu( Panel* panel, Plugin* plugin, gboolean use_sub_menu );

extern GdkPixbuf* lxpanel_load_icon( const char* name, int width, int height, gboolean use_fallback );

void fb_button_set_from_file(GtkWidget* btn, const char* img_file, gint width, gint height, gboolean keep_ratio);

gboolean lxpanel_launch_app(const char* exec, GList* files, gboolean in_terminal);

#endif
