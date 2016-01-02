/*
 * Copyright (C) 2008 Fred Chien <fred@lxde.org>
 *               2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2010-2012 Julien Lavergne <julien.lavergne@gmail.com>
 *               2012 Henry Gebhardt <hsggebhardt@gmail.com>
 *               2014 Peter <ombalaxitabou@users.sf.net>
 *               2014-2015 Andriy Grytsenko <andrej@rep.kiev.ua>
 *
 * This file is a part of LXPanel project.
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

#define _ISOC99_SOURCE /* lrint() */
#define _GNU_SOURCE /* exp10() */

#include <gtk/gtk.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <alsa/asoundlib.h>
#include <poll.h>
#include <math.h>
#include <libfm/fm-gtk.h>
#include "plugin.h"
#include "misc.h"

#define ICONS_VOLUME_HIGH   "volume-high"
#define ICONS_VOLUME_MEDIUM "volume-medium"
#define ICONS_VOLUME_LOW    "volume-low"
#define ICONS_MUTE          "mute"

#ifdef __UCLIBC__
/* 10^x = 10^(log e^x) = (e^x)^log10 = e^(x * log 10) */
# define M_LN10		2.30258509299404568402	/* log_e 10 */
#define exp10(x) (exp((x) * log(10)))
#endif /* __UCLIBC__ */

#define MAX_LINEAR_DB_SCALE 24

typedef struct {

    /* Graphics. */
    GtkWidget * plugin;				/* Back pointer to the widget */
    LXPanel * panel;				/* Back pointer to panel */
    config_setting_t * settings;		/* Plugin settings */
    GtkWidget * tray_icon;			/* Displayed image */
    GtkWidget * popup_window;			/* Top level window for popup */
    GtkWidget * volume_scale;			/* Scale for volume */
    GtkWidget * mute_check;			/* Checkbox for mute state */
    gboolean show_popup;			/* Toggle to show and hide the popup on left click */
    guint volume_scale_handler;			/* Handler for vscale widget */
    guint mute_check_handler;			/* Handler for mute_check widget */

    /* ALSA interface. */
    snd_mixer_t * mixer;			/* The mixer */
    snd_mixer_selem_id_t * sid;			/* The element ID */
    snd_mixer_elem_t * master_element;		/* The Master element */
    guint mixer_evt_idle;			/* Timer to handle restarting poll */
    guint restart_idle;
    gint alsamixer_mapping;

    /* unloading and error handling */
    GIOChannel **channels;                      /* Channels that we listen to */
    guint *watches;                             /* Watcher IDs for channels */
    guint num_channels;                         /* Number of channels */

    /* Icons */
    const char* icon_panel;
    const char* icon_fallback;

} VolumeALSAPlugin;

static gboolean asound_restart(gpointer vol_gpointer);
static gboolean asound_initialize(VolumeALSAPlugin * vol);
static void asound_deinitialize(VolumeALSAPlugin * vol);
static void volumealsa_update_display(VolumeALSAPlugin * vol);
static void volumealsa_destructor(gpointer user_data);

/*** ALSA ***/

static gboolean asound_find_element(VolumeALSAPlugin * vol, const char * ename)
{
    for (
      vol->master_element = snd_mixer_first_elem(vol->mixer);
      vol->master_element != NULL;
      vol->master_element = snd_mixer_elem_next(vol->master_element))
    {
        snd_mixer_selem_get_id(vol->master_element, vol->sid);
        if ((snd_mixer_selem_is_active(vol->master_element))
        && (strcmp(ename, snd_mixer_selem_id_get_name(vol->sid)) == 0))
            return TRUE;
    }
    return FALSE;
}

