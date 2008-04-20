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

#include <X11/Xatom.h>
#include <X11/cursorfont.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "misc.h"
#include "panel.h"

//#define DEBUG
#include "dbg.h"


/* X11 data types */
Atom a_UTF8_STRING;
Atom a_XROOTPMAP_ID;

/* old WM spec */
Atom a_WM_STATE;
Atom a_WM_CLASS;
Atom a_WM_DELETE_WINDOW;
Atom a_WM_PROTOCOLS;

/* new NET spec */
Atom a_NET_WORKAREA;
Atom a_NET_CLIENT_LIST;
Atom a_NET_CLIENT_LIST_STACKING;
Atom a_NET_NUMBER_OF_DESKTOPS;
Atom a_NET_CURRENT_DESKTOP;
Atom a_NET_DESKTOP_VIEWPORT;
Atom a_NET_DESKTOP_NAMES;
Atom a_NET_ACTIVE_WINDOW;
Atom a_NET_CLOSE_WINDOW;
Atom a_NET_SUPPORTED;
Atom a_NET_WM_STATE;
Atom a_NET_WM_STATE_SKIP_TASKBAR;
Atom a_NET_WM_STATE_SKIP_PAGER;
Atom a_NET_WM_STATE_STICKY;
Atom a_NET_WM_STATE_HIDDEN;
Atom a_NET_WM_STATE_SHADED;
Atom a_NET_WM_WINDOW_TYPE;
Atom a_NET_WM_WINDOW_TYPE_DESKTOP;
Atom a_NET_WM_WINDOW_TYPE_DOCK;
Atom a_NET_WM_WINDOW_TYPE_TOOLBAR;
Atom a_NET_WM_WINDOW_TYPE_MENU;
Atom a_NET_WM_WINDOW_TYPE_UTILITY;
Atom a_NET_WM_WINDOW_TYPE_SPLASH;
Atom a_NET_WM_WINDOW_TYPE_DIALOG;
Atom a_NET_WM_WINDOW_TYPE_NORMAL;
Atom a_NET_WM_DESKTOP;
Atom a_NET_WM_PID;
Atom a_NET_WM_NAME;
Atom a_NET_WM_VISIBLE_NAME;
Atom a_NET_WM_STRUT;
Atom a_NET_WM_STRUT_PARTIAL;
Atom a_NET_WM_ICON;
Atom a_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR;

Atom a_LXPANEL_CMD; /* for private client message */

enum{
    I_UTF8_STRING,
    I_XROOTPMAP_ID,
    I_WM_STATE,
    I_WM_CLASS,
    I_WM_DELETE_WINDOW,
    I_WM_PROTOCOLS,
    I_NET_WORKAREA,
    I_NET_CLIENT_LIST,
    I_NET_CLIENT_LIST_STACKING,
    I_NET_NUMBER_OF_DESKTOPS,
    I_NET_CURRENT_DESKTOP,
    I_NET_DESKTOP_NAMES,
    I_NET_ACTIVE_WINDOW,
    I_NET_SUPPORTED,

    I_NET_WM_STATE,
    I_NET_WM_STATE_SKIP_TASKBAR,
    I_NET_WM_STATE_SKIP_PAGER,
    I_NET_WM_STATE_STICKY,
    I_NET_WM_STATE_HIDDEN,
    I_NET_WM_STATE_SHADED,
    I_NET_WM_WINDOW_TYPE,

    I_NET_WM_WINDOW_TYPE_DESKTOP,
    I_NET_WM_WINDOW_TYPE_DOCK,
    I_NET_WM_WINDOW_TYPE_TOOLBAR,
    I_NET_WM_WINDOW_TYPE_MENU,
    I_NET_WM_WINDOW_TYPE_UTILITY,
    I_NET_WM_WINDOW_TYPE_SPLASH,
    I_NET_WM_WINDOW_TYPE_DIALOG,
    I_NET_WM_WINDOW_TYPE_NORMAL,
    I_NET_WM_DESKTOP,
    I_NET_WM_PID,
    I_NET_WM_NAME,
    I_NET_WM_VISIBLE_NAME,
    I_NET_WM_STRUT,
    I_NET_WM_STRUT_PARTIAL,
    I_NET_WM_ICON,
    I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR,

    I_LXPANEL_CMD,
    N_ATOMS
};

pair allign_pair[] = {
    { ALLIGN_NONE, "none" },
    { ALLIGN_LEFT, "left" },
    { ALLIGN_RIGHT, "right" },
    { ALLIGN_CENTER, "center"},
    { 0, NULL },
};

pair edge_pair[] = {
    { EDGE_NONE, "none" },
    { EDGE_LEFT, "left" },
    { EDGE_RIGHT, "right" },
    { EDGE_TOP, "top" },
    { EDGE_BOTTOM, "bottom" },
    { 0, NULL },
};

pair width_pair[] = {
    { WIDTH_NONE, "none" },
    { WIDTH_REQUEST, "request" },
    { WIDTH_PIXEL, "pixel" },
    { WIDTH_PERCENT, "percent" },
    { 0, NULL },
};

pair height_pair[] = {
    { HEIGHT_NONE, "none" },
    { HEIGHT_PIXEL, "pixel" },
    { 0, NULL },
};

pair bool_pair[] = {
    { 0, "0" },
    { 1, "1" },
    { 0, NULL },
};
pair pos_pair[] = {
    { POS_NONE, "none" },
    { POS_START, "start" },
    { POS_END,  "end" },
    { 0, NULL},
};


int
str2num(pair *p, gchar *str, int defval)
{
    ENTER;
    for (;p && p->str; p++) {
        if (!g_ascii_strcasecmp(str, p->str))
            RET(p->num);
    }
    RET(defval);
}

