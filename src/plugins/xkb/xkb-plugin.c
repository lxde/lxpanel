/**
 * Copyright (c) 2010 LxDE Developers, see the file AUTHORS for details.
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

/* Originally derived from xfce4-xkb-plugin, Copyright 2004 Alexander Iliev,
 * which credits Michael Glickman. */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "xkb.h"

void panel_config_save(Panel * panel);   /* defined in configurator.c */

static void xkb_active_window_event(FbEv * ev, gpointer data);
static gboolean xkb_scroll_event(GtkWidget * widget, GdkEventScroll * event, gpointer data);
static gboolean xkb_button_press_event(GtkWidget * widget,  GdkEventButton * event, gpointer data);
static int xkb_constructor(Plugin * plugin, char ** fp);
static void xkb_destructor(Plugin * plugin);
static void xkb_display_type_changed(GtkComboBox * cb, gpointer * data);
static void xkb_enable_per_application_changed(GtkToggleButton * tb, gpointer * data);
static void xkb_default_language_changed(GtkComboBox * cb, gpointer * data);
static void xkb_configuration_response(GtkDialog * dialog, gint arg1, gpointer data);
static void xkb_configure(Plugin * p, GtkWindow * parent);
static void xkb_save_configuration(Plugin * p, FILE * fp);
static void xkb_panel_configuration_changed(Plugin * p);

/* Redraw the graphics. */
void xkb_redraw(XkbPlugin * xkb) 
{
    /* Set the image. */
    gboolean valid_image = FALSE;
    if (xkb->display_type == IMAGE)
    {
        int size = xkb->plugin->panel->icon_size;
        char * group_name = (char *) xkb_get_current_symbol_name_lowercase(xkb);
        if (group_name != NULL)
        {
            char * filename = g_strdup_printf("%s/%s.png", FLAGSDIR, group_name);
            GdkPixbuf * unscaled_pixbuf = gdk_pixbuf_new_from_file(filename, NULL);
            g_free(filename);
            g_free(group_name);

            if (unscaled_pixbuf != NULL)
            {
                /* Loaded successfully. */
                int width = gdk_pixbuf_get_width(unscaled_pixbuf);
                int height = gdk_pixbuf_get_height(unscaled_pixbuf);
                GdkPixbuf * pixbuf = gdk_pixbuf_scale_simple(unscaled_pixbuf, size * width / height, size, GDK_INTERP_BILINEAR);
                if (pixbuf != NULL)
                {
                    gtk_image_set_from_pixbuf(GTK_IMAGE(xkb->image), pixbuf);
                    g_object_unref(G_OBJECT(pixbuf));
                    gtk_widget_hide(xkb->label);
                    gtk_widget_show(xkb->image);
                    gtk_widget_set_tooltip_text(xkb->btn, xkb_get_current_group_name(xkb));
                    valid_image = TRUE;
                }
                g_object_unref(unscaled_pixbuf);
            }
        }
    }

    /* Set the label. */
    if ((xkb->display_type == TEXT) || ( ! valid_image))
    {
        char * group_name = (char *) xkb_get_current_symbol_name(xkb);
        if (group_name != NULL)
        {
            panel_draw_label_text(xkb->plugin->panel, xkb->label, (char *) group_name, TRUE, TRUE);
            gtk_widget_hide(xkb->image);
            gtk_widget_show(xkb->label);
            gtk_widget_set_tooltip_text(xkb->btn, xkb_get_current_group_name(xkb));
        }
    }
}

/* Handler for "active_window" event on root window listener. */
static void xkb_active_window_event(FbEv * ev, gpointer data) 
{
    XkbPlugin * xkb = (XkbPlugin *) data;
    if (xkb->enable_perapp)
    {
        Window * win = fb_ev_active_window(ev);
        if (*win != None)
        {
            xkb_active_window_changed(xkb, *win);
            xkb_redraw(xkb);
        }
    }
}

/* Handler for "scroll-event" on drawing area. */
static gboolean xkb_scroll_event(GtkWidget * widget, GdkEventScroll * event, gpointer data)
{
    XkbPlugin * xkb = (XkbPlugin *) data;

    /* Change to next or previous group. */
    xkb_change_group(xkb,
        (((event->direction == GDK_SCROLL_UP) || (event->direction == GDK_SCROLL_RIGHT)) ? 1 : -1));
    return TRUE;
}