/* NOTE by PCMan:
 * This is magic! Since ALSA uses its own machanism to handle this part.
 * After polling of mixer fds, it requires that we should call
 * snd_mixer_handle_events to clear all pending mixer events.
 * However, when using the glib IO channels approach, we don't have
 * poll() and snd_mixer_poll_descriptors_revents(). Due to the design of
 * glib, on_mixer_event() will be called for every fd whose status was
 * changed. So, after each poll(), it's called for several times,
 * not just once. Therefore, we cannot call snd_mixer_handle_events()
 * directly in the event handler. Otherwise, it will get called for
 * several times, which might clear unprocessed pending events in the queue.
 * So, here we call it once in the event callback for the first fd.
 * Then, we don't call it for the following fds. After all fds with changed
 * status are handled, we remove this restriction in an idle handler.
 * The next time the event callback is involked for the first fs, we can
 * call snd_mixer_handle_events() again. Racing shouldn't happen here
 * because the idle handler has the same priority as the io channel callback.
 * So, io callbacks for future pending events should be in the next gmain
 * iteration, and won't be affected.
 */

static gboolean asound_reset_mixer_evt_idle(VolumeALSAPlugin * vol)
{
    if (!g_source_is_destroyed(g_main_current_source()))
        vol->mixer_evt_idle = 0;
    return FALSE;
}

/* Handler for I/O event on ALSA channel. */
static gboolean asound_mixer_event(GIOChannel * channel, GIOCondition cond, gpointer vol_gpointer)
{
    VolumeALSAPlugin * vol = (VolumeALSAPlugin *) vol_gpointer;
    int res = 0;

    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;

    if (vol->mixer_evt_idle == 0)
    {
        vol->mixer_evt_idle = g_idle_add_full(G_PRIORITY_DEFAULT, (GSourceFunc) asound_reset_mixer_evt_idle, vol, NULL);
        res = snd_mixer_handle_events(vol->mixer);
    }

    if (cond & G_IO_IN)
    {
        /* the status of mixer is changed. update of display is needed. */
        volumealsa_update_display(vol);
    }

    if ((cond & G_IO_HUP) || (res < 0))
    {
        /* This means there're some problems with alsa. */
        g_warning("volumealsa: ALSA (or pulseaudio) had a problem: "
                "volumealsa: snd_mixer_handle_events() = %d,"
                " cond 0x%x (IN: 0x%x, HUP: 0x%x).", res, cond,
                G_IO_IN, G_IO_HUP);
        gtk_widget_set_tooltip_text(vol->plugin, "ALSA (or pulseaudio) had a problem."
                " Please check the lxpanel logs.");

        if (vol->restart_idle == 0)
            vol->restart_idle = g_timeout_add_seconds(1, asound_restart, vol);

        return FALSE;
    }

    return TRUE;
}

static gboolean asound_restart(gpointer vol_gpointer)
{
    VolumeALSAPlugin * vol = vol_gpointer;

    if (g_source_is_destroyed(g_main_current_source()))
        return FALSE;

    asound_deinitialize(vol);

    if (!asound_initialize(vol)) {
        g_warning("volumealsa: Re-initialization failed.");
        return TRUE; // try again in a second
    }

    g_warning("volumealsa: Restarted ALSA interface...");

    vol->restart_idle = 0;
    return FALSE;
}

/* Initialize the ALSA interface. */
static gboolean asound_initialize(VolumeALSAPlugin * vol)
{
    /* Access the "default" device. */
    snd_mixer_selem_id_alloca(&vol->sid);
    snd_mixer_open(&vol->mixer, 0);
    snd_mixer_attach(vol->mixer, "default");
    snd_mixer_selem_register(vol->mixer, NULL, NULL);
    snd_mixer_load(vol->mixer);

    /* Find Master element, or Front element, or PCM element, or LineOut element.
     * If one of these succeeds, master_element is valid. */
    if ( ! asound_find_element(vol, "Master"))
        if ( ! asound_find_element(vol, "Front"))
            if ( ! asound_find_element(vol, "PCM"))
                if ( ! asound_find_element(vol, "LineOut"))
                    return FALSE;

    /* Set the playback volume range as we wish it. */
    if ( ! vol->alsamixer_mapping)
        snd_mixer_selem_set_playback_volume_range(vol->master_element, 0, 100);

    /* Listen to events from ALSA. */
    int n_fds = snd_mixer_poll_descriptors_count(vol->mixer);
    struct pollfd * fds = g_new0(struct pollfd, n_fds);

    vol->channels = g_new0(GIOChannel *, n_fds);
    vol->watches = g_new0(guint, n_fds);
    vol->num_channels = n_fds;

    snd_mixer_poll_descriptors(vol->mixer, fds, n_fds);
    int i;
    for (i = 0; i < n_fds; ++i)
    {
        GIOChannel* channel = g_io_channel_unix_new(fds[i].fd);
        vol->watches[i] = g_io_add_watch(channel, G_IO_IN | G_IO_HUP, asound_mixer_event, vol);
        vol->channels[i] = channel;
    }
    g_free(fds);
    return TRUE;
}

