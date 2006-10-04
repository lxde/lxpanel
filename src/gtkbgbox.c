/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

#include <string.h>
#include "gtkbgbox.h"
#include "bg.h"
#include <gdk/gdk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkpixmap.h>
#include <gdk/gdkprivate.h>
#include <glib.h>
#include <glib-object.h>


//#define DEBUG
#include "dbg.h"

typedef struct {
    GdkPixmap *pixmap;
    guint32 tintcolor;
    gint alpha;
    int bg_type;
    FbBg *bg;
} GtkBgboxPrivate;




#define GTK_BGBOX_GET_PRIVATE(obj)  G_TYPE_INSTANCE_GET_PRIVATE((obj), GTK_TYPE_BGBOX, GtkBgboxPrivate)

static void gtk_bgbox_class_init    (GtkBgboxClass *klass);
static void gtk_bgbox_init          (GtkBgbox *bgbox);
static void gtk_bgbox_realize       (GtkWidget *widget);
static void gtk_bgbox_size_request  (GtkWidget *widget, GtkRequisition   *requisition);
static void gtk_bgbox_size_allocate (GtkWidget *widget, GtkAllocation    *allocation);
static void gtk_bgbox_style_set (GtkWidget *widget, GtkStyle  *previous_style);
static gboolean gtk_bgbox_configure_event(GtkWidget *widget, GdkEventConfigure *e);
static gboolean gtk_bgbox_destroy_event (GtkWidget *widget, GdkEventAny *event);
static gboolean gtk_bgbox_delete_event (GtkWidget *widget, GdkEventAny *event);

static void gtk_bgbox_finalize (GObject *object);

static void gtk_bgbox_free_bg(GtkWidget *bgbox);
static void gtk_bgbox_set_bg_root(GtkWidget *widget, GtkBgboxPrivate *priv);
static void gtk_bgbox_bg_changed(FbBg *bg, GtkWidget *widget);

static GtkBinClass *parent_class = NULL;

GType
gtk_bgbox_get_type (void)
{
    static GType bgbox_type = 0;

    if (!bgbox_type)
    {
        static const GTypeInfo bgbox_info =
            {
                sizeof (GtkBgboxClass),
                NULL,		/* base_init */
                NULL,		/* base_finalize */
                (GClassInitFunc) gtk_bgbox_class_init,
                NULL,		/* class_finalize */
                NULL,		/* class_data */
                sizeof (GtkBgbox),
                0,		/* n_preallocs */
                (GInstanceInitFunc) gtk_bgbox_init,
            };

        bgbox_type = g_type_register_static (GTK_TYPE_BIN, "GtkBgbox",
              &bgbox_info, 0);
    }

    return bgbox_type;
}

static void
gtk_bgbox_class_init (GtkBgboxClass *class)
{
    GObjectClass *object_class = G_OBJECT_CLASS (class);
    GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (class);

    parent_class = g_type_class_peek_parent (class);

    widget_class->realize         = gtk_bgbox_realize;
    widget_class->size_request    = gtk_bgbox_size_request;
    widget_class->size_allocate   = gtk_bgbox_size_allocate;
    widget_class->style_set       = gtk_bgbox_style_set;
    widget_class->configure_event = gtk_bgbox_configure_event;
    widget_class->destroy_event   = gtk_bgbox_destroy_event;
    widget_class->delete_event    = gtk_bgbox_delete_event;

    object_class->finalize = gtk_bgbox_finalize;
    g_type_class_add_private (class, sizeof (GtkBgboxPrivate));
}

static void
gtk_bgbox_init (GtkBgbox *bgbox)
{
    GtkBgboxPrivate *priv;

    GTK_WIDGET_UNSET_FLAGS (bgbox, GTK_NO_WINDOW);
 
    priv = GTK_BGBOX_GET_PRIVATE (bgbox);
    priv->bg_type = BG_NONE;
}

GtkWidget*
gtk_bgbox_new (void)
{
    ENTER;
    RET(g_object_new (GTK_TYPE_BGBOX, NULL));
}

static void
gtk_bgbox_finalize (GObject *object)
{
    ENTER;
    gtk_bgbox_free_bg(GTK_WIDGET(object));
    RET();
}

static gboolean
gtk_bgbox_destroy_event (GtkWidget *widget, GdkEventAny *event)
{
    ENTER;

    RET(FALSE);
}