/* Handler for button-press-event on top level widget. */
static gboolean xkb_button_press_event(GtkWidget * widget,  GdkEventButton * event, gpointer data) 
{
    XkbPlugin * xkb = (XkbPlugin *) data;

    /* Standard right-click handling. */
    if (plugin_button_press_event(widget, event, xkb->plugin))
        return TRUE;

    /* Change to next group. */
    xkb_change_group(xkb, 1);
    return TRUE;
}

/* Plugin constructor. */
static int xkb_constructor(Plugin * plugin, char ** fp)
{
    /* Allocate plugin context and set into Plugin private data pointer. */
    XkbPlugin * xkb = g_new0(XkbPlugin, 1);
    xkb->plugin = plugin;
    plugin->priv = xkb;

    /* Initialize to defaults. */
    xkb->display_type = IMAGE;
    xkb->enable_perapp = TRUE;
    xkb->default_group = 0;

    /* Load parameters from the configuration file. */
    line s;
    s.len = 256;
    if (fp != NULL)
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END)
        {
            if (s.type == LINE_NONE)
            {
                ERR( "xkb: illegal token %s\n", s.str);
                return 0;
            }
            if (s.type == LINE_VAR)
            {
                if (g_ascii_strcasecmp(s.t[0], "DisplayType") == 0)
                    xkb->display_type = atoi(s.t[1]);
                else if (g_ascii_strcasecmp(s.t[0], "PerAppLayout") == 0)
                    xkb->enable_perapp = str2num(bool_pair, s.t[1], 0);
                else if (g_ascii_strcasecmp(s.t[0], "DefaultGroup") == 0)
                    xkb->default_group = atoi(s.t[1]);
                else
                    ERR( "xkb: unknown var %s\n", s.t[0]);
            }
            else
            {
                ERR( "xkb: illegal in this context %s\n", s.str);
                return 0;
            }
        }
    }

    /* Allocate top level widget and set into Plugin widget pointer. */
    plugin->pwid = gtk_event_box_new();
    gtk_widget_add_events(plugin->pwid, GDK_BUTTON_PRESS_MASK);

    /* Create a button as the child of the event box. */
    xkb->btn = gtk_button_new();
    gtk_container_add(GTK_CONTAINER(plugin->pwid), xkb->btn);
    gtk_button_set_relief(GTK_BUTTON(xkb->btn), GTK_RELIEF_NONE);
    GTK_WIDGET_UNSET_FLAGS(xkb->btn, GTK_CAN_FOCUS);
    GTK_WIDGET_UNSET_FLAGS(xkb->btn, GTK_CAN_DEFAULT);
    gtk_widget_show(xkb->btn);

    /* Create a horizontal box as the child of the button. */
    GtkWidget * hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(xkb->btn), hbox);
    gtk_widget_show(hbox);

    /* Create a label and an image as children of the horizontal box.
     * Only one of these is visible at a time, controlled by user preference
     * and the successful loading of the image. */
    xkb->label = gtk_label_new("");
    gtk_container_add(GTK_CONTAINER(hbox), xkb->label);
    xkb->image = gtk_image_new();
    gtk_container_add(GTK_CONTAINER(hbox), xkb->image);

    /* Initialize the XKB interface. */
    xkb_mechanism_constructor(xkb);

    /* Connect signals. */
    g_signal_connect(xkb->btn, "button-press-event", G_CALLBACK(xkb_button_press_event), xkb);
    g_signal_connect(xkb->btn, "scroll-event", G_CALLBACK(xkb_scroll_event), xkb);
    g_signal_connect(G_OBJECT(fbev), "active_window", G_CALLBACK(xkb_active_window_event), xkb);

    /* Show the widget and return. */
    xkb_redraw(xkb);
    gtk_widget_show(plugin->pwid);
    return 1;
}

/* Plugin destructor. */
static void xkb_destructor(Plugin * plugin)
{
    XkbPlugin * xkb = (XkbPlugin *) plugin->priv;

    /* Disconnect root window event handler. */
    g_signal_handlers_disconnect_by_func(G_OBJECT(fbev), xkb_active_window_event, xkb);

    /* Disconnect from the XKB mechanism. */
    g_source_remove(xkb->source_id);
    xkb_mechanism_destructor(xkb);

    /* Ensure that the configuration dialog is dismissed. */
    if (xkb->config_dlg != NULL)
        gtk_widget_destroy(xkb->config_dlg);

    /* Deallocate all memory. */
    g_free(xkb);
}

