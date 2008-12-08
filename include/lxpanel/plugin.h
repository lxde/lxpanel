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

#ifndef LXPANEL_PLUGIN_H
#define LXPANEL_PLUGIN_H

#include <X11/Xlib.h>
#include <gtk/gtk.h>
#include <stdio.h>

typedef FbEv;
typedef FbBg;
typedef pair;

extern FbEv *fbev;
extern pair bool_pair[];

/* Panel */
enum { ALLIGN_NONE, ALLIGN_LEFT, ALLIGN_CENTER, ALLIGN_RIGHT  };
enum { EDGE_NONE, EDGE_LEFT, EDGE_RIGHT, EDGE_TOP, EDGE_BOTTOM };
enum { WIDTH_NONE, WIDTH_REQUEST, WIDTH_PIXEL, WIDTH_PERCENT };
enum { HEIGHT_NONE, HEIGHT_PIXEL, HEIGHT_REQUEST };
enum {
    ORIENT_NONE = -1,
    ORIENT_VERT = GTK_ORIENTATION_VERTICAL,
    ORIENT_HORIZ = GTK_ORIENTATION_HORIZONTAL
};
enum { POS_NONE, POS_START, POS_END };

typedef struct _Panel Panel;
struct _Panel{
    char* name;
    GtkWidget *topgwin;
    Window topxwin;
    GtkStyle *defstyle;
    GtkWidget *box;

    GtkRequisition requisition;
    GtkWidget *(*my_box_new) (gboolean, gint);
    GtkWidget *(*my_separator_new) ();

    FbBg *bg;
    int alpha;
    guint32 tintcolor;
    guint32 fontcolor;
    GdkColor gtintcolor;
    GdkColor gfontcolor;

    int ax, ay, aw, ah;
    int cx, cy, cw, ch;
    int allign, edge, margin;
    int orientation;
    int widthtype, width;
    int heighttype, height;

    guint config_changed : 1;
    guint self_destroy : 1;
    guint setdocktype : 1;
    guint setstrut : 1;
    guint round_corners : 1;
    guint usefontcolor : 1;
    guint transparent : 1;
    guint background : 1;
    guint spacing;

    int desknum;
    int curdesk;
    guint32 *workarea;
    int wa_len;

    char* background_file;

    int plug_num;
    GList *plugins;

    GSList* system_menus;

    GtkWidget* pref_dialog;
};

/* Plugin */
struct _Plugin;
typedef struct {
    char *fname;
    int count;
    GModule *gmodule;

    int dynamic : 1;
    int invisible : 1;
    char *type;
    char *name;
    char *version;
    char *description;

    int (*constructor)(struct _Plugin *this, char **fp);
    void (*destructor)(struct _Plugin *this);
    void (*config)(struct _Plugin *this, GtkWindow* parent);
    void (*save)(struct _Plugin *this, FILE* fp);
    void (*orientation)(struct _Plugin *this);
} PluginClass;

typedef struct _Plugin{
    PluginClass *class;
    Panel        *panel;
    GtkWidget    *pwid;
    int           expand;
    int           padding;
    int           border;
    gpointer      priv;
} Plugin;

/* misc */
enum {
    CONF_TYPE_STR,
    CONF_TYPE_INT,
    CONF_TYPE_BOOL,
    CONF_TYPE_FILE,
    CONF_TYPE_FILE_ENTRY
};

enum { LINE_NONE, LINE_BLOCK_START, LINE_BLOCK_END, LINE_VAR };

typedef struct {
    int num, len, type;
    gchar str[256];
    gchar *t[3];
} line;

/* Config Stuffs */
int str2num(pair *p, gchar *str, int defval);
gchar *num2str(pair *p, int num, gchar *defval);

extern int lxpanel_get_line(char **fp, line *s);
extern int lxpanel_put_line(FILE* fp, const char* format, ...);
#define lxpanel_put_str(fp, name, val) (G_UNLIKELY( !(val) || !*(val) )) ? 0 : lxpanel_put_line(fp, "%s=%s", name, val)
#define lxpanel_put_bool(fp, name, val) lxpanel_put_line(fp, "%s=%c", name, (val) ? '1' : '0')
#define lxpanel_put_int(fp, name, val) lxpanel_put_line(fp, "%s=%d", name, val)


/* For debuging */
#define ERR(fmt, args...) fprintf(stderr, fmt, ## args)
#define DBG2(fmt, args...) fprintf(stderr, "%s:%s:%-5d: " fmt, __FILE__,  __FUNCTION__, __LINE__, ## args)
#define ENTER2          do { fprintf(stderr, "%s:%s:%-5d: ENTER\n",  __FILE__,__FUNCTION__, __LINE__); } while(0)
#define RET2(args...)   do { fprintf(stderr, "%s:%s:%-5d: RETURN\n",  __FILE__,__FUNCTION__, __LINE__);\
return args; } while(0)

enum { LOG_NONE, LOG_ERR, LOG_WARN, LOG_INFO, LOG_ALL };
#ifdef DEBUG

#define ENTER          do { fprintf(stderr, "%s:%s:%-5d: ENTER\n",  __FILE__,__FUNCTION__, __LINE__); } while(0)
#define RET(args...)   do { fprintf(stderr, "%s:%s:%-5d: RETURN\n", __FILE__, __FUNCTION__, __LINE__);\
return args; } while(0)
#define DBG(fmt, args...) fprintf(stderr, "%s:%s:%-5d: " fmt,  __FILE__,__FUNCTION__, __LINE__, ## args)
#define LOG(level, fmt, args...) fprintf(stderr, fmt, ## args)

#else

extern int log_level;
#define ENTER         do {  } while(0)
#define RET(args...)   return args
#define DBG(fmt, args...)   do {  } while(0)
#define LOG(level, fmt, args...) do { if (level <= log_level) fprintf(stderr, fmt, ## args); } while(0)

#endif

/* X11 */
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
extern Atom a_LXPANEL_CMD; /* for private client message */

/* glib-mem */
#include <glib.h>

#if ! GLIB_CHECK_VERSION(2, 10, 0)
/* older versions of glib don't provde g_slice API */
#define g_slice_alloc(size)         g_malloc(size)
#define g_slice_alloc0(size)        g_malloc0(size)
#define g_slice_new(type)           g_new(type, 1)
#define g_slice_new0(type)          g_new0(type, 1)
#define g_slice_free(type, mem)     g_free(mem)
#define g_slice_free1(size, mem)    g_free(mem)
#endif

#endif