static void asound_deinitialize(VolumeALSAPlugin * vol)
{
    guint i;

    if (vol->mixer_evt_idle != 0) {
        g_source_remove(vol->mixer_evt_idle);
        vol->mixer_evt_idle = 0;
    }

    for (i = 0; i < vol->num_channels; i++) {
        g_source_remove(vol->watches[i]);
        g_io_channel_shutdown(vol->channels[i], FALSE, NULL);
        g_io_channel_unref(vol->channels[i]);
    }
    g_free(vol->channels);
    g_free(vol->watches);
    vol->channels = NULL;
    vol->watches = NULL;
    vol->num_channels = 0;

    snd_mixer_close(vol->mixer);
    vol->master_element = NULL;
    /* FIXME: unalloc vol->sid */
}

/* Get the presence of the mute control from the sound system. */
static gboolean asound_has_mute(VolumeALSAPlugin * vol)
{
    return ((vol->master_element != NULL) ? snd_mixer_selem_has_playback_switch(vol->master_element) : FALSE);
}

/* Get the condition of the mute control from the sound system. */
static gboolean asound_is_muted(VolumeALSAPlugin * vol)
{
    /* The switch is on if sound is not muted, and off if the sound is muted.
     * Initialize so that the sound appears unmuted if the control does not exist. */
    int value = 1;
    if (vol->master_element != NULL)
        snd_mixer_selem_get_playback_switch(vol->master_element, 0, &value);
    return (value == 0);
}

static long lrint_dir(double x, int dir)
{
    if (dir > 0)
        return lrint(ceil(x));
    else if (dir < 0)
        return lrint(floor(x));
    else
        return lrint(x);
}

static inline gboolean use_linear_dB_scale(long dBmin, long dBmax)
{
    return dBmax - dBmin <= MAX_LINEAR_DB_SCALE * 100;
}

static long get_normalized_volume(snd_mixer_elem_t *elem,
                                    snd_mixer_selem_channel_id_t channel)
{
    long min, max, value;
    double normalized, min_norm;
    int err;

    err = snd_mixer_selem_get_playback_dB_range(elem, &min, &max);
    if (err < 0 || min >= max) {
        err = snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        if (err < 0 || min == max)
            return 0;

        err = snd_mixer_selem_get_playback_volume(elem, channel, &value);
        if (err < 0)
            return 0;

        return lrint(100.0 * (value - min) / (double)(max - min));
    }

    err = snd_mixer_selem_get_playback_dB(elem, channel, &value);
    if (err < 0)
        return 0;

    if (use_linear_dB_scale(min, max))
        return lrint(100.0 * (value - min) / (double)(max - min));

    normalized = exp10((value - max) / 6000.0);
    if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
        min_norm = exp10((min - max) / 6000.0);
        normalized = (normalized - min_norm) / (1 - min_norm);
    }

    return lrint(100.0 * normalized);
}

/* Get the volume from the sound system.
 * This implementation returns the average of the Front Left and Front Right channels. */
static int asound_get_volume(VolumeALSAPlugin * vol)
{
    long aleft = 0;
    long aright = 0;

    if (vol->master_element != NULL)
    {
        if ( ! vol->alsamixer_mapping)
        {
            snd_mixer_selem_get_playback_volume(vol->master_element, SND_MIXER_SCHN_FRONT_LEFT, &aleft);
            snd_mixer_selem_get_playback_volume(vol->master_element, SND_MIXER_SCHN_FRONT_RIGHT, &aright);
        }
        else
        {
            aleft = get_normalized_volume(vol->master_element, SND_MIXER_SCHN_FRONT_LEFT);
            aright = get_normalized_volume(vol->master_element, SND_MIXER_SCHN_FRONT_RIGHT);
        }
    }
    return (aleft + aright) >> 1;
}