/* Handler for "changed" event on default language combo box of configuration dialog. */
static void xkb_display_type_changed(GtkComboBox * cb, gpointer * data) 
{
    /* Fetch the new value and redraw. */
    XkbPlugin * xkb = (XkbPlugin *) data;
    xkb->display_type = gtk_combo_box_get_active(cb);
    xkb_redraw(xkb);
}

/* Handler for "toggled" event on per-application check box of configuration dialog. */
static void xkb_enable_per_application_changed(GtkToggleButton * tb, gpointer * data) 
{
    /* Fetch the new value and redraw. */
    XkbPlugin * xkb = (XkbPlugin *) data;
    xkb->enable_perapp = gtk_toggle_button_get_active(tb);
    gtk_widget_set_sensitive(xkb->per_app_default_layout_menu, xkb->enable_perapp);
    xkb_redraw(xkb);
}

/* Handler for "changed" event on default language combo box of configuration dialog. */
static void xkb_default_language_changed(GtkComboBox * cb, gpointer * data)
{
    /* Fetch the new value and redraw. */
    XkbPlugin * xkb = (XkbPlugin *) data;
    xkb->default_group = gtk_combo_box_get_active(cb);
    xkb_redraw(xkb);
}

/* Handler for "response" event on configuration dialog. */
static void xkb_configuration_response(GtkDialog * dialog, int response, gpointer data)
{
    XkbPlugin * xkb = (XkbPlugin *) data;

    /* Save the new configuration and redraw the plugin. */
    panel_config_save(xkb->plugin->panel);
    xkb_redraw(xkb);

    /* Destroy the dialog. */
    gtk_widget_destroy(xkb->config_dlg);
    xkb->config_dlg = NULL;
}

