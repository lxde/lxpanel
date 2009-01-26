/*
 * fb-background-monitor.c:
 *
 * Copyright (C) 2001, 2002 Ian McKellar <yakk@yakk.net>
 *                     2002 Sun Microsystems, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 * Authors:
 *      Ian McKellar <yakk@yakk.net>
 *      Mark McLoughlin <mark@skynet.ie>
 */

#include <glib.h>
#include <glib-object.h>
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include "bg.h"

//#define DEBUG
#include "dbg.h"


enum {
    CHANGED,
    LAST_SIGNAL
};


struct _FbBgClass {
    GObjectClass   parent_class;
    void         (*changed) (FbBg *monitor);
};

struct _FbBg {
    GObject    parent_instance;

    Window   xroot;
    Atom     id;
    GC       gc;
    Display *dpy;
    Pixmap   pixmap;
};

static void fb_bg_class_init (FbBgClass *klass);
static void fb_bg_init (FbBg *monitor);
static void fb_bg_finalize (GObject *object);
static Pixmap fb_bg_get_xrootpmap(FbBg *monitor);
static void fb_bg_changed(FbBg *monitor);


static guint signals [LAST_SIGNAL] = { 0 };

static FbBg *default_bg = NULL;

GType
fb_bg_get_type (void)
{
    static GType object_type = 0;

    if (!object_type) {
        static const GTypeInfo object_info = {
            sizeof (FbBgClass),
            (GBaseInitFunc) NULL,
            (GBaseFinalizeFunc) NULL,
            (GClassInitFunc) fb_bg_class_init,
            NULL,           /* class_finalize */
            NULL,           /* class_data */
            sizeof (FbBg),
            0,              /* n_preallocs */
            (GInstanceInitFunc) fb_bg_init,
        };

        object_type = g_type_register_static (
            G_TYPE_OBJECT, "FbBg", &object_info, 0);
    }

    return object_type;
}



static void
fb_bg_class_init (FbBgClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);
    ENTER;
    signals [CHANGED] =
        g_signal_new ("changed",
              G_OBJECT_CLASS_TYPE (object_class),
              G_SIGNAL_RUN_FIRST,
              G_STRUCT_OFFSET (FbBgClass, changed),
              NULL, NULL,
              g_cclosure_marshal_VOID__VOID,
              G_TYPE_NONE, 0);
    klass->changed = fb_bg_changed;
    object_class->finalize = fb_bg_finalize;
    RET();
}

static void
fb_bg_init (FbBg *bg)
{
    XGCValues  gcv;
    uint mask;

    ENTER;
    bg->dpy = GDK_DISPLAY();
    bg->xroot = DefaultRootWindow(bg->dpy);
    bg->id = gdk_x11_get_xatom_by_name("_XROOTPMAP_ID");
    bg->pixmap = fb_bg_get_xrootpmap(bg);
    gcv.ts_x_origin = 0;
    gcv.ts_y_origin = 0;
    gcv.fill_style = FillTiled;
    mask = GCTileStipXOrigin | GCTileStipYOrigin | GCFillStyle;
    if (bg->pixmap != None) {
        gcv.tile = bg->pixmap;
        mask |= GCTile ;
    }
    bg->gc = XCreateGC (bg->dpy, bg->xroot, mask, &gcv) ;
    RET();
}

static void
fb_bg_finalize (GObject *object)
{
    FbBg *bg;

    ENTER;
    bg = FB_BG (object);
    XFreeGC(bg->dpy, bg->gc);
    RET();
}


static Pixmap
fb_bg_get_xrootpmap(FbBg *bg)
{
    Pixmap ret = None;

    ENTER;
    if (bg->id) {
        int  act_format, c = 2 ;
        u_long  nitems ;
        u_long  bytes_after ;
        u_char *prop = NULL;
        Atom ret_type;

        do {
            if (XGetWindowProperty(bg->dpy, bg->xroot, bg->id, 0, 1,
                      False, XA_PIXMAP, &ret_type, &act_format,
                      &nitems, &bytes_after, &prop) == Success) {
                if (ret_type == XA_PIXMAP) {
                    ret = *((Pixmap *)prop);
                    XFree(prop);
                    break;
                }
            }
        } while (--c > 0);
    }
    RET(ret);

}