static gboolean
gtk_bgbox_delete_event (GtkWidget *widget, GdkEventAny *event)

{
    ENTER;

    RET(FALSE);
}


static void
gtk_bgbox_realize (GtkWidget *widget)
{
    GdkWindowAttr attributes;
    gint attributes_mask;
    gint border_width;
    GtkBgboxPrivate *priv;

    ENTER;
    GTK_WIDGET_SET_FLAGS (widget, GTK_REALIZED);

    border_width = GTK_CONTAINER (widget)->border_width;
  
    attributes.x = widget->allocation.x + border_width;
    attributes.y = widget->allocation.y + border_width;
    attributes.width = widget->allocation.width - 2*border_width;
    attributes.height = widget->allocation.height - 2*border_width;
    attributes.window_type = GDK_WINDOW_CHILD;
    attributes.event_mask = gtk_widget_get_events (widget)
        | GDK_BUTTON_MOTION_MASK
        | GDK_BUTTON_PRESS_MASK
        | GDK_BUTTON_RELEASE_MASK
        | GDK_EXPOSURE_MASK
        | GDK_ENTER_NOTIFY_MASK
        | GDK_LEAVE_NOTIFY_MASK;

    priv = GTK_BGBOX_GET_PRIVATE (widget);
  
    attributes.visual = gtk_widget_get_visual (widget);
    attributes.colormap = gtk_widget_get_colormap (widget);
    attributes.wclass = GDK_INPUT_OUTPUT;
    
    attributes_mask = GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP;
      
    widget->window = gdk_window_new (gtk_widget_get_parent_window (widget),
          &attributes, attributes_mask);
    gdk_window_set_user_data (widget->window, widget);
    widget->style = gtk_style_attach (widget->style, widget->window);
    gtk_bgbox_set_background(widget, BG_STYLE, 0, 0);
    RET();
}


static void
gtk_bgbox_style_set (GtkWidget *widget, GtkStyle  *previous_style)
{
    GtkBgboxPrivate *priv;

    ENTER;
    priv = GTK_BGBOX_GET_PRIVATE (widget);
    if (GTK_WIDGET_REALIZED (widget) && !GTK_WIDGET_NO_WINDOW (widget)
          && priv->bg_type == BG_ROOT) {
        gtk_bgbox_free_bg(widget);
        gtk_bgbox_set_background(widget, BG_ROOT, priv->tintcolor, priv->alpha);
    }
    RET();
}

/* gtk discards configure_event for GTK_WINDOW_CHILD. too pitty */
static  gboolean
gtk_bgbox_configure_event (GtkWidget *widget, GdkEventConfigure *e)
{
    ENTER;
    DBG("geom: size (%d, %d). pos (%d, %d)\n", e->width, e->height, e->x, e->y);
    RET(FALSE);
    
}

static void
gtk_bgbox_size_request (GtkWidget *widget, GtkRequisition *requisition)
{
    GtkBin *bin = GTK_BIN (widget);
    ENTER;
    requisition->width = GTK_CONTAINER (widget)->border_width * 2;
    requisition->height = GTK_CONTAINER (widget)->border_width * 2;

    if (bin->child && GTK_WIDGET_VISIBLE (bin->child))
    {
        GtkRequisition child_requisition;
      
        gtk_widget_size_request (bin->child, &child_requisition);

        requisition->width += child_requisition.width;
        requisition->height += child_requisition.height;
    }
    RET();
}

/* calls with same allocation are usually refer to exactly same background
 * and we just skip them for optimization reason.
 * so if you see artifacts or unupdated background - reallocate bg on every call
 */
