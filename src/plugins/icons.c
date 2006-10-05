#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
//#include <X11/xpm.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "gtkbar.h"


//#define DEBUG
#include "dbg.h"
/*
 * TODO : icon_copied
 * 21/03/04 aanatoly
 * v   implement wm icon scaling ??
 *     implement net_wm_icon 
 */

typedef struct wmpix_t {
    struct wmpix_t *next;
    gulong *data;
    int size;
    XClassHint ch;
} wmpix_t;

struct _icons;
typedef struct _task{
    struct _icons *ics;
    struct task *next;
    Window win;
    int refcount;
    XClassHint ch;    
} task;



typedef struct _icons{
    plugin *plug;
    Window *wins;
    int win_num;
    GHashTable  *task_list;
    int num_tasks;
    wmpix_t *wmpix; 
    int wmpixno;
    wmpix_t *dicon;
} icons;



static void ics_propertynotify(icons *ics, XEvent *ev);
static GdkFilterReturn ics_event_filter( XEvent *, GdkEvent *, icons *);
static void icons_destructor(plugin *p);


static void
get_wmclass(task *tk)
{
    ENTER;
    if (tk->ch.res_name)
        XFree(tk->ch.res_name);
    if (tk->ch.res_class)
        XFree(tk->ch.res_class);
    if (!XGetClassHint (gdk_display, tk->win, &tk->ch)) 
        tk->ch.res_class = tk->ch.res_name = NULL;
    RET();
}




static inline task *
find_task (icons * ics, Window win)
{
    ENTER;
    RET(g_hash_table_lookup(ics->task_list, &win));
}


static void
del_task (icons * ics, task *tk, int hdel)
{
    ENTER;
    ics->num_tasks--; 
    if (hdel)
        g_hash_table_remove(ics->task_list, &tk->win);
    g_free(tk);
    RET();
}

static wmpix_t *
get_dicon_maybe(icons *ics, task *tk)
{
    XWMHints *hints;
    gulong *data;
    int n;

    ENTER;
    data = get_xaproperty(tk->win, a_NET_WM_ICON, XA_CARDINAL, &n);
    if (data) {
        XFree(data);
        RET(NULL);
    }
    
    hints = (XWMHints *) get_xaproperty (tk->win, XA_WM_HINTS, XA_WM_HINTS, 0);
    if (hints) {
        if ((hints->flags & IconPixmapHint) || (hints->flags & IconMaskHint)) {
            XFree (hints);
            RET(NULL);
        }
        XFree (hints);
    }
    RET(ics->dicon);
}


static wmpix_t *
get_user_icon(icons *ics, task *tk)
{
    wmpix_t *tmp;

    ENTER;
    if (tk->ch.res_class) {
        for (tmp = ics->wmpix; tmp; tmp = tmp->next) {
            if ((!tmp->ch.res_name || !strcmp(tmp->ch.res_name, tk->ch.res_name))
                  && (!tmp->ch.res_class || !strcmp(tmp->ch.res_class, tk->ch.res_class))) {
                
                RET(tmp);
	    }
	}
    }
    RET(NULL);
    //RET (ics->dicon);
}



gulong *
pixbuf2argb (GdkPixbuf *pixbuf, int *size)
{
    gulong *data;
    guchar *pixels;
    gulong *p;
    gint width, height, stride;
    gint x, y;
    gint n_channels;

    ENTER;
    *size = 0;
    width = gdk_pixbuf_get_width (pixbuf);
    height = gdk_pixbuf_get_height (pixbuf);
    stride = gdk_pixbuf_get_rowstride (pixbuf);
    n_channels = gdk_pixbuf_get_n_channels (pixbuf);
      
    *size += 2 + width * height;
    p = data = g_malloc (*size * sizeof (gulong));
    *p++ = width;
    *p++ = height;
    
    pixels = gdk_pixbuf_get_pixels (pixbuf);
    
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            guchar r, g, b, a;
            
            r = pixels[y*stride + x*n_channels + 0];
            g = pixels[y*stride + x*n_channels + 1];
            b = pixels[y*stride + x*n_channels + 2];
            if (n_channels >= 4)
                a = pixels[y*stride + x*n_channels + 3];
            else
                a = 255;
            
            *p++ = a << 24 | r << 16 | g << 8 | b ;
        }
    }
    RET(data);
}