static int set_normalized_volume(snd_mixer_elem_t *elem,
                                 snd_mixer_selem_channel_id_t channel,
                                 int vol,
                                 int dir)
{
    long min, max, value;
    double min_norm, volume;
    int err;

    volume = vol / 100.0;

    err = snd_mixer_selem_get_playback_dB_range(elem, &min, &max);
    if (err < 0 || min >= max) {
        err = snd_mixer_selem_get_playback_volume_range(elem, &min, &max);
        if (err < 0)
            return err;

        value = lrint_dir(volume * (max - min), dir) + min;
        return snd_mixer_selem_set_playback_volume(elem, channel, value);
    }

    if (use_linear_dB_scale(min, max)) {
        value = lrint_dir(volume * (max - min), dir) + min;
        return snd_mixer_selem_set_playback_dB(elem, channel, value, dir);
    }

    if (min != SND_CTL_TLV_DB_GAIN_MUTE) {
        min_norm = exp10((min - max) / 6000.0);
        volume = volume * (1 - min_norm) + min_norm;
    }
    value = lrint_dir(6000.0 * log10(volume), dir) + max;

    return snd_mixer_selem_set_playback_dB(elem, channel, value, dir);
}

/* Set the volume to the sound system.
 * This implementation sets the Front Left and Front Right channels to the specified value. */
static void asound_set_volume(VolumeALSAPlugin * vol, int volume)
{
    int dir = volume - asound_get_volume(vol);

    /* Volume is set to the correct value already */
    if (dir == 0)
        return;

    if (vol->master_element != NULL)
    {
        if ( ! vol->alsamixer_mapping)
        {
            snd_mixer_selem_set_playback_volume(vol->master_element, SND_MIXER_SCHN_FRONT_LEFT, volume);
            snd_mixer_selem_set_playback_volume(vol->master_element, SND_MIXER_SCHN_FRONT_RIGHT, volume);
        }
        else
        {
            set_normalized_volume(vol->master_element, SND_MIXER_SCHN_FRONT_LEFT, volume, dir);
            set_normalized_volume(vol->master_element, SND_MIXER_SCHN_FRONT_RIGHT, volume, dir);
        }
    }
}

/*** Graphics ***/

static void volumealsa_lookup_current_icon(VolumeALSAPlugin * vol, gboolean mute, int level)
{
    /* Change icon according to mute / volume */
    const char* icon_panel="audio-volume-muted-panel";
    const char* icon_fallback=ICONS_MUTE;
    if (mute)
    {
         icon_panel = "audio-volume-muted-panel";
         icon_fallback=ICONS_MUTE;
    }
    else if (level >= 66)
    {
         icon_panel = "audio-volume-high-panel";
         icon_fallback=ICONS_VOLUME_HIGH;
    }
    else if (level >= 33)
    {
         icon_panel = "audio-volume-medium-panel";
         icon_fallback=ICONS_VOLUME_MEDIUM;
    }
    else if (level > 0)
    {
         icon_panel = "audio-volume-low-panel";
         icon_fallback=ICONS_VOLUME_LOW;
    }

    vol->icon_panel = icon_panel;
    vol->icon_fallback = icon_fallback;
}

static void volumealsa_update_current_icon(VolumeALSAPlugin * vol, gboolean mute, int level)
{
    /* Find suitable icon */
    volumealsa_lookup_current_icon(vol, mute, level);

    /* Change icon, fallback to default icon if theme doesn't exsit */
    lxpanel_image_change_icon(vol->tray_icon, vol->icon_panel, vol->icon_fallback);

    /* Display current level in tooltip. */
    char * tooltip = g_strdup_printf("%s %d", _("Volume control"), level);
    gtk_widget_set_tooltip_text(vol->plugin, tooltip);
    g_free(tooltip);
}

/*
 * Here we just update volume's vertical scale and mute check button.
 * The rest will be updated by signal handelrs.
 */
