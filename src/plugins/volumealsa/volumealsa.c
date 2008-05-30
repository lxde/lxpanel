/**
 * Copyright (c) 2008 LxDE Developers, see the file AUTHORS for details.
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
#include <glib.h>
#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <alsa/asoundlib.h>
#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "dbg.h"

#define ICONS_VOLUME PACKAGE_DATA_DIR "/lxpanel/images/volume.png"
#define ICONS_MUTE PACKAGE_DATA_DIR "/lxpanel/images/mute.png"

typedef struct {
    Plugin* plugin;
    GtkWidget *mainw;
    GtkWidget *tray_icon;
    GtkWidget *dlg;
    GtkTooltips* tooltips;
    GtkWidget *vscale;
    snd_mixer_t *mixer;
    snd_mixer_selem_id_t *sid;
    snd_mixer_elem_t *master_element;
    long alsa_min_vol, alsa_max_vol;
    int mute;
    int show;
} volume_t;



/* ALSA */
static gboolean find_element(volume_t *vol, const char *ename)
{
    for (vol->master_element=snd_mixer_first_elem(vol->mixer);vol->master_element;vol->master_element=snd_mixer_elem_next(vol->master_element)) {
        snd_mixer_selem_get_id(vol->master_element, vol->sid);
        if (!snd_mixer_selem_is_active(vol->master_element))
            continue;

        if (strcmp(ename, snd_mixer_selem_id_get_name(vol->sid))==0) {
            return TRUE;
        }
    }

    return FALSE;
}

static gboolean asound_init(volume_t *vol)
{
    snd_mixer_selem_id_alloca(&vol->sid);
    snd_mixer_open(&vol->mixer, 0);
    snd_mixer_attach(vol->mixer, "default");
    snd_mixer_selem_register(vol->mixer, NULL, NULL);
    snd_mixer_load(vol->mixer);

    /* Find Master element */
    if (!find_element(vol, "Master"))
        if (!find_element(vol, "Front"))
            if (!find_element(vol, "PCM"))
                return FALSE;


    snd_mixer_selem_get_playback_volume_range(vol->master_element, &vol->alsa_min_vol, &vol->alsa_max_vol);

    snd_mixer_selem_set_playback_volume_range(vol->master_element, 0, 100);

    return TRUE;
}

static int asound_read(volume_t *vol)
{
    long aleft, aright;
    snd_mixer_handle_events(vol->mixer);
    /* Left */
    snd_mixer_selem_get_playback_volume(vol->master_element, SND_MIXER_SCHN_FRONT_LEFT, &aleft);
    /* Right */
    snd_mixer_selem_get_playback_volume(vol->master_element, SND_MIXER_SCHN_FRONT_RIGHT, &aright);

    return (aleft + aright) >> 1;
}

static void asound_write(volume_t *vol, int volume)
{
    snd_mixer_selem_set_playback_volume(vol->master_element, SND_MIXER_SCHN_FRONT_LEFT, volume);
    snd_mixer_selem_set_playback_volume(vol->master_element, SND_MIXER_SCHN_FRONT_RIGHT, volume);
}

static gboolean focus_out_event(GtkWidget *widget, GdkEvent *event, volume_t *vol)
{
    gtk_widget_hide(vol->dlg);
    vol->show = 0;
    return FALSE;
}

static gboolean tray_icon_press(GtkWidget *widget, GdkEventButton *event, volume_t *vol)
{
    if( event->button == 3 )  /* right button */
    {
        GtkMenu* popup = lxpanel_get_panel_menu( vol->plugin->panel, vol->plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, event->button, event->time );
        return TRUE;
    }

    if (vol->show==0) {
        gtk_window_set_position(GTK_WINDOW(vol->dlg), GTK_WIN_POS_MOUSE);
        gtk_scale_set_digits(GTK_SCALE(vol->vscale), asound_read(vol));
        gtk_widget_show_all(vol->dlg);
        vol->show = 1;
    } else {
        gtk_widget_hide(vol->dlg);
        vol->show = 0;
    }
    return TRUE;
}

static void on_vscale_value_changed(GtkRange *range, volume_t *vol)
{
    asound_write(vol, gtk_range_get_value(range));
}

static void click_mute(GtkWidget *widget, volume_t *vol)
{
    int chn;

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) {
        gtk_image_set_from_file(vol->tray_icon, ICONS_MUTE);
        for (chn = 0; chn <= SND_MIXER_SCHN_LAST; chn++) {
            snd_mixer_selem_set_playback_switch(vol->master_element, chn, 0);
        }
    } else {
        gtk_image_set_from_file(vol->tray_icon, ICONS_VOLUME);
        for (chn = 0; chn <= SND_MIXER_SCHN_LAST; chn++) {
            snd_mixer_selem_set_playback_switch(vol->master_element, chn, 1);
        }
    }
}