static void
set_icon_maybe (icons *ics, task *tk)
{
    wmpix_t *pix;

    ENTER;
    g_assert ((ics != NULL) && (tk != NULL));
    g_return_if_fail(tk != NULL);


    pix = get_user_icon(ics, tk);
    if (!pix) 
        pix = get_dicon_maybe(ics, tk);
    
    if (!pix)
        RET();

    DBG("%s size=%d\n", pix->ch.res_name, pix->size);
    XChangeProperty (GDK_DISPLAY(), tk->win,
          a_NET_WM_ICON, XA_CARDINAL, 32, PropModeReplace, (guchar*) pix->data, pix->size);

    RET();
}




/* tell to remove element with zero refcount */
static gboolean
remove_stale_tasks(Window *win, task *tk, gpointer data)
{
    ENTER;
    if (tk->refcount-- == 0) {
        del_task(tk->ics, tk, 0);
        RET(TRUE);
    }
    RET(FALSE);
}

/*****************************************************
 * handlers for NET actions                          *
 *****************************************************/

static GdkFilterReturn
ics_event_filter( XEvent *xev, GdkEvent *event, icons *ics)
{
    
    ENTER;
    g_assert(ics != NULL);
    if (xev->type == PropertyNotify )
	ics_propertynotify(ics, xev);
    RET(GDK_FILTER_CONTINUE);
}


static void
do_net_client_list(GtkWidget *widget, icons *ics)
{
    int i;
    task *tk;
    
    ENTER;
    if (ics->wins)
        XFree(ics->wins);
    ics->wins = get_xaproperty (GDK_ROOT_WINDOW(), a_NET_CLIENT_LIST, XA_WINDOW, &ics->win_num);
    if (!ics->wins) 
	RET();

    for (i = 0; i < ics->win_num; i++) {
        if ((tk = g_hash_table_lookup(ics->task_list, &ics->wins[i]))) {
            tk->refcount++;
        } else {
            tk = g_new0(task, 1);
            tk->refcount++;
            ics->num_tasks++;
            tk->win = ics->wins[i];
            tk->ics = ics;
            
            if (!FBPANEL_WIN(tk->win))
                XSelectInput (GDK_DISPLAY(), tk->win, PropertyChangeMask | StructureNotifyMask); 
            get_wmclass(tk);
            set_icon_maybe(ics, tk);
            g_hash_table_insert(ics->task_list, &tk->win, tk);
        }
    }
    
    /* remove windows that arn't in the NET_CLIENT_LIST anymore */
    g_hash_table_foreach_remove(ics->task_list, (GHRFunc) remove_stale_tasks, NULL);
    RET();
}

static void
ics_propertynotify(icons *ics, XEvent *ev)
{
    Atom at;
    Window win;

    
    ENTER;
    win = ev->xproperty.window;
    at = ev->xproperty.atom;
    DBG("win=%x at=%d\n", win, at);
    if (win != GDK_ROOT_WINDOW()) {
	task *tk = find_task(ics, win);
        
	if (!tk) RET();
        if (at == XA_WM_CLASS) {
	    get_wmclass(tk);
            set_icon_maybe(ics, tk);
	} else if (at == XA_WM_HINTS) {
            set_icon_maybe(ics, tk);
	}  
    }
    RET();
}

static void
icons_build_gui(plugin *p)
{
    icons *ics = (icons *)p->priv;
    
    ENTER;
    g_signal_connect (G_OBJECT (fbev), "client_list",
          G_CALLBACK (do_net_client_list), (gpointer) ics);
    gdk_window_add_filter(NULL, (GdkFilterFunc)ics_event_filter, ics );
    RET();
}