/* Callback when the configuration dialog is to be shown. */
static void xkb_configure(Plugin * p, GtkWindow * parent)
{
    XkbPlugin * xkb = (XkbPlugin *) p->priv;

    /* Create dialog window. */
    GtkWidget * dlg = gtk_dialog_new_with_buttons(
        _("Configure Keyboard Layout Switcher"), 
        NULL,
        GTK_DIALOG_NO_SEPARATOR,
        GTK_STOCK_CLOSE, 
        GTK_RESPONSE_OK,
        NULL);
    xkb->config_dlg = dlg;
    panel_apply_icon(GTK_WINDOW(dlg));

    /* Create a vertical box as the child of the dialog. */
    GtkWidget * vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dlg)->vbox), vbox);

    /* Create a frame as the child of the vertical box. */
    GtkWidget * display_type_frame = gtk_frame_new(NULL);
    gtk_frame_set_label(GTK_FRAME(display_type_frame), _("Show layout as"));
    gtk_box_pack_start(GTK_BOX(vbox), display_type_frame, TRUE, TRUE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(display_type_frame), 5);

    /* Create an alignment as the child of the frame. */
    GtkWidget * alignment2 = gtk_alignment_new(0.5, 0.5, 1, 1);
    gtk_container_add(GTK_CONTAINER(display_type_frame), alignment2);
    gtk_alignment_set_padding(GTK_ALIGNMENT(alignment2), 4, 4, 10, 10);
  
    /* Create a horizontal box as the child of the alignment. */
    GtkWidget * hbox = gtk_hbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(alignment2), hbox);

    /* Create a combo box as the child of the horizontal box. */
    GtkWidget * display_type_optmenu = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(display_type_optmenu), _("image"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(display_type_optmenu), _("text"));
    gtk_box_pack_start(GTK_BOX(hbox), display_type_optmenu, TRUE, TRUE, 2);
    g_signal_connect(display_type_optmenu, "changed", G_CALLBACK(xkb_display_type_changed), xkb);
    gtk_combo_box_set_active(GTK_COMBO_BOX(display_type_optmenu), xkb->display_type);

    /* Create a frame as the child of the vertical box. */
    GtkWidget * per_app_frame = gtk_frame_new(NULL);
    gtk_frame_set_label(GTK_FRAME(per_app_frame), _("Per application settings"));
    gtk_widget_show(per_app_frame);
    gtk_box_pack_start(GTK_BOX(vbox), per_app_frame, TRUE, TRUE, 2);
    gtk_container_set_border_width(GTK_CONTAINER(per_app_frame), 5);

    /* Create an alignment as the child of the frame. */
    GtkWidget * alignment1 = gtk_alignment_new(0.5, 0.5, 1, 1);
    gtk_container_add(GTK_CONTAINER(per_app_frame), alignment1);
    gtk_alignment_set_padding(GTK_ALIGNMENT(alignment1), 4, 4, 10, 10);

    /* Create a vertical box as the child of the alignment. */
    GtkWidget * per_app_vbox = gtk_vbox_new(FALSE, 2);
    gtk_container_add(GTK_CONTAINER(alignment1), per_app_vbox);

    /* Create a check button as the child of the vertical box. */
    GtkWidget * per_app_checkbutton = gtk_check_button_new_with_mnemonic(_("_Remember layout for each application"));
    gtk_box_pack_start(GTK_BOX(per_app_vbox), per_app_checkbutton, FALSE, FALSE, 2);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(per_app_checkbutton), xkb->enable_perapp);
    g_signal_connect(per_app_checkbutton, "toggled", G_CALLBACK(xkb_enable_per_application_changed), xkb);

    /* Create a horizontal box as the child of the vertical box. */
    GtkWidget * hbox3 = gtk_hbox_new(FALSE, 2);
    gtk_box_pack_start(GTK_BOX(per_app_vbox), hbox3, TRUE, TRUE, 2);

    /* Create a label as the child of the horizontal box. */
    GtkWidget * label4 = gtk_label_new(_("Default layout:"));
    gtk_box_pack_start(GTK_BOX(hbox3), label4, FALSE, FALSE, 2);

    /* Create a combo box as the child of the horizontal box. */
    xkb->per_app_default_layout_menu = gtk_combo_box_new_text();
    gtk_box_pack_start(GTK_BOX(hbox3), xkb->per_app_default_layout_menu, FALSE, TRUE, 2);
    gtk_widget_set_sensitive(xkb->per_app_default_layout_menu, xkb->enable_perapp);

    /* Populate the combo box with the available choices. */
    int i;
    for (i = 0; i < xkb_get_group_count(xkb); i++) 
    {
        gtk_combo_box_append_text(
            GTK_COMBO_BOX(xkb->per_app_default_layout_menu), 
            xkb_get_symbol_name_by_res_no(xkb, i));
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(xkb->per_app_default_layout_menu), xkb->default_group);
    g_signal_connect(xkb->per_app_default_layout_menu, "changed", G_CALLBACK(xkb_default_language_changed), xkb);

    /* Connect signals. */
    g_signal_connect(xkb->config_dlg, "response", G_CALLBACK(xkb_configuration_response), xkb);

    /* Display the dialog. */
    gtk_widget_set_size_request(GTK_WIDGET(xkb->config_dlg), 400, -1);	/* Improve geometry */
    gtk_widget_show_all(xkb->config_dlg);
    gtk_window_present(GTK_WINDOW(xkb->config_dlg));
}

/* Callback when the configuration is to be saved. */
static void xkb_save_configuration(Plugin * p, FILE * fp)
{
    XkbPlugin * xkb = (XkbPlugin *) p->priv;
    lxpanel_put_int(fp, "DisplayType", xkb->display_type);
    lxpanel_put_int(fp, "PerAppLayout", xkb->enable_perapp);
    lxpanel_put_int(fp, "DefaultGroup", xkb->default_group);
}

/* Callback when panel configuration changes. */
static void xkb_panel_configuration_changed(Plugin * p)
{
    /* Do a full redraw. */
    XkbPlugin * xkb = (XkbPlugin *) p->priv;
    xkb_redraw(xkb);
}

/* Plugin descriptor. */
PluginClass xkb_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "xkb",
    name : N_("Keyboard Layout Switcher"),
    version: "1.0",
    description : N_("Switch between available keyboard layouts"),

    constructor : xkb_constructor,
    destructor  : xkb_destructor,
    config : xkb_configure,
    save : xkb_save_configuration,
    panel_configuration_changed : xkb_panel_configuration_changed

};