gchar *
num2str(pair *p, int num, gchar *defval)
{
    ENTER;
    for (;p && p->str; p++) {
        if (num == p->num)
            RET(p->str);
    }
    RET(defval);
}

int buf_gets( char* buf, int len, char **fp )
{
    char* p;
    int i = 0;
    if( !fp || !(p = *fp) || !**fp )
    {
        buf[0] = '\0';
        return 0;
    }

    do
    {
        if( G_LIKELY( i < len ) )
        {
            buf[i] = *p;
            ++i;
        }
        if( G_UNLIKELY(*p == '\n') )
        {
            ++p;
            break;
        }
    }while( *(++p) );
    buf[i] = '\0';
    *fp = p;
    return i;
}

extern int
lxpanel_put_line(FILE* fp, const char* format, ...)
{
    static int indent = 0;
    int i, ret;
    va_list args;

    if( strchr(format, '}') )
        --indent;

    for( i = 0; i < indent; ++i )
        fputs( "    ", fp );

    va_start (args, format);
    ret = vfprintf (fp, format, args);
    va_end (args);

    if( strchr(format, '{') )
        ++indent;
    fputc( '\n', fp );  /* add line break */
    return (ret + 1);
}

extern int
lxpanel_put_str( FILE* fp, const char* name, const char* val )
{
    if( G_UNLIKELY( !val || !*val ) )
        return 0;
    return lxpanel_put_line( fp, "%s=%s", name, val );
}

extern int
lxpanel_put_bool( FILE* fp, const char* name, gboolean val )
{
    return lxpanel_put_line( fp, "%s=%c", name, val ? '1' : '0' );
}

extern int
lxpanel_put_int( FILE* fp, const char* name, int val )
{
    return lxpanel_put_line( fp, "%s=%d", name, val );
}

extern  int
lxpanel_get_line(char**fp, line *s)
{
    gchar *tmp, *tmp2;
    ENTER;
    s->type = LINE_NONE;
    if (!fp)
        RET(s->type);
    while (buf_gets(s->str, s->len, fp)) {

        g_strstrip(s->str);

        if (s->str[0] == '#' || s->str[0] == 0) {
            continue;
        }
        if (!g_ascii_strcasecmp(s->str, "}")) {
            s->type = LINE_BLOCK_END;
            break;
        }

        s->t[0] = s->str;
        for (tmp = s->str; isalnum(*tmp); tmp++);
        for (tmp2 = tmp; isspace(*tmp2); tmp2++);
        if (*tmp2 == '=') {
            for (++tmp2; isspace(*tmp2); tmp2++);
            s->t[1] = tmp2;
            *tmp = 0;
            s->type = LINE_VAR;
        } else if  (*tmp2 == '{') {
            *tmp = 0;
            s->type = LINE_BLOCK_START;
        } else {
            ERR( "parser: unknown token: '%c'\n", *tmp2);
        }
        break;
    }
    RET(s->type);
}

int
get_line_as_is(char** fp, line *s)
{
    gchar *tmp, *tmp2;

    ENTER;
    if (!fp) {
        s->type = LINE_NONE;
        RET(s->type);
    }
    s->type = LINE_NONE;
    while (buf_gets(s->str, s->len, fp)) {
        g_strstrip(s->str);
        if (s->str[0] == '#' || s->str[0] == 0)
        continue;
        DBG( ">> %s\n", s->str);
        if (!g_ascii_strcasecmp(s->str, "}")) {
            s->type = LINE_BLOCK_END;
            DBG( "        : line_block_end\n");
            break;
        }
        for (tmp = s->str; isalnum(*tmp); tmp++);
        for (tmp2 = tmp; isspace(*tmp2); tmp2++);
        if (*tmp2 == '=') {
            s->type = LINE_VAR;
        } else if  (*tmp2 == '{') {
            s->type = LINE_BLOCK_START;
        } else {
            DBG( "        : ? <%c>\n", *tmp2);
        }
        break;
    }
    RET(s->type);

}