static int
read_application(plugin *p)
{
    icons *ics = (icons *)p->priv;
    GdkPixbuf *gp = NULL;
    line s;
    gchar *fname, *appname, *classname;
    wmpix_t *wp = NULL;
    gulong *data;
    int size;
    
    ENTER;
    s.len = 256;
    fname = appname = classname = NULL;
    while (get_line(p->fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_NONE) {
            ERR( "icons: illegal token %s\n", s.str);
            goto error;
        }
        if (s.type == LINE_VAR) {
            if (!g_ascii_strcasecmp(s.t[0], "image")) 
                fname = expand_tilda(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "appname"))
                appname = g_strdup(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "classname"))
                classname = g_strdup(s.t[1]);
            else {
                ERR( "icons: unknown var %s\n", s.t[0]);
                goto error;
            }
        } else {
            ERR( "icons: illegal in this context %s\n", s.str);
            goto error;
        }
    }
    if (!fname)
        RET(0);
    gp = gdk_pixbuf_new_from_file(fname, NULL);  
    if (gp) {
        if ((data = pixbuf2argb(gp, &size))) {
            wp = g_new0 (wmpix_t, 1);
            wp->next = ics->wmpix;
            wp->data = data;
            wp->size = size;
            wp->ch.res_name = appname;
            wp->ch.res_class = classname;
            ics->wmpix = wp;
            ics->wmpixno++;
        }
        g_object_unref(gp);
    }
    g_free(fname);    
    RET(1);
  
 error:
    g_free(fname);
    g_free(appname);
    g_free(classname);
    RET(0);
}

static int
read_dicon(icons *ics, gchar *name)
{
    gchar *fname;
    GdkPixbuf *gp;
    int size;
    gulong *data;
    
    ENTER;
    fname = expand_tilda(name);
    if (!fname)
        RET(0);
    gp = gdk_pixbuf_new_from_file(fname, NULL);  
    if (gp) {
        if ((data = pixbuf2argb(gp, &size))) {
            ics->dicon = g_new0 (wmpix_t, 1);
            ics->dicon->data = data;
            ics->dicon->size = size;
        }
        g_object_unref(gp);
    }
    g_free(fname);    
    RET(1);
}


static int
icons_constructor(plugin *p)
{
    icons *ics;
    line s;
    
    ENTER;
    ics = g_new0(icons, 1);
    ics->plug = p;
    p->priv = ics;
    
    ics->wmpixno           = 0;
    ics->task_list         = g_hash_table_new(g_int_hash, g_int_equal);
    s.len = 256;
    while (get_line(p->fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_NONE) {
            ERR( "icons: illegal token %s\n", s.str);
            goto error;
        }
        if (s.type == LINE_VAR) {
            if (!g_ascii_strcasecmp(s.t[0], "DefaultIcon")) {
                if (!read_dicon(ics, s.t[1])) {
                    goto error;
                }
            } else {
                ERR( "icons: unknown var %s\n", s.t[0]);
                goto error;
            }
        } else if (s.type == LINE_BLOCK_START) {
            if (!g_ascii_strcasecmp(s.t[0], "application")) {
                if (!read_application(p)) {
                    goto error;
                }
            } else {
                ERR( "icons: unknown var %s\n", s.t[0]);
                goto error;
            }
        } else {
            ERR( "icons: illegal in this context %s\n", s.str);
            goto error;
        }
    }
  
    icons_build_gui(p);
    do_net_client_list(NULL, ics);
    RET(1);
    
 error:
    icons_destructor(p);
    RET(0);
}


static void
icons_destructor(plugin *p)
{
    icons *ics = (icons *)p->priv;
    wmpix_t *wp;
    
    ENTER;
    g_signal_handlers_disconnect_by_func(G_OBJECT (fbev), do_net_client_list, ics); 
    gdk_window_remove_filter(NULL, (GdkFilterFunc)ics_event_filter, ics );
    while (ics->wmpix) {
        wp = ics->wmpix;
        ics->wmpix = ics->wmpix->next;
        g_free(wp->ch.res_name);
        g_free(wp->ch.res_class);
        g_free(wp->data);
        g_free(wp);
    }
    RET();
}

plugin_class icons_plugin_class = {
    fname: NULL,
    count: 0,

    type : "icons",
    name : "icons",
    version: "1.0",
    description : N_("Change window icons"),
    invisible : 1,
    
    constructor : icons_constructor,
    destructor  : icons_destructor,
};