GdkPixmap *
fb_bg_get_xroot_pix_for_win(FbBg *bg, GtkWidget *widget)
{
    Window win;
    Window dummy;
    Pixmap bgpix;
    GdkPixmap *gbgpix;
    guint  width, height, border, depth;
    int  x, y;

    ENTER;
    win =  GDK_WINDOW_XWINDOW(widget->window);
    if (!XGetGeometry(bg->dpy, win, &dummy, &x, &y, &width, &height, &border,
              &depth)) {
        DBG2("XGetGeometry failed\n");
        RET(NULL);
    }
    XTranslateCoordinates(bg->dpy, win, bg->xroot, 0, 0, &x, &y, &dummy);
    DBG("win=%x %dx%d%+d%+d\n", win, width, height, x, y);
    gbgpix = gdk_pixmap_new(NULL, width, height, depth);
    if (!gbgpix) {
        ERR("gdk_pixmap_new failed\n");
        RET(NULL);
    }
    bgpix =  gdk_x11_drawable_get_xid(gbgpix);
    XSetTSOrigin(bg->dpy, bg->gc, -x, -y) ;
    XFillRectangle(bg->dpy, bgpix, bg->gc, 0, 0, width, height);
    RET(gbgpix);
}

void
fb_bg_composite(GdkDrawable *base, GdkGC *gc, guint32 tintcolor, gint alpha)
{
    GdkPixbuf *ret, *ret2;
    int w, h;
    static GdkColormap *cmap = NULL;

    ENTER;
    gdk_drawable_get_size (base, &w, &h);
    if (!cmap) {
        cmap = gdk_colormap_get_system ();
    }
    DBG("here\n");
    ret = gdk_pixbuf_get_from_drawable (NULL, base, cmap, 0, 0, 0, 0, w, h);
    if (!ret)
        RET();
    DBG("here w=%d h=%d\n", w, h);
    ret2 = gdk_pixbuf_composite_color_simple(ret, w, h,
          GDK_INTERP_HYPER, 255-alpha, MIN(w, h), tintcolor, tintcolor);
    DBG("here\n");
    if (!ret2) {
        g_object_unref(ret);
        RET();
    }
    //gdk_pixbuf_render_to_drawable (ret2, base, gc, 0, 0, 0, 0, w, h, GDK_RGB_DITHER_NONE, 0, 0);
    gdk_draw_pixbuf (base, gc, ret2, 0, 0, 0, 0, w, h, GDK_RGB_DITHER_NONE, 0, 0);
    g_object_unref(ret);
    g_object_unref(ret2);
    RET();
}


static void
fb_bg_changed(FbBg *bg)
{
    ENTER;
    bg->pixmap = fb_bg_get_xrootpmap(bg);
    if (bg->pixmap != None) {
        XGCValues  gcv;

        gcv.tile = bg->pixmap;
        XChangeGC(bg->dpy, bg->gc, GCTile, &gcv);
        DBG("changed\n");
    }
    RET();
}

inline void fb_bg_notify_changed_bg(FbBg *bg)
{
    ENTER;
    g_signal_emit (bg, signals [CHANGED], 0);
    RET();
}

FbBg *fb_bg_get_for_display(void)
{
    ENTER;
    if (!default_bg)
    {
        default_bg = fb_bg_new();
        g_object_add_weak_pointer( G_OBJECT(default_bg), 
                (gpointer)&default_bg );
    }
    else
        g_object_ref(default_bg);
    RET(default_bg);
}

GdkPixmap *
fb_bg_get_pix_from_file(GtkWidget *widget, const char *filename)
{
    ENTER;
    GdkPixbuf *pixbuf;
    GdkPixmap *pixmap;

    pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
    if (!pixbuf) {
        RET(widget->style->bg_pixmap[0]);
    }
    pixmap = gdk_pixmap_new(widget->window, gdk_pixbuf_get_width(pixbuf), gdk_pixbuf_get_height(pixbuf), -1);
    gdk_pixbuf_render_to_drawable(pixbuf,pixmap,
            widget->style->fg_gc[GTK_STATE_NORMAL],
            0,0,0,0,
            gdk_pixbuf_get_width(pixbuf),
            gdk_pixbuf_get_height(pixbuf),
            GDK_RGB_DITHER_NORMAL,0,0);

    g_object_unref( pixbuf );
    RET(pixmap);
}