void resolve_atoms()
{
    static const char* atom_names[ N_ATOMS ];

    atom_names[ I_UTF8_STRING ] = "UTF8_STRING";
    atom_names[ I_XROOTPMAP_ID ] = "_XROOTPMAP_ID";
    atom_names[ I_WM_STATE ] = "WM_STATE";
    atom_names[ I_WM_CLASS ] = "WM_CLASS";
    atom_names[ I_WM_DELETE_WINDOW ] = "WM_DELETE_WINDOW";
    atom_names[ I_WM_PROTOCOLS ] = "WM_PROTOCOLS";
    atom_names[ I_NET_WORKAREA ] = "_NET_WORKAREA";
    atom_names[ I_NET_CLIENT_LIST ] = "_NET_CLIENT_LIST";
    atom_names[ I_NET_CLIENT_LIST_STACKING ] = "_NET_CLIENT_LIST_STACKING";
    atom_names[ I_NET_NUMBER_OF_DESKTOPS ] = "_NET_NUMBER_OF_DESKTOPS";
    atom_names[ I_NET_CURRENT_DESKTOP ] = "_NET_CURRENT_DESKTOP";
    atom_names[ I_NET_DESKTOP_NAMES ] = "_NET_DESKTOP_NAMES";
    atom_names[ I_NET_ACTIVE_WINDOW ] = "_NET_ACTIVE_WINDOW";
    atom_names[ I_NET_SUPPORTED ] = "_NET_SUPPORTED";
    atom_names[ I_NET_WM_DESKTOP ] = "_NET_WM_DESKTOP";
    atom_names[ I_NET_WM_STATE ] = "_NET_WM_STATE";
    atom_names[ I_NET_WM_STATE_SKIP_TASKBAR ] = "_NET_WM_STATE_SKIP_TASKBAR";
    atom_names[ I_NET_WM_STATE_SKIP_PAGER ] = "_NET_WM_STATE_SKIP_PAGER";
    atom_names[ I_NET_WM_STATE_STICKY ] = "_NET_WM_STATE_STICKY";
    atom_names[ I_NET_WM_STATE_HIDDEN ] = "_NET_WM_STATE_HIDDEN";
    atom_names[ I_NET_WM_STATE_SHADED ] = "_NET_WM_STATE_SHADED";
    atom_names[ I_NET_WM_WINDOW_TYPE ] = "_NET_WM_WINDOW_TYPE";

    atom_names[ I_NET_WM_WINDOW_TYPE_DESKTOP ] = "_NET_WM_WINDOW_TYPE_DESKTOP";
    atom_names[ I_NET_WM_WINDOW_TYPE_DOCK ] = "_NET_WM_WINDOW_TYPE_DOCK";
    atom_names[ I_NET_WM_WINDOW_TYPE_TOOLBAR ] = "_NET_WM_WINDOW_TYPE_TOOLBAR";
    atom_names[ I_NET_WM_WINDOW_TYPE_MENU ] = "_NET_WM_WINDOW_TYPE_MENU";
    atom_names[ I_NET_WM_WINDOW_TYPE_UTILITY ] = "_NET_WM_WINDOW_TYPE_UTILITY";
    atom_names[ I_NET_WM_WINDOW_TYPE_SPLASH ] = "_NET_WM_WINDOW_TYPE_SPLASH";
    atom_names[ I_NET_WM_WINDOW_TYPE_DIALOG ] = "_NET_WM_WINDOW_TYPE_DIALOG";
    atom_names[ I_NET_WM_WINDOW_TYPE_NORMAL ] = "_NET_WM_WINDOW_TYPE_NORMAL";
    atom_names[ I_NET_WM_DESKTOP ] = "_NET_WM_DESKTOP";
    atom_names[ I_NET_WM_PID ] = "_NET_WM_PID";
    atom_names[ I_NET_WM_NAME ] = "_NET_WM_NAME";
    atom_names[ I_NET_WM_VISIBLE_NAME ] = "_NET_WM_VISIBLE_NAME";
    atom_names[ I_NET_WM_STRUT ] = "_NET_WM_STRUT";
    atom_names[ I_NET_WM_STRUT_PARTIAL ] = "_NET_WM_STRUT_PARTIAL";
    atom_names[ I_NET_WM_ICON ] = "_NET_WM_ICON";
    atom_names[ I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR ] = "_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR";
    atom_names[ I_LXPANEL_CMD ] = "_LXPANEL_CMD";

    Atom atoms[ N_ATOMS ];

    ENTER;

    if( !  XInternAtoms( GDK_DISPLAY(), atom_names, N_ATOMS, False, atoms ) )
    {
        g_warning( "Error: unable to return Atoms" );
        return;
    }

    a_UTF8_STRING = atoms[ I_UTF8_STRING ];
    a_XROOTPMAP_ID = atoms[ I_XROOTPMAP_ID ];
    a_WM_STATE = atoms[ I_WM_STATE ];
    a_WM_CLASS = atoms[ I_WM_CLASS ];
    a_WM_DELETE_WINDOW = atoms[ I_WM_DELETE_WINDOW ];
    a_WM_PROTOCOLS = atoms[ I_WM_PROTOCOLS ];
    a_NET_WORKAREA = atoms[ I_NET_WORKAREA ];
    a_NET_CLIENT_LIST = atoms[ I_NET_CLIENT_LIST ];
    a_NET_CLIENT_LIST_STACKING = atoms[ I_NET_CLIENT_LIST_STACKING ];
    a_NET_NUMBER_OF_DESKTOPS = atoms[ I_NET_NUMBER_OF_DESKTOPS ];
    a_NET_CURRENT_DESKTOP = atoms[ I_NET_CURRENT_DESKTOP ];
    a_NET_DESKTOP_NAMES = atoms[ I_NET_DESKTOP_NAMES ];
    a_NET_ACTIVE_WINDOW = atoms[ I_NET_ACTIVE_WINDOW ];
    a_NET_SUPPORTED = atoms[ I_NET_SUPPORTED ];
    a_NET_WM_STATE = atoms[ I_NET_WM_STATE ];
    a_NET_WM_STATE_SKIP_TASKBAR = atoms[ I_NET_WM_STATE_SKIP_TASKBAR ];
    a_NET_WM_STATE_SKIP_PAGER = atoms[ I_NET_WM_STATE_SKIP_PAGER ];
    a_NET_WM_STATE_STICKY = atoms[ I_NET_WM_STATE_STICKY ];
    a_NET_WM_STATE_HIDDEN = atoms[ I_NET_WM_STATE_HIDDEN ];
    a_NET_WM_STATE_SHADED = atoms[ I_NET_WM_STATE_SHADED ];
    a_NET_WM_WINDOW_TYPE = atoms[ I_NET_WM_WINDOW_TYPE ];

    a_NET_WM_WINDOW_TYPE_DESKTOP = atoms[ I_NET_WM_WINDOW_TYPE_DESKTOP ];
    a_NET_WM_WINDOW_TYPE_DOCK = atoms[ I_NET_WM_WINDOW_TYPE_DOCK ];
    a_NET_WM_WINDOW_TYPE_TOOLBAR = atoms[ I_NET_WM_WINDOW_TYPE_TOOLBAR ];
    a_NET_WM_WINDOW_TYPE_MENU = atoms[ I_NET_WM_WINDOW_TYPE_MENU ];
    a_NET_WM_WINDOW_TYPE_UTILITY = atoms[ I_NET_WM_WINDOW_TYPE_UTILITY ];
    a_NET_WM_WINDOW_TYPE_SPLASH = atoms[ I_NET_WM_WINDOW_TYPE_SPLASH ];
    a_NET_WM_WINDOW_TYPE_DIALOG = atoms[ I_NET_WM_WINDOW_TYPE_DIALOG ];
    a_NET_WM_WINDOW_TYPE_NORMAL = atoms[ I_NET_WM_WINDOW_TYPE_NORMAL ];
    a_NET_WM_DESKTOP = atoms[ I_NET_WM_DESKTOP ];
    a_NET_WM_NAME = atoms[ I_NET_WM_NAME ];
    a_NET_WM_VISIBLE_NAME = atoms[ I_NET_WM_VISIBLE_NAME ];
    a_NET_WM_STRUT = atoms[ I_NET_WM_STRUT ];
    a_NET_WM_STRUT_PARTIAL = atoms[ I_NET_WM_STRUT_PARTIAL ];
    a_NET_WM_ICON = atoms[ I_NET_WM_ICON ];
    a_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR = atoms[ I_KDE_NET_WM_SYSTEM_TRAY_WINDOW_FOR ];

    a_LXPANEL_CMD = atoms[ I_LXPANEL_CMD ];

    RET();
}


