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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
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

#ifndef __GTK_BGBOX_H__
#define __GTK_BGBOX_H__


#include <gdk/gdk.h>
#include <gtk/gtkbin.h>
#include "bg.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GTK_TYPE_BGBOX              (gtk_bgbox_get_type ())
#define GTK_BGBOX(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_BGBOX, GtkBgbox))
#define GTK_BGBOX_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_BGBOX, GtkBgboxClass))
#define GTK_IS_BGBOX(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_BGBOX))
#define GTK_IS_BGBOX_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_BGBOX))
#define GTK_BGBOX_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_BGBOX, GtkBgboxClass))

typedef struct _GtkBgbox	  GtkBgbox;
typedef struct _GtkBgboxClass  GtkBgboxClass;

struct _GtkBgbox
{
    GtkBin bin;
};

struct _GtkBgboxClass
{
    GtkBinClass parent_class;
};

enum { BG_NONE, BG_STYLE, BG_ROOT, BG_LAST };

GType	   gtk_bgbox_get_type (void) G_GNUC_CONST;
GtkWidget* gtk_bgbox_new (void);
void gtk_bgbox_set_background (GtkWidget *widget, int bg_type, guint32 tintcolor, gint alpha);


#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GTK_BGBOX_H__ */
