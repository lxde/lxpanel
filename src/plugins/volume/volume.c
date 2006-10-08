#include <gtk/gtk.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <gdk-pixbuf/gdk-pixbuf.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#include "volume-impl.h"

#include "volume_xpm.h"

int mixer_fd;

typedef struct {
	GtkWidget *mainw;
	GtkWidget *dlg;
} volume_t;

static void
volume_destructor(plugin *p)
{
	volume_t *ns = (volume_t *) p->priv;

	ENTER;
	if (ns->dlg)
		gtk_widget_destroy(ns->dlg);
	gtk_widget_destroy(ns->mainw);
	if (mixer_fd)
		close(mixer_fd);
	g_free(ns);
	RET();
}

static void on_volume_focus (GtkWidget* dlg, GdkEventFocus *event, volume_t *ns)
{
	/* FIXME: use smarter method */
	gtk_widget_destroy( dlg );
	ns->dlg = NULL;
}

static void on_button_press (GtkWidget* widget, GdkEventButton* evt, plugin* p)
{
	volume_t *ns = (volume_t*) p->priv;

	if( evt->button == 1 ) { /*  Left click*/
		if( ! ns->dlg ) {
			ns->dlg = create_volume_window();
			g_signal_connect( ns->dlg, "focus-out-event", on_volume_focus, ns );
		}
		gtk_window_present( GTK_WINDOW(ns->dlg) );
	}
}

static int
volume_constructor(plugin *p)
{
	volume_t *ns;
	line s;
	GdkPixbuf *icon;
	GtkWidget *image;

	ENTER;
	s.len = 256;  
	ns = g_new0(volume_t, 1);
	g_return_val_if_fail(ns != NULL, 0);
	p->priv = ns;

	/* check if OSS mixer device could be open */
	mixer_fd = open ("/dev/mixer", O_RDWR, 0);
	if (mixer_fd < 0) {
		RET(0);
	}

	ns->mainw = gtk_event_box_new();

	icon = gdk_pixbuf_new_from_xpm_data(volume_xpm);
	if(icon) {
		image = gtk_image_new_from_pixbuf(icon);
		gtk_container_add (GTK_CONTAINER (ns->mainw), image);
	}

	gtk_widget_add_events( ns->mainw, GDK_BUTTON_PRESS_MASK );
	g_signal_connect( ns->mainw, "button-press-event",
			G_CALLBACK(on_button_press), p );
	gtk_widget_set_size_request( ns->mainw, 24, 24 );

	ns->dlg = NULL;
	gtk_widget_show_all(ns->mainw);

	gtk_container_add(GTK_CONTAINER(p->pwid), ns->mainw);

	RET(1);

error:
	volume_destructor(p);
	RET(0);
}


plugin_class volume_plugin_class = {
	fname: NULL,
	count: 0,

	type : "volume",
	name : "volume",
	version: "1.0",
	description : "Display and control volume",

	constructor : volume_constructor,
	destructor  : volume_destructor,
};