static void panel_init(Plugin *p)
{
    volume_t *vol = p->priv;
    GtkWidget *scrolledwindow;
    GtkWidget *viewport;
    GtkWidget *box;
    GtkWidget *frame;
    GtkWidget *checkbutton;

    /* set show flags */
    vol->show = 0;

    /* create a new window */
    vol->dlg = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_decorated(GTK_WINDOW(vol->dlg), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(vol->dlg), 5);
    gtk_window_set_default_size(GTK_WINDOW(vol->dlg), 80, 140);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(vol->dlg), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(vol->dlg), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(vol->dlg), GDK_WINDOW_TYPE_HINT_DIALOG);

    /* setting background to default */
    //gtk_widget_set_style(vol->dlg, p->panel->defstyle);

    /* Focus-out signal */
    g_signal_connect (G_OBJECT (vol->dlg), "focus_out_event",
              G_CALLBACK (focus_out_event), vol);

    scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width (GTK_CONTAINER (scrolledwindow), 0);
    gtk_widget_show (scrolledwindow);
    gtk_container_add (GTK_CONTAINER (vol->dlg), scrolledwindow);
    GTK_WIDGET_UNSET_FLAGS (scrolledwindow, GTK_CAN_FOCUS);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_SHADOW_NONE);

    viewport = gtk_viewport_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scrolledwindow), viewport);
    gtk_viewport_set_shadow_type (GTK_VIEWPORT (viewport), GTK_SHADOW_NONE);
    gtk_widget_show(viewport);

    /* create frame */
    frame = gtk_frame_new(_("Volume"));
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(viewport), frame);

    /* create box */
    box = gtk_vbox_new(FALSE, 0);

    /* create controller */
    vol->vscale = gtk_vscale_new(GTK_ADJUSTMENT(gtk_adjustment_new(asound_read(vol), 0, 100, 0, 0, 0)));
    gtk_scale_set_draw_value(GTK_SCALE(vol->vscale), FALSE);
    gtk_range_set_inverted(GTK_RANGE(vol->vscale), TRUE);

    g_signal_connect ((gpointer) vol->vscale, "value_changed",
                      G_CALLBACK (on_vscale_value_changed),
                      vol);

    checkbutton = gtk_check_button_new_with_label(_("Mute"));
    snd_mixer_selem_get_playback_switch(vol->master_element, 0, &vol->mute);

    if (!vol->mute)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checkbutton), TRUE);

    g_signal_connect ((gpointer) checkbutton, "toggled",
                      G_CALLBACK (click_mute),
                      vol);

    gtk_box_pack_start(GTK_BOX(box), vol->vscale, TRUE, TRUE, 0);
    gtk_box_pack_end(GTK_BOX(box), checkbutton, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), box);

    /* setting background to default */
    gtk_widget_set_style(viewport, p->panel->defstyle);
}

static void
volumealsa_destructor(Plugin *p)
{
    volume_t *vol = (volume_t *) p->priv;

    ENTER;
    if (vol->dlg)
        gtk_widget_destroy(vol->dlg);

    g_free(vol);
    RET();
}

static int
volumealsa_constructor(Plugin *p, char **fp)
{
    volume_t *vol;
    line s;
    GdkPixbuf *icon;
    GtkWidget *image;
    GtkIconTheme* theme;
    GtkIconInfo* info;

    ENTER;
    s.len = 256;
    vol = g_new0(volume_t, 1);
    vol->plugin = p;
    g_return_val_if_fail(vol != NULL, 0);
    p->priv = vol;

    /* initializing */
    if (!asound_init(vol))
        RET(1);

    panel_init(p);

    /* main */
    vol->mainw = gtk_event_box_new();

    gtk_widget_add_events(vol->mainw, GDK_BUTTON_PRESS_MASK);
    gtk_widget_set_size_request( vol->mainw, 24, 24 );

    g_signal_connect(G_OBJECT(vol->mainw), "button-press-event",
                         G_CALLBACK(tray_icon_press), vol);

    /* tray icon */
    snd_mixer_selem_get_playback_switch(vol->master_element, 0, &vol->mute);
    if (vol->mute==0)
        vol->tray_icon = gtk_image_new_from_file(ICONS_MUTE);
    else
        vol->tray_icon = gtk_image_new_from_file(ICONS_VOLUME);

    gtk_container_add(GTK_CONTAINER(vol->mainw), vol->tray_icon);

    gtk_widget_show_all(vol->mainw);

    vol->tooltips = p->panel->tooltips;;
#if GLIB_CHECK_VERSION( 2, 10, 0 )
    g_object_ref_sink( vol->tooltips );
#else
    g_object_ref( vol->tooltips );
    gtk_object_sink( vol->tooltips );
#endif

    /* FIXME: display current level in tooltip. ex: "Volume Control: 80%"  */
    gtk_tooltips_set_tip (vol->tooltips, vol->mainw, _("Volume control"), NULL);

    /* store the created plugin widget in plugin->pwid */
    p->pwid = vol->mainw;

    RET(1);
}


PluginClass volumealsa_plugin_class = {
    fname: NULL,
    count: 0,

    type : "volumealsa",
    name : N_("Volume Control"),
    version: "1.0",
    description : "Display and control volume for ALSA",

    constructor : volumealsa_constructor,
    destructor  : volumealsa_destructor,
    config : NULL,
    save : NULL
};