static void
gtk_bgbox_size_allocate (GtkWidget *widget, GtkAllocation *wa)
{
    GtkBin *bin;
    GtkAllocation ca;
    GtkBgboxPrivate *priv;
    int same_alloc, border;
  
    ENTER;
    same_alloc = !memcmp(&widget->allocation, wa, sizeof(*wa));
    DBG("same alloc = %d\n", same_alloc);
    DBG("x=%d y=%d w=%d h=%d\n", wa->x, wa->y, wa->width, wa->height);
    DBG("x=%d y=%d w=%d h=%d\n", widget->allocation.x, widget->allocation.y,
          widget->allocation.width, widget->allocation.height);
    widget->allocation = *wa;
    bin = GTK_BIN (widget);
    border = GTK_CONTAINER (widget)->border_width;
    ca.x = border;
    ca.y = border;
    ca.width  = MAX (wa->width  - border * 2, 0);
    ca.height = MAX (wa->height - border * 2, 0);
 
    if (GTK_WIDGET_REALIZED (widget) && !GTK_WIDGET_NO_WINDOW (widget)
          && !same_alloc) {
        priv = GTK_BGBOX_GET_PRIVATE (widget);
        gdk_window_move_resize (widget->window, wa->x, wa->y, wa->width, wa->height);
        if (priv->bg_type == BG_ROOT) {
            gtk_bgbox_free_bg(widget);
            gtk_bgbox_set_background(widget, BG_ROOT, priv->tintcolor, priv->alpha);
        }
    }
    
    if (bin->child)
        gtk_widget_size_allocate (bin->child, &ca);
    RET();
}

static void
gtk_bgbox_free_bg(GtkWidget *bgbox)
{
    GtkBgboxPrivate *priv;

    ENTER;
    priv = GTK_BGBOX_GET_PRIVATE (bgbox);
    if (priv->bg_type == BG_ROOT) {
        g_object_unref(priv->pixmap);
        priv->pixmap = NULL;
        g_signal_handlers_disconnect_by_func(priv->bg, gtk_bgbox_bg_changed, bgbox);
        g_object_unref(priv->bg);
        priv->bg = NULL;
    }
    priv->bg_type = BG_NONE;
    RET();
}

static void
gtk_bgbox_bg_changed(FbBg *bg, GtkWidget *widget)
{
    GtkBgboxPrivate *priv;

    ENTER;
    priv = GTK_BGBOX_GET_PRIVATE (widget);
    if (GTK_WIDGET_REALIZED (widget) && !GTK_WIDGET_NO_WINDOW (widget)
          && priv->bg_type == BG_ROOT) {
        gtk_bgbox_free_bg(widget);
        gtk_bgbox_set_background(widget, BG_ROOT, priv->tintcolor, priv->alpha);
    }
    RET();
}

void
gtk_bgbox_set_background(GtkWidget *widget, int bg_type, guint32 tintcolor, gint alpha)
{
    GtkBgboxPrivate *priv;

    ENTER;
    if (!(GTK_IS_BGBOX (widget)))
        RET();

    priv = GTK_BGBOX_GET_PRIVATE (widget);
    DBG("bg_type old:%d new:%d\n", priv->bg_type, bg_type);
    gtk_bgbox_free_bg(widget);
    priv->bg_type = bg_type;
    if (priv->bg_type == BG_STYLE) {
          gtk_style_set_background(widget->style, widget->window, widget->state);
    } else if (priv->bg_type == BG_ROOT) {
        if (priv->bg == NULL) 
            priv->bg = fb_bg_get_for_display();
        g_signal_connect(G_OBJECT(priv->bg), "changed", G_CALLBACK(gtk_bgbox_bg_changed), widget);
        priv->tintcolor = tintcolor;
        priv->alpha = alpha;
        gtk_bgbox_set_bg_root(widget, priv);
    }
    RET();
}

static void
gtk_bgbox_set_bg_root(GtkWidget *widget, GtkBgboxPrivate *priv)
{
    priv = GTK_BGBOX_GET_PRIVATE (widget);
    GdkRectangle rect;
    
    ENTER;
    priv->pixmap = fb_bg_get_xroot_pix_for_win(priv->bg, widget);
    DBG("here\n");
    if (!priv->pixmap || priv->pixmap ==  GDK_NO_BG) {
        priv->bg_type = BG_NONE;
        RET();
    }
    DBG("here\n");
    fb_bg_composite(priv->pixmap, widget->style->black_gc, priv->tintcolor, priv->alpha);
    DBG("here\n");
    gdk_window_set_back_pixmap(widget->window, priv->pixmap, FALSE);
    //gdk_window_clear(widget->window);
    rect.x = widget->allocation.x;
    rect.y = widget->allocation.y;
    rect.width = widget->allocation.width;
    rect.height = widget->allocation.height;
    //gdk_window_invalidate_rect(widget->window, &rect, TRUE);
    gtk_widget_queue_draw_area(widget, 0, 0, widget->allocation.width, widget->allocation.height);
    RET();
}