void
Xclimsg(Window win, long type, long l0, long l1, long l2, long l3, long l4)
{
    XClientMessageEvent xev;

    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = type;
    xev.format = 32;
    xev.data.l[0] = l0;
    xev.data.l[1] = l1;
    xev.data.l[2] = l2;
    xev.data.l[3] = l3;
    xev.data.l[4] = l4;
    XSendEvent(GDK_DISPLAY(), GDK_ROOT_WINDOW(), False,
          (SubstructureNotifyMask | SubstructureRedirectMask),
          (XEvent *) & xev);
}

void
Xclimsgwm(Window win, Atom type, Atom arg)
{
    XClientMessageEvent xev;

    xev.type = ClientMessage;
    xev.window = win;
    xev.message_type = type;
    xev.format = 32;
    xev.data.l[0] = arg;
    xev.data.l[1] = GDK_CURRENT_TIME;
    XSendEvent(GDK_DISPLAY(), win, False, 0L, (XEvent *) &xev);
}


void *
get_utf8_property(Window win, Atom atom)
{

    Atom type;
    int format;
    gulong nitems;
    gulong bytes_after;
    gchar *val, *retval;
    int result;
    guchar *tmp = NULL;

    type = None;
    retval = NULL;
    result = XGetWindowProperty (GDK_DISPLAY(), win, atom, 0, G_MAXLONG, False,
          a_UTF8_STRING, &type, &format, &nitems,
          &bytes_after, &tmp);
    if (result != Success || type == None)
        return NULL;
    val = (gchar *) tmp;
    if (val) {
        if (type == a_UTF8_STRING && format == 8 && nitems != 0)
            retval = g_strndup (val, nitems);
        XFree (val);
    }
    return retval;

}

char **
get_utf8_property_list(Window win, Atom atom, int *count)
{
    Atom type;
    int format, i;
    gulong nitems;
    gulong bytes_after;
    gchar *s, **retval = NULL;
    int result;
    guchar *tmp = NULL;

    *count = 0;
    result = XGetWindowProperty(GDK_DISPLAY(), win, atom, 0, G_MAXLONG, False,
          a_UTF8_STRING, &type, &format, &nitems,
          &bytes_after, &tmp);
    if (result != Success || type != a_UTF8_STRING || tmp == NULL)
        return NULL;

    if (nitems) {
        gchar *val = (gchar *) tmp;
        DBG("res=%d(%d) nitems=%d val=%s\n", result, Success, nitems, val);
        for (i = 0; i < nitems; i++) {
            if (!val[i])
                (*count)++;
        }
        retval = g_new0 (char*, *count + 2);
        for (i = 0, s = val; i < *count; i++, s = s +  strlen (s) + 1) {
            retval[i] = g_strdup(s);
        }
        if (val[nitems-1]) {
            result = nitems - (s - val);
            DBG("val does not ends by 0, moving last %d bytes\n", result);
            g_memmove(s - 1, s, result);
            val[nitems-1] = 0;
            DBG("s=%s\n", s -1);
            retval[i] = g_strdup(s - 1);
            (*count)++;
        }
    }
    XFree (tmp);

    return retval;

}

void *
get_xaproperty (Window win, Atom prop, Atom type, int *nitems)
{
    Atom type_ret;
    int format_ret;
    unsigned long items_ret;
    unsigned long after_ret;
    unsigned char *prop_data;

    ENTER;
    prop_data = NULL;
    if (XGetWindowProperty (GDK_DISPLAY(), win, prop, 0, 0x7fffffff, False,
              type, &type_ret, &format_ret, &items_ret,
              &after_ret, &prop_data) != Success)
    {
        if( G_UNLIKELY(prop_data) )
            XFree( prop_data );
        if( nitems )
            *nitems = 0;
        RET(NULL);
    }
    if (nitems)
        *nitems = items_ret;
    RET(prop_data);
}

static char*
text_property_to_utf8 (const XTextProperty *prop)
{
  char **list;
  int count;
  char *retval;

  ENTER;
  list = NULL;
  count = gdk_text_property_to_utf8_list (gdk_x11_xatom_to_atom (prop->encoding),
                                          prop->format,
                                          prop->value,
                                          prop->nitems,
                                          &list);

  DBG("count=%d\n", count);
  if (count == 0)
    return NULL;

  retval = list[0];
  list[0] = g_strdup (""); /* something to free */

  g_strfreev (list);

  RET(retval);
}

