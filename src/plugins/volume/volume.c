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

#include <gtk/gtk.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#include "volume-impl.h"

#include "volume_xpm.h"
#undef const

int mixer_fd;

GtkSpinButton *vol_spin;
static gdouble vol_before_mute;
static gdouble curr_volume;
static GtkWidget *curr_image;
static gboolean skip_botton1_event;

typedef struct {
    GtkWidget *mainw;
    GtkWidget *dlg;
    GtkTooltips* tooltips;
} volume_t;

static void
volume_destructor(Plugin *p)
{
    volume_t *vol = (volume_t *) p->priv;

    ENTER;
    if (vol->dlg)
        gtk_widget_destroy(vol->dlg);
    g_object_unref( vol->tooltips );
    gtk_widget_destroy(vol->mainw);
    if (mixer_fd)
        close(mixer_fd);
    g_free(vol);
    RET();
}

static void update_icon (Plugin* p)
{
	volume_t *vol = (volume_t*) p->priv;
	
	GdkPixbuf *icon;
	GtkWidget *image;
	GtkIconTheme* theme;
	GtkIconInfo* info;
	
	theme = p->panel->icon_theme;
	
	if (curr_volume <= 0) {
		info = gtk_icon_theme_lookup_icon( theme, "stock_volume-mute", p->panel->icon_size, 0 );
	}
	else if (curr_volume > 0 && curr_volume <= 50) {
		info = gtk_icon_theme_lookup_icon( theme, "stock_volume-min", p->panel->icon_size, 0 );
	}
	else if (curr_volume > 50 && curr_volume <= 75) {
		info = gtk_icon_theme_lookup_icon( theme, "stock_volume-med", p->panel->icon_size, 0 );
	}
	else if (curr_volume > 75) {
		info = gtk_icon_theme_lookup_icon( theme, "stock_volume-max", p->panel->icon_size, 0 );
	}

	if (info ) {
		icon = gdk_pixbuf_new_from_file_at_size(
				gtk_icon_info_get_filename( info ),
				p->panel->icon_size, p->panel->icon_size, NULL );
		gtk_icon_info_free( info );
	}
	else {
			icon = gdk_pixbuf_new_from_xpm_data((const char **) volume_xpm);
	}
	
	 
	
	if (icon) {
		if (curr_image) { 
			gtk_container_remove(GTK_CONTAINER (vol->mainw),curr_image);
			curr_image = NULL;
		}
		image = gtk_image_new_from_pixbuf(icon);
		gtk_container_add (GTK_CONTAINER (vol->mainw), image);
		 
		curr_image = image;
	}
	gtk_widget_show_all(vol->mainw);
	return;
}

static void on_volume_focus (GtkWidget* dlg, GdkEventFocus *event, Plugin* p)
{
	volume_t *vol = (volume_t*) p->priv;
	
	if (! vol_spin) return;
	GtkAdjustment *vol_adjustment = gtk_spin_button_get_adjustment (vol_spin);
	if (! vol_adjustment) return;
	curr_volume = gtk_adjustment_get_value (vol_adjustment);
	
	update_icon(p);
	
	/* FIXME: use smarter method */
	gtk_widget_destroy( dlg );
	vol->dlg = NULL;
}

static void on_mouse_scroll (GtkWidget* widget, GdkEventScroll* evt, Plugin* p)
{
	volume_t *vol = (volume_t*) p->priv;

	if ( ! vol->dlg ) {

		vol->dlg = create_volume_window();
		g_signal_connect( vol->mainw, "delete-event",
				G_CALLBACK(on_volume_focus), p );

	}
	else {
		if (! vol_spin) return;
		GtkAdjustment *vol_adjustment = 
			gtk_spin_button_get_adjustment (vol_spin);
		if (! vol_adjustment) return;

		curr_volume = gtk_adjustment_get_value (vol_adjustment);

		if (evt->direction == GDK_SCROLL_UP) {
			curr_volume += 2;
		}
		else /*if (evt->direction == GDK_SCROLL_DOWN)*/ {
			curr_volume -= 2;
		}
		update_icon(p);
		gtk_adjustment_set_value (vol_adjustment, curr_volume);
		gtk_spin_button_set_adjustment(vol_spin, vol_adjustment);
		skip_botton1_event = TRUE;
	}
}