static void volumealsa_update_display(VolumeALSAPlugin * vol)
{
    /* Mute. */
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vol->mute_check), asound_is_muted(vol));
    gtk_widget_set_sensitive(vol->mute_check, (asound_has_mute(vol)));

    /* Volume. */
    if (vol->volume_scale != NULL)
    {
        gtk_range_set_value(GTK_RANGE(vol->volume_scale), asound_get_volume(vol));
    }
}

/* Handler for "button-press-event" signal on main widget. */
static gboolean volumealsa_button_press_event(GtkWidget * widget, GdkEventButton * event, LXPanel * panel)
{
    VolumeALSAPlugin * vol = lxpanel_plugin_get_data(widget);

    /* Left-click.  Show or hide the popup window. */
    if (event->button == 1)
    {
        if (vol->show_popup)
        {
            gtk_widget_hide(vol->popup_window);
            vol->show_popup = FALSE;
        }
        else
        {
            gtk_widget_show_all(vol->popup_window);
            vol->show_popup = TRUE;
        }
    }

    /* Middle-click.  Toggle the mute status. */
    else if (event->button == 2)
    {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vol->mute_check), ! gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vol->mute_check)));
    }
    return TRUE;
}

/* Handler for "focus-out" signal on popup window. */
static gboolean volumealsa_popup_focus_out(GtkWidget * widget, GdkEvent * event, VolumeALSAPlugin * vol)
{
    /* Hide the widget. */
    gtk_widget_hide(vol->popup_window);
    vol->show_popup = FALSE;
    return FALSE;
}

/* Handler for "map" signal on popup window. */
static void volumealsa_popup_map(GtkWidget * widget, VolumeALSAPlugin * vol)
{
    lxpanel_plugin_adjust_popup_position(widget, vol->plugin);
}

/* Handler for "value_changed" signal on popup window vertical scale. */
static void volumealsa_popup_scale_changed(GtkRange * range, VolumeALSAPlugin * vol)
{
    int level = gtk_range_get_value(GTK_RANGE(vol->volume_scale));
    gboolean mute = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vol->mute_check));

    /* Reflect the value of the control to the sound system. */
    asound_set_volume(vol, level);

    /*
     * Redraw the controls.
     * Scale and check button do not need to be updated, as these are always
     * in sync with user's actions.
     */
    volumealsa_update_current_icon(vol, mute, level);
}

/* Handler for "scroll-event" signal on popup window vertical scale. */
static void volumealsa_popup_scale_scrolled(GtkScale * scale, GdkEventScroll * evt, VolumeALSAPlugin * vol)
{
    /* Get the state of the vertical scale. */
    gdouble val = gtk_range_get_value(GTK_RANGE(vol->volume_scale));

    /* Dispatch on scroll direction to update the value. */
    if ((evt->direction == GDK_SCROLL_UP) || (evt->direction == GDK_SCROLL_LEFT))
        val += 2;
    else
        val -= 2;

    /* Reset the state of the vertical scale.  This provokes a "value_changed" event. */
    gtk_range_set_value(GTK_RANGE(vol->volume_scale), CLAMP((int)val, 0, 100));
}

/* Handler for "toggled" signal on popup window mute checkbox. */
static void volumealsa_popup_mute_toggled(GtkWidget * widget, VolumeALSAPlugin * vol)
{
    int level = gtk_range_get_value(GTK_RANGE(vol->volume_scale));
    gboolean mute = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vol->mute_check));

    /* Reflect the mute toggle to the sound system. */
    if (vol->master_element != NULL)
    {
        int chn;
        for (chn = 0; chn <= SND_MIXER_SCHN_LAST; chn++)
            snd_mixer_selem_set_playback_switch(vol->master_element, chn, ((mute) ? 0 : 1));
    }

    /*
     * Redraw the controls.
     * Scale and check button do not need to be updated, as these are always
     * in sync with user's actions.
     */
    volumealsa_update_current_icon(vol, mute, level);
}