char *
get_textproperty(Window win, Atom atom)
{
    XTextProperty text_prop;
    char *retval;

    ENTER;
    if (XGetTextProperty(GDK_DISPLAY(), win, &text_prop, atom)) {
        DBG("format=%d enc=%d nitems=%d value=%s   \n",
              text_prop.format,
              text_prop.encoding,
              text_prop.nitems,
              text_prop.value);
        retval = text_property_to_utf8 (&text_prop);
        if (text_prop.nitems > 0)
            XFree (text_prop.value);
        RET(retval);

    }
    RET(NULL);
}


int
get_net_number_of_desktops()
{
    int desknum;
    guint32 *data;

    ENTER;
    data = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_NUMBER_OF_DESKTOPS,
          XA_CARDINAL, 0);
    if (!data)
        RET(0);

    desknum = *data;
    XFree (data);
    RET(desknum);
}


int
get_net_current_desktop ()
{
    int desk;
    guint32 *data;

    ENTER;
    data = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CURRENT_DESKTOP, XA_CARDINAL, 0);
    if (!data)
        RET(0);

    desk = *data;
    XFree (data);
    RET(desk);
}

int
get_net_wm_desktop(Window win)
{
    int desk = 0;
    guint32 *data;

    ENTER;
    data = get_xaproperty (win, a_NET_WM_DESKTOP, XA_CARDINAL, 0);
    if (data) {
        desk = *data;
        XFree (data);
    }
    RET(desk);
}

GPid
get_net_wm_pid(Window win)
{
    GPid pid = 0;
    guint32 *data;

    ENTER;
    data = get_xaproperty (win, a_NET_WM_PID, XA_CARDINAL, 0);
    if (data) {
        pid = *data;
        XFree (data);
    }
    RET(pid);
}

void
get_net_wm_state(Window win, NetWMState *nws)
{
    Atom *state;
    int num3;


    ENTER;
    bzero(nws, sizeof(nws));
    if (!(state = get_xaproperty(win, a_NET_WM_STATE, XA_ATOM, &num3)))
        RET();

    DBG( "%x: netwm state = { ", (unsigned int)win);
    while (--num3 >= 0) {
        if (state[num3] == a_NET_WM_STATE_SKIP_PAGER) {
            DBG("NET_WM_STATE_SKIP_PAGER ");
            nws->skip_pager = 1;
        } else if (state[num3] == a_NET_WM_STATE_SKIP_TASKBAR) {
            DBG( "NET_WM_STATE_SKIP_TASKBAR ");
        nws->skip_taskbar = 1;
    } else if (state[num3] == a_NET_WM_STATE_STICKY) {
            DBG( "NET_WM_STATE_STICKY ");
        nws->sticky = 1;
        } else if (state[num3] == a_NET_WM_STATE_HIDDEN) {
            DBG( "NET_WM_STATE_HIDDEN ");
            nws->hidden = 1;
    } else if (state[num3] == a_NET_WM_STATE_SHADED) {
            DBG( "NET_WM_STATE_SHADED ");
            nws->shaded = 1;
    } else {
        DBG( "... ");
    }
    }
    XFree(state);
    DBG( "}\n");
    RET();
}




void
get_net_wm_window_type(Window win, NetWMWindowType *nwwt)
{
    Atom *state;
    int num3;


    ENTER;
    bzero(nwwt, sizeof(nwwt));
    if (!(state = get_xaproperty(win, a_NET_WM_WINDOW_TYPE, XA_ATOM, &num3)))
        RET();

    DBG( "%x: netwm state = { ", (unsigned int)win);
    while (--num3 >= 0) {
        if (state[num3] == a_NET_WM_WINDOW_TYPE_DESKTOP) {
            DBG("NET_WM_WINDOW_TYPE_DESKTOP ");
            nwwt->desktop = 1;
        } else if (state[num3] == a_NET_WM_WINDOW_TYPE_DOCK) {
            DBG( "NET_WM_WINDOW_TYPE_DOCK ");
        nwwt->dock = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_TOOLBAR) {
            DBG( "NET_WM_WINDOW_TYPE_TOOLBAR ");
        nwwt->toolbar = 1;
        } else if (state[num3] == a_NET_WM_WINDOW_TYPE_MENU) {
            DBG( "NET_WM_WINDOW_TYPE_MENU ");
            nwwt->menu = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_UTILITY) {
            DBG( "NET_WM_WINDOW_TYPE_UTILITY ");
            nwwt->utility = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_SPLASH) {
            DBG( "NET_WM_WINDOW_TYPE_SPLASH ");
            nwwt->splash = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_DIALOG) {
            DBG( "NET_WM_WINDOW_TYPE_DIALOG ");
            nwwt->dialog = 1;
    } else if (state[num3] == a_NET_WM_WINDOW_TYPE_NORMAL) {
            DBG( "NET_WM_WINDOW_TYPE_NORMAL ");
            nwwt->normal = 1;
    } else {
        DBG( "... ");
    }
    }
    XFree(state);
    DBG( "}\n");
    RET();
}





int
get_wm_state (Window win)
{
    unsigned long *data;
    int ret = 0;

    ENTER;
    data = get_xaproperty (win, a_WM_STATE, a_WM_STATE, 0);
    if (data) {
        ret = data[0];
        XFree (data);
    }
    RET(ret);
}