static gboolean on_button_press (GtkWidget* widget, GdkEventButton* evt, Plugin* p)
{
	volume_t *vol = (volume_t*) p->priv;

	/* for scroll correction */
	if (skip_botton1_event) {
		gtk_widget_destroy(vol->dlg);
		vol->dlg = NULL;
		skip_botton1_event = FALSE;
	}

	switch ( evt->button ) { 
	case 1:	{	/*  Left click */
		if ( ! vol->dlg ) {
			vol->dlg = create_volume_window();

			/* setting background to default */
			gtk_widget_set_style(vol->dlg, p->panel->defstyle);

			g_signal_connect( vol->dlg, "focus-out-event",
					G_CALLBACK(on_volume_focus), p );

			gtk_window_present( GTK_WINDOW(vol->dlg) );
		}
		else {
			/* update icon */
			if (! vol_spin) return;
			GtkAdjustment *vol_adjustment =
				gtk_spin_button_get_adjustment (vol_spin);
			if (! vol_adjustment) return;
			curr_volume = gtk_adjustment_get_value (vol_adjustment);
			update_icon(p);

			gtk_widget_destroy(vol->dlg);
			vol->dlg = NULL;
		}
		break;
	}

	case 3:	{	/* right button */
		GtkMenu* popup = lxpanel_get_panel_menu( p->panel, p, FALSE );
		gtk_menu_popup( popup, NULL, NULL, NULL, NULL, evt->button, evt->time );
		return TRUE;
	}

	case 2:	{	/* middle mouse button */
		if ( ! vol->dlg ) {
			vol->dlg = create_volume_window();
		}

		if (! vol_spin) return;
		GtkAdjustment *vol_adjustment =
			gtk_spin_button_get_adjustment (vol_spin);
		if (! vol_adjustment) return;

		curr_volume = gtk_adjustment_get_value (vol_adjustment);

		if (curr_volume > 0) {
			/* turning to mute */
			vol_before_mute = curr_volume;
			curr_volume = 0;
		}
		else {
			curr_volume = vol_before_mute;
		}

		gtk_adjustment_set_value (vol_adjustment, curr_volume);
		gtk_spin_button_set_adjustment(vol_spin, vol_adjustment);

		update_icon(p);

		gtk_widget_destroy( vol->dlg );
		vol->dlg = NULL;
		break;
	}
	default:	/* never here */
		break;
	}
	return FALSE;
}

static int volume_constructor(Plugin *p, char **fp)
{
    volume_t *vol;
    line s;
    GdkPixbuf *icon;
    GtkWidget *image;
    GtkIconTheme* theme;
    GtkIconInfo* info;
    
    vol_before_mute = 1;
    curr_volume = 0;
    curr_image = NULL;
    skip_botton1_event = FALSE;

    ENTER;
    s.len = 256;
    vol = g_new0(volume_t, 1);
    g_return_val_if_fail(vol != NULL, 0);
    p->priv = vol;

    /* check if OSS mixer device could be open */
    mixer_fd = open ("/dev/mixer", O_RDWR, 0);
    if (mixer_fd < 0) {
        RET(0);
    }

    vol->mainw = gtk_event_box_new();

    gtk_widget_add_events( vol->mainw, GDK_BUTTON_PRESS_MASK );
    g_signal_connect( vol->mainw, "button-press-event",
            G_CALLBACK(on_button_press), p );
            
    g_signal_connect( vol->mainw, "scroll-event",
            G_CALLBACK(on_mouse_scroll), p );
    gtk_widget_set_size_request( vol->mainw, p->panel->icon_size, p->panel->icon_size );

    /* obtain current volume */
    vol->dlg = create_volume_window();
    if (! vol_spin) return;
	GtkAdjustment *vol_adjustment =
		gtk_spin_button_get_adjustment (vol_spin);
    if (! vol_adjustment) return;
    curr_volume = gtk_adjustment_get_value (vol_adjustment);
    
    update_icon(p);
	gtk_widget_destroy( vol->dlg );
    vol->dlg = NULL;  

    vol->tooltips = gtk_tooltips_new ();
    g_object_ref_sink( vol->tooltips );

    /* FIXME: display current level in tooltip. ex: "Volume Control: 80%"  */
    gtk_tooltips_set_tip (vol->tooltips, vol->mainw, _("Volume control"), NULL);

    p->pwid = vol->mainw;
    RET(1);
}


PluginClass volume_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "volume",
    name : N_("Volume Control"),
    version: "1.0",
    description : "Display and control volume",

    constructor : volume_constructor,
    destructor  : volume_destructor,
    config : NULL,
    save : NULL
};