/* Build the window that appears when the top level widget is clicked. */
static void volumealsa_build_popup_window(GtkWidget *p)
{
    VolumeALSAPlugin * vol = lxpanel_plugin_get_data(p);

    /* Create a new window. */
    vol->popup_window = gtk_window_new(GTK_WINDOW_POPUP);
    gtk_window_set_decorated(GTK_WINDOW(vol->popup_window), FALSE);
    gtk_container_set_border_width(GTK_CONTAINER(vol->popup_window), 5);
    gtk_window_set_default_size(GTK_WINDOW(vol->popup_window), 80, 140);
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(vol->popup_window), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(vol->popup_window), TRUE);
    gtk_window_set_type_hint(GTK_WINDOW(vol->popup_window), GDK_WINDOW_TYPE_HINT_UTILITY);

    /* Connect signals. */
    g_signal_connect(G_OBJECT(vol->popup_window), "focus-out-event", G_CALLBACK(volumealsa_popup_focus_out), vol);
    g_signal_connect(G_OBJECT(vol->popup_window), "map", G_CALLBACK(volumealsa_popup_map), vol);

    /* Create a scrolled window as the child of the top level window. */
    GtkWidget * scrolledwindow = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_set_border_width (GTK_CONTAINER(scrolledwindow), 0);
    gtk_widget_show(scrolledwindow);
    gtk_container_add(GTK_CONTAINER(vol->popup_window), scrolledwindow);
    gtk_widget_set_can_focus(scrolledwindow, FALSE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW (scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_NEVER);
    gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scrolledwindow), GTK_SHADOW_NONE);

    /* Create a viewport as the child of the scrolled window. */
    GtkWidget * viewport = gtk_viewport_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolledwindow), viewport);
    gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
    gtk_widget_show(viewport);

    /* Create a frame as the child of the viewport. */
    GtkWidget * frame = gtk_frame_new(_("Volume"));
    gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_IN);
    gtk_container_add(GTK_CONTAINER(viewport), frame);

    /* Create a vertical box as the child of the frame. */
    GtkWidget * box = gtk_vbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(frame), box);

    /* Create a vertical scale as the child of the vertical box. */
    vol->volume_scale = gtk_vscale_new(GTK_ADJUSTMENT(gtk_adjustment_new(100, 0, 100, 0, 0, 0)));
    gtk_scale_set_draw_value(GTK_SCALE(vol->volume_scale), FALSE);
    gtk_range_set_inverted(GTK_RANGE(vol->volume_scale), TRUE);
    gtk_box_pack_start(GTK_BOX(box), vol->volume_scale, TRUE, TRUE, 0);

    /* Value-changed and scroll-event signals. */
    vol->volume_scale_handler = g_signal_connect(vol->volume_scale, "value-changed", G_CALLBACK(volumealsa_popup_scale_changed), vol);
    g_signal_connect(vol->volume_scale, "scroll-event", G_CALLBACK(volumealsa_popup_scale_scrolled), vol);

    /* Create a check button as the child of the vertical box. */
    vol->mute_check = gtk_check_button_new_with_label(_("Mute"));
    gtk_box_pack_end(GTK_BOX(box), vol->mute_check, FALSE, FALSE, 0);
    vol->mute_check_handler = g_signal_connect(vol->mute_check, "toggled", G_CALLBACK(volumealsa_popup_mute_toggled), vol);

    /* Set background to default. */
    gtk_widget_set_style(viewport, panel_get_defstyle(vol->panel));
}

/* Plugin constructor. */
static GtkWidget *volumealsa_constructor(LXPanel *panel, config_setting_t *settings)
{
    /* Allocate and initialize plugin context and set into Plugin private data pointer. */
    VolumeALSAPlugin * vol = g_new0(VolumeALSAPlugin, 1);
    GtkWidget *p;

    /* Read config necessary for proper initialization of ALSA. */
    config_setting_lookup_int(settings, "UseAlsamixerVolumeMapping", &vol->alsamixer_mapping);

    /* Initialize ALSA.  If that fails, present nothing. */
    if ( ! asound_initialize(vol))
    {
        volumealsa_destructor(vol);
        return NULL;
    }

    /* Allocate top level widget and set into Plugin widget pointer. */
    vol->panel = panel;
    vol->plugin = p = gtk_event_box_new();
    vol->settings = settings;
    lxpanel_plugin_set_data(p, vol, volumealsa_destructor);
    gtk_widget_set_tooltip_text(p, _("Volume control"));

    /* Allocate icon as a child of top level. */
    vol->tray_icon = lxpanel_image_new_for_icon(panel, "audio-volume-muted-panel",
                                                -1, ICONS_MUTE);
    gtk_container_add(GTK_CONTAINER(p), vol->tray_icon);

    /* Initialize window to appear when icon clicked. */
    volumealsa_build_popup_window(p);

    /* Connect signals. */
    g_signal_connect(G_OBJECT(p), "scroll-event", G_CALLBACK(volumealsa_popup_scale_scrolled), vol );

    /* Update the display, show the widget, and return. */
    volumealsa_update_display(vol);
    volumealsa_update_current_icon(vol, asound_is_muted(vol), asound_get_volume(vol));
    gtk_widget_show_all(p);
    return p;
}