static void
calculate_width(int scrw, int wtype, int allign, int margin,
      int *panw, int *x)
{
    ENTER;
    DBG("scrw=%d\n", scrw);
    DBG("IN panw=%d\n", *panw);
    //scrw -= 2;
    if (wtype == WIDTH_PERCENT) {
        /* sanity check */
        if (*panw > 100)
            *panw = 100;
        else if (*panw < 0)
            *panw = 1;
        *panw = ((gfloat) scrw * (gfloat) *panw) / 100.0;
    }
    if (allign != ALLIGN_CENTER) {
        if (margin > scrw) {
            ERR( "margin is bigger then edge size %d > %d. Ignoring margin\n",
                  margin, scrw);
            margin = 0;
        }
        if (wtype == WIDTH_PERCENT)
            //*panw = MAX(scrw - margin, *panw);
            ;
        else
            *panw = MIN(scrw - margin, *panw);
    }
    DBG("OUT panw=%d\n", *panw);
    if (allign == ALLIGN_LEFT)
        *x += margin;
    else if (allign == ALLIGN_RIGHT) {
        *x += scrw - *panw - margin;
        if (*x < 0)
            *x = 0;
    } else if (allign == ALLIGN_CENTER)
        *x += (scrw - *panw) / 2;
    RET();
}


void
calculate_position(Panel *np)
{
    int sswidth, ssheight, minx, miny;

    ENTER;
    if (0)  {
        //if (np->curdesk < np->wa_len/4) {
        minx = np->workarea[np->curdesk*4 + 0];
        miny = np->workarea[np->curdesk*4 + 1];
        sswidth  = np->workarea[np->curdesk*4 + 2];
        ssheight = np->workarea[np->curdesk*4 + 3];
    } else {
        minx = miny = 0;
        sswidth  = gdk_screen_width();
        ssheight = gdk_screen_height();

    }

    if (np->edge == EDGE_TOP || np->edge == EDGE_BOTTOM) {
        np->aw = np->width;
        np->ax = minx;
        calculate_width(sswidth, np->widthtype, np->allign, np->margin,
              &np->aw, &np->ax);
        np->ah = np->height;
        np->ah = MIN(PANEL_HEIGHT_MAX, np->ah);
        np->ah = MAX(PANEL_HEIGHT_MIN, np->ah);
        np->ay = miny + ((np->edge == EDGE_TOP) ? 0 : (ssheight - np->ah));

    } else {
        np->ah = np->width;
        np->ay = miny;
        calculate_width(ssheight, np->widthtype, np->allign, np->margin,
              &np->ah, &np->ay);
        np->aw = np->height;
        np->aw = MIN(PANEL_HEIGHT_MAX, np->aw);
        np->aw = MAX(PANEL_HEIGHT_MIN, np->aw);
        np->ax = minx + ((np->edge == EDGE_LEFT) ? 0 : (sswidth - np->aw));
    }
    //g_debug("%s - x=%d y=%d w=%d h=%d\n", __FUNCTION__, np->ax, np->ay, np->aw, np->ah);
    RET();
}



gchar *
expand_tilda(gchar *file)
{
    ENTER;
    RET((file[0] == '~') ?
        g_strdup_printf("%s%s", getenv("HOME"), file+1)
        : g_strdup(file));

}




#if 0
Window
Select_Window(Display *dpy)
{
    int status;
    Cursor cursor;
    XEvent event;
    Window target_win = None, root = RootWindow(dpy,DefaultScreen(dpy));
    int buttons = 0;

    ENTER;
    /* Make the target cursor */
    cursor = XCreateFontCursor(dpy, XC_crosshair);

    /* Grab the pointer using target cursor, letting it room all over */
    status = XGrabPointer(dpy, root, False,
          ButtonPressMask|ButtonReleaseMask, GrabModeSync,
          GrabModeAsync, root, cursor, CurrentTime);
    if (status != GrabSuccess) {
        ERR("Can't grab the mouse.");
        RET(None);
    }
    /* Let the user select a window... */
    while ((target_win == None) || (buttons != 0)) {
        /* allow one more event */
        XAllowEvents(dpy, SyncPointer, CurrentTime);
        XWindowEvent(dpy, root, ButtonPressMask|ButtonReleaseMask, &event);
        switch (event.type) {
        case ButtonPress:
            if (target_win == None) {
                target_win = event.xbutton.subwindow; /* window selected */
                DBG("target win = 0x%x\n", target_win);
                if (target_win == None) target_win = root;
            }
            buttons++;
            break;
        case ButtonRelease:
            if (buttons > 0) /* there may have been some down before we started */
                buttons--;
            break;
        }
    }

    XUngrabPointer(dpy, CurrentTime);      /* Done with pointer */
    RET(target_win);
}
#endif

/*
 * SuxPanel version 0.1
 * Copyright (c) 2003 Leandro Pereira <leandro@linuxmag.com.br>
 *
 * This program may be distributed under the terms of GNU General
 * Public License version 2. You should have received a copy of the
 * license with this program; if not, please consult http://www.fsf.org/.
 *
 * This program comes with no warranty. Use at your own risk.
 *
 */

GdkPixbuf *
gdk_pixbuf_scale_ratio(GdkPixbuf *p, int width, int height, GdkInterpType itype, gboolean keep_ratio)
{
    gfloat w, h, rw, rh;

    if (keep_ratio) {
        w = gdk_pixbuf_get_width(p);
        h = gdk_pixbuf_get_height(p);
        rw = w / width;
        rh = h / height;
        if (rw > rh)
            height = h / rw;
        else
            width =  w / rh;
    }
    return  gdk_pixbuf_scale_simple(p, width, height, itype);

}


