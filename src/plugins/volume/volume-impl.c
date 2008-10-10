/**
 * Copyright (c) 2006, 2008 LxDE Developers,
 * 	see the file AUTHORS for details.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "volume-impl.h"

#ifdef __FreeBSD__
#include <sys/soundcard.h>
#elif defined(__linux__) || defined(__Linux__)
#include <linux/soundcard.h>
#else
#error "Not supported platform"
#endif

#include <fcntl.h>

extern int mixer_fd;
extern GtkSpinButton* vol_spin;

typedef struct stereovolume
{
	unsigned char left;
	unsigned char right;
} StereoVolume;

static StereoVolume tmpvol;

typedef struct _originalset
{
	unsigned char mainvol;
	unsigned char pcm;
	unsigned char speaker;
	unsigned char line1;
	unsigned char cd;
	unsigned char mic;
} originalset;

static originalset orig;

static GtkWidget *volume_window;
static GtkWidget *vbox1;
static GtkWidget *hbox1;
static GtkWidget *spinbutton1;
static GtkWidget *hbox3;
static GtkWidget *vscale1;
static GtkAccelGroup *accel_group;
static GtkAdjustment *vol_adjustment;
static GtkWidget *hbox4;
static GtkWidget *frame;

static gboolean
on_spinbutton1_button_release_event (GtkWidget *widget,
                                     GdkEventButton *event,
                                     gpointer user_data);

static gboolean
on_vscale1_button_release_event (GtkWidget *widget,
                                 GdkEventButton *event,
                                 gpointer user_data);

static void
get_current_levels() 
{
	ioctl(mixer_fd, MIXER_READ(SOUND_MIXER_VOLUME), &tmpvol);
	orig.mainvol = tmpvol.left;
	gtk_adjustment_set_value(GTK_ADJUSTMENT(vol_adjustment), (double) tmpvol.left);
}

GtkWidget*
create_volume_window (void)
{
	accel_group = gtk_accel_group_new ();

	volume_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_size_request (volume_window, 60, 240);
	gtk_container_set_border_width (GTK_CONTAINER (volume_window), 2);

	gtk_window_set_resizable (GTK_WINDOW (volume_window), FALSE);
	gtk_window_set_position (GTK_WINDOW (volume_window), GTK_WIN_POS_MOUSE);
	gtk_window_set_decorated (GTK_WINDOW (volume_window), FALSE);
	gtk_window_set_skip_taskbar_hint( GTK_WINDOW (volume_window), TRUE );

	frame = gtk_frame_new (_("Volume"));

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox1);
	gtk_container_add (GTK_CONTAINER (frame), vbox1);
	gtk_container_add (GTK_CONTAINER (volume_window), frame);
	gtk_widget_show (frame);
	vol_adjustment = GTK_ADJUSTMENT (gtk_adjustment_new (1, 0, 100, 1, 10, 0));

	/* get original adjustments */
	get_current_levels();

	hbox1 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox1);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox1, FALSE, TRUE, 8);


	/* ajustment, climb rate, digits */
	spinbutton1 = gtk_spin_button_new (GTK_ADJUSTMENT (vol_adjustment), 1, 0);
	gtk_widget_show (spinbutton1);

	gtk_box_pack_start (GTK_BOX (hbox1), spinbutton1, TRUE, FALSE, 0);
	gtk_widget_set_size_request (spinbutton1, -1, 22);
	g_signal_connect ((gpointer) spinbutton1, "value_changed",
			  G_CALLBACK (on_spinbutton1_button_release_event),
			  NULL);
	g_object_set_data_full(G_OBJECT (volume_window), "spinbutton1", 
			       gtk_widget_ref (spinbutton1),
			       (GDestroyNotify) gtk_widget_unref);

	hbox3 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox3);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox3, TRUE, TRUE, 5);

	vscale1 = gtk_vscale_new (GTK_ADJUSTMENT (vol_adjustment));
	gtk_widget_show (vscale1);
	gtk_range_set_inverted (GTK_RANGE (vscale1), TRUE);
	gtk_box_pack_start (GTK_BOX (hbox3), vscale1, TRUE, TRUE, 0);
	gtk_scale_set_draw_value (GTK_SCALE (vscale1), FALSE);
	g_signal_connect ((gpointer) vscale1, "button_release_event",
			  G_CALLBACK (on_vscale1_button_release_event),
			  vol_adjustment);
	g_object_set_data_full(G_OBJECT (volume_window), "vscale1",
			       gtk_widget_ref (vscale1),
			       (GDestroyNotify) gtk_widget_unref);

	hbox4 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox4);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox4, FALSE, TRUE, 8);

	g_signal_connect ((gpointer) volume_window, "destroy",
			  G_CALLBACK (gtk_widget_destroy),
			  NULL);

	g_object_set_data (G_OBJECT (volume_window), "volume_window", volume_window);
	g_object_set_data_full (G_OBJECT (volume_window), "frame",
			        gtk_widget_ref (frame), 
				(GDestroyNotify) gtk_widget_unref);
	g_object_set_data_full (G_OBJECT (volume_window), "vbox1",
			        gtk_widget_ref (vbox1), 
				(GDestroyNotify) gtk_widget_unref);
	g_object_set_data_full (G_OBJECT (volume_window), "hbox1",
			        gtk_widget_ref (hbox1), 
				(GDestroyNotify) gtk_widget_unref);
	g_object_set_data_full (G_OBJECT (volume_window), "hbox3",
			        gtk_widget_ref (hbox3), 
				(GDestroyNotify) gtk_widget_unref);
	g_object_set_data_full (G_OBJECT (volume_window), "hbox4",
			        gtk_widget_ref (hbox4), 
				(GDestroyNotify) gtk_widget_unref);

	gtk_window_add_accel_group (GTK_WINDOW (volume_window), accel_group);

	vol_spin = spinbutton1;

	return volume_window;
}

static StereoVolume vol;

static gboolean 
on_spinbutton1_button_release_event (GtkWidget *widget, 
		                     GdkEventButton *event,
				     gpointer user_data)
{
	GtkSpinButton *spin;
	extern GtkWidget *spinbutton1;
	spin = GTK_SPIN_BUTTON (spinbutton1);
	vol.left = vol.right = (int) gtk_spin_button_get_value_as_int (spin);
	ioctl(mixer_fd,MIXER_WRITE(SOUND_MIXER_VOLUME), &vol);

	return FALSE;
}

static gboolean
on_vscale1_button_release_event (GtkWidget *widget,
		 		 GdkEventButton *event,
		                 gpointer user_data)
{
	vol.left = vol.right = 
		(int) gtk_adjustment_get_value(GTK_ADJUSTMENT(user_data));
	ioctl(mixer_fd, MIXER_WRITE(SOUND_MIXER_VOLUME), &vol);
	return FALSE;
}