/* Plugin destructor. */
static void volumealsa_destructor(gpointer user_data)
{
    VolumeALSAPlugin * vol = (VolumeALSAPlugin *) user_data;

    asound_deinitialize(vol);

    /* If the dialog box is open, dismiss it. */
    if (vol->popup_window != NULL)
        gtk_widget_destroy(vol->popup_window);

    if (vol->restart_idle)
        g_source_remove(vol->restart_idle);

    /* Deallocate all memory. */
    g_free(vol);
}

/* Callback when the configuration dialog is to be shown. */

static GtkWidget *volumealsa_configure(LXPanel *panel, GtkWidget *p)
{
    VolumeALSAPlugin * vol = lxpanel_plugin_get_data(p);
    char *path = NULL;
    const gchar *command_line = NULL;
    GAppInfoCreateFlags flags = G_APP_INFO_CREATE_NONE;

    /* FIXME: configure settings! */
    /* check if command line was configured */
    config_setting_lookup_string(vol->settings, "MixerCommand", &command_line);
    /* FIXME: support "needs terminal" for MixerCommand */
    /* FIXME: selection for master channel! */
    /* FIXME: selection for the device */
    /* FIXME: configure buttons for each action (toggle volume/mixer/mute)! */
    /* FIXME: allow bind multimedia keys to volume using libkeybinder */

    /* if command isn't set in settings then let guess it */
    if (command_line == NULL && (path = g_find_program_in_path("pulseaudio")))
    {
        g_free(path);
     /* Assume that when pulseaudio is installed, it's launching every time */
        if ((path = g_find_program_in_path("gnome-sound-applet")))
        {
            command_line = "gnome-sound-applet";
        }
        else if ((path = g_find_program_in_path("pavucontrol")))
        {
            command_line = "pavucontrol";
        }
    }

    /* Fallback to alsamixer when PA is not running, or when no PA utility is find */
    if (command_line == NULL)
    {
        if ((path = g_find_program_in_path("gnome-alsamixer")))
        {
            command_line = "gnome-alsamixer";
        }
        else if ((path = g_find_program_in_path("alsamixergui")))
        {
            command_line = "alsamixergui";
        }
        else if ((path = g_find_program_in_path("alsamixer")))
        {
            command_line = "alsamixer";
            flags = G_APP_INFO_CREATE_NEEDS_TERMINAL;
        }
    }
    g_free(path);

    if (command_line)
    {
        fm_launch_command_simple(NULL, NULL, flags, command_line, NULL);
    }
    else
    {
        fm_show_error(NULL, NULL,
                      _("Error, you need to install an application to configure"
                        " the sound (pavucontrol, alsamixer ...)"));
    }

    return NULL;
}

/* Callback when panel configuration changes. */
static void volumealsa_panel_configuration_changed(LXPanel *panel, GtkWidget *p)
{
    /* Do a full redraw. */
    volumealsa_update_display(lxpanel_plugin_get_data(p));
}

FM_DEFINE_MODULE(lxpanel_gtk, volumealsa)

/* Plugin descriptor. */
LXPanelPluginInit fm_module_init_lxpanel_gtk = {
    .name = N_("Volume Control"),
    .description = N_("Display and control volume for ALSA"),

    .new_instance = volumealsa_constructor,
    .config = volumealsa_configure,
    .reconfigure = volumealsa_panel_configuration_changed,
    .button_press_event = volumealsa_button_press_event
};

/* vim: set sw=4 et sts=4 : */