GtkWidget *
gtk_image_new_from_file_scaled(const gchar *file, gint width,
      gint height, gboolean keep_ratio)
{
    GtkWidget *img;
    GdkPixbuf /*- *pb, -*/ *pb_scaled;
    // gfloat w, h, rw, rh;
    GtkIconInfo *inf = NULL;

    ENTER;

    if( G_UNLIKELY( ! file ) )
        goto err;

    if ( ! g_file_test(file, G_FILE_TEST_EXISTS))
    {
        /* FIXME: should reload icon when theme gets changed */
        inf = gtk_icon_theme_lookup_icon(gtk_icon_theme_get_default(),
                                         file, MAX(width, height), 0);
        if( ! inf )
            goto err;
        file = gtk_icon_info_get_filename(inf);
    }

#if GTK_CHECK_VERSION( 2, 6, 0 )
    pb_scaled = gdk_pixbuf_new_from_file_at_scale( file, width, height,
                                                   keep_ratio, NULL );
    if( !pb_scaled )
        goto err;
#else
    if (!(pb = gdk_pixbuf_new_from_file(file, NULL)))
        goto err;

    if (keep_ratio) {
        w = gdk_pixbuf_get_width(pb);
        h = gdk_pixbuf_get_height(pb);
        rw = w / width;
        rh = h / height;
        if (rw > rh)
            height = h / rw;
        else
            width =  w / rh;
    }
    pb_scaled = gdk_pixbuf_scale_simple(pb, width, height,
                                        GDK_INTERP_BILINEAR);
    g_object_unref(pb);
#endif
    img = gtk_image_new_from_pixbuf(pb_scaled);
    g_object_unref(pb_scaled);

    if( inf )
        gtk_icon_info_free ( inf );

    RET(img);

 err:
    img = gtk_image_new_from_stock(GTK_STOCK_MISSING_IMAGE,
                                   GTK_ICON_SIZE_BUTTON);
    RET(img);
}


void
get_button_spacing(GtkRequisition *req, GtkContainer *parent, gchar *name)
{
    GtkWidget *b;
    //gint focus_width;
    //gint focus_pad;

    ENTER;
    b = gtk_button_new();
    gtk_widget_set_name(GTK_WIDGET(b), name);
    GTK_WIDGET_UNSET_FLAGS (b, GTK_CAN_FOCUS);
    GTK_WIDGET_UNSET_FLAGS (b, GTK_CAN_DEFAULT);
    gtk_container_set_border_width (GTK_CONTAINER (b), 0);

    if (parent)
        gtk_container_add(parent, b);

    gtk_widget_show(b);
    gtk_widget_size_request(b, req);

    gtk_widget_destroy(b);
    RET();
}


guint32 gcolor2rgb24(GdkColor *color)
{
    guint32 i;
    guint16 r, g, b;

    ENTER;

    r = color->red * 0xFF / 0xFFFF;
    g = color->green * 0xFF / 0xFFFF;
    b = color->blue * 0xFF / 0xFFFF;
    DBG("%x %x %x ==> %x %x %x\n", color->red, color->green, color->blue, r, g, b);

    i = (color->red * 0xFF / 0xFFFF) & 0xFF;
    i <<= 8;
    i |= (color->green * 0xFF / 0xFFFF) & 0xFF;
    i <<= 8;
    i |= (color->blue * 0xFF / 0xFFFF) & 0xFF;
    DBG("i=%x\n", i);
    RET(i);
}


static gboolean
fb_button_enter (GtkImage *widget, GdkEventCrossing *event)
{
    GdkPixbuf *dark, *light;
    int i;
    gulong hicolor;
    guchar *src, *up, extra[3];

    ENTER;
    if (gtk_image_get_storage_type(widget) != GTK_IMAGE_PIXBUF)
        RET(TRUE);
    light = g_object_get_data(G_OBJECT(widget), "light");
    dark = gtk_image_get_pixbuf(widget);
    if (!light) {
        hicolor = (gulong) g_object_get_data(G_OBJECT(widget), "hicolor");
        light = gdk_pixbuf_add_alpha(dark, FALSE, 0, 0, 0);
        if (!light)
            RET(TRUE);
        src = gdk_pixbuf_get_pixels (light);
        for (i = 2; i >= 0; i--, hicolor >>= 8)
            extra[i] = hicolor & 0xFF;
        for (up = src + gdk_pixbuf_get_height(light) * gdk_pixbuf_get_rowstride (light);
             src < up; src+=4) {
            if (src[3] == 0)
                continue;
            for (i = 0; i < 3; i++) {
                if (src[i] + extra[i] >= 255)
                    src[i] = 255;
                else
                    src[i] += extra[i];
            }
        }
        g_object_set_data_full (G_OBJECT(widget), "light", light, g_object_unref);
    }
    g_object_ref(dark);
    g_object_set_data_full (G_OBJECT(widget), "dark", dark, g_object_unref);
    gtk_image_set_from_pixbuf(widget, light);
    RET(TRUE);

}

static gboolean
fb_button_leave (GtkImage *widget, GdkEventCrossing *event, gpointer user_data)
{
    GdkPixbuf *dark;

    ENTER;
    if (gtk_image_get_storage_type(widget) != GTK_IMAGE_PIXBUF)
        RET(TRUE);
    dark = g_object_get_data(G_OBJECT(widget), "dark");
    if (dark)
        gtk_image_set_from_pixbuf(widget, dark);
    RET(TRUE);
}


GtkWidget *
fb_button_new_from_file(gchar *fname, int width, int height, gulong hicolor, gboolean keep_ratio)
{
    GtkWidget *b, *image;
    ENTER;
//    b = gtk_vbox_new(FALSE, 0); //gtk_bgbox_new();
    b = gtk_event_box_new();
    gtk_container_set_border_width(GTK_CONTAINER(b), 0);
    GTK_WIDGET_UNSET_FLAGS (b, GTK_CAN_FOCUS);

    image = gtk_image_new_from_file_scaled(fname, width, height, keep_ratio);
    gtk_misc_set_alignment(GTK_MISC(image), 0, 0);
    g_object_set_data(G_OBJECT(image), "hicolor", (gpointer)hicolor);
    gtk_misc_set_padding (GTK_MISC(image), 0, 0);
    if (hicolor > 0) {
        gtk_widget_add_events(b, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect_swapped (G_OBJECT (b), "enter-notify-event",
              G_CALLBACK (fb_button_enter), image);
        g_signal_connect_swapped (G_OBJECT (b), "leave-notify-event",
              G_CALLBACK (fb_button_leave), image);
    }
    gtk_container_add(GTK_CONTAINER(b), image);
    gtk_widget_show(image);
    gtk_widget_show(b);
    RET(b);
}

GtkWidget *
fb_button_new_from_file_with_label(gchar *fname, int width, int height,
      gulong hicolor, gboolean keep_ratio, gchar *name)
{
    GtkWidget *b, *image, *box, *label;

    ENTER;
    b = gtk_event_box_new();
    gtk_container_set_border_width(GTK_CONTAINER(b), 0);
    GTK_WIDGET_UNSET_FLAGS (b, GTK_CAN_FOCUS);

    box = gtk_hbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(box), 0);
    GTK_WIDGET_UNSET_FLAGS (box, GTK_CAN_FOCUS);
    gtk_container_add(GTK_CONTAINER(b), box);

    image = gtk_image_new_from_file_scaled(fname, width, height, keep_ratio);
    g_object_set_data(G_OBJECT(image), "hicolor", (gpointer)hicolor);
    gtk_misc_set_padding (GTK_MISC(image), 0, 0);
    if (hicolor > 0) {
        gtk_widget_add_events(b, GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect_swapped (G_OBJECT (b), "enter-notify-event",
              G_CALLBACK (fb_button_enter), image);
        g_signal_connect_swapped (G_OBJECT (b), "leave-notify-event",
              G_CALLBACK (fb_button_leave), image);
    }
    gtk_box_pack_start(GTK_BOX(box), image, FALSE, FALSE, 0);
    if (name) {
        label =  gtk_label_new(name);
        gtk_misc_set_padding(GTK_MISC(label), 2, 0);
        gtk_box_pack_end(GTK_BOX(box), label, FALSE, FALSE, 0);
    }
    gtk_widget_show_all(b);
    RET(b);
}

char* translate_exec_to_cmd( const char* exec, const char* icon,
                             const char* title, const char* fpath )
{
    GString* cmd = g_string_sized_new( 256 );
    for( ; *exec; ++exec )
    {
        if( G_UNLIKELY(*exec == '%') )
        {
            ++exec;
            if( !*exec )
                break;
            switch( *exec )
            {
                case 'c':
                    g_string_append( cmd, title );
                    break;
                case 'i':
                    if( icon )
                    {
                        g_string_append( cmd, "--icon " );
                        g_string_append( cmd, icon );
                    }
                    break;
                case 'k':
                {
                    char* uri = g_filename_to_uri( fpath, NULL, NULL );
                    g_string_append( cmd, uri );
                    g_free( uri );
                    break;
                }
                case '%':
                    g_string_append_c( cmd, '%' );
                    break;
            }
        }
        else
            g_string_append_c( cmd, *exec );
    }
    return g_string_free( cmd, FALSE );
}

/*
 This function is used to re-create a new box with different
 orientation from the old one, add all children of the old one to
 the new one, and then destroy the old box.
 It's mainly used when we need to change the orientation of the panel or
 any plugin with a layout box. Since GtkHBox cannot be changed to GtkVBox,
 recreating a new box to replace the old one is required.
*/
GtkWidget* recreate_box( GtkBox* oldbox, GtkOrientation orientation )
{
    GtkBox* newbox;
    GList *child, *children;
    GtkWidget* (*my_box_new) (gboolean homogeneous, gint spacing);

    if( GTK_IS_HBOX(oldbox) ) {
        if( orientation == GTK_ORIENTATION_HORIZONTAL )
            return GTK_WIDGET(oldbox);
    }
    else {
        if( orientation == GTK_ORIENTATION_VERTICAL )
            return GTK_WIDGET(oldbox);
    }
    my_box_new = (orientation == GTK_ORIENTATION_HORIZONTAL ? gtk_hbox_new : gtk_vbox_new);

    newbox = GTK_BOX(my_box_new( gtk_box_get_homogeneous(oldbox),
                                 gtk_box_get_spacing(oldbox) ));
    gtk_container_set_border_width (GTK_CONTAINER (newbox),
                                    gtk_container_get_border_width(GTK_CONTAINER(oldbox)) );
    children = gtk_container_get_children( GTK_CONTAINER (oldbox) );
    for( child = children; child; child = child->next ) {
        gboolean expand, fill;
        guint padding;
        GtkWidget* w = GTK_WIDGET(child->data);
        gtk_box_query_child_packing( oldbox, w,
                                     &expand, &fill, &padding, NULL );
        /* g_debug( "repack %s, expand=%d, fill=%d", gtk_widget_get_name(w), expand, fill ); */
        g_object_ref( w );
        gtk_container_remove( GTK_CONTAINER (oldbox), w );
        gtk_box_pack_start( newbox, w, expand, fill, padding );
        g_object_unref( w );
    }
    g_list_free( children );
    gtk_widget_show_all( GTK_WIDGET(newbox) );
    gtk_widget_destroy( GTK_WIDGET(oldbox) );
    return GTK_WIDGET(newbox);
}

void show_error( GtkWindow* parent_win, const char* msg )
{
    GtkWidget* dlg = gtk_message_dialog_new( parent_win,
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_OK, msg );
    gtk_dialog_run( (GtkDialog*)dlg );
    gtk_widget_destroy( dlg );
}
