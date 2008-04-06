/*
//====================================================================
//  xfce4-xkb-plugin - XFCE4 Xkb Layout Indicator panel plugin
// -------------------------------------------------------------------
//  Alexander Iliev <sasoiliev@mamul.org>
//  20-Feb-04
// -------------------------------------------------------------------
//  Parts of this code belong to Michael Glickman <wmalms@yahooo.com>
//  and his program wmxkb.
//  WARNING: DO NOT BOTHER Michael Glickman WITH QUESTIONS ABOUT THIS
//           PROGRAM!!! SEND INSTEAD EMAILS TO <sasoiliev@mamul.org>
//====================================================================
*/

/* Modified by Hong Jen Yee (PCMan) <pcman.tw@gmail.com> on 2008-04-06 for lxpanel */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n.h>

#include <stdio.h>
#include <ctype.h>

#include "xkb.h"

static t_xkb_options_dlg *dlg = NULL;
static GIOChannel *channel;
static guint source_id;

static gulong win_change_hanler, win_close_hanler;

static void
xkb_free(t_xkb *xkb);

static void
xkb_save_config(t_xkb *xkb, gchar *filename);

static t_xkb_options_dlg*
xkb_options_dlg_create();

static void
free_xkb_options_dlg(GtkDialog *dialog, gint arg1, gpointer user_data);

static void
xkb_options_dlg_set_xkb(t_xkb_options_dlg *dlg, t_xkb *xkb);

static void 
change_group(GtkButton *btn, gpointer data) ;

static void 
active_window_changed(FbEv *ev, gpointer data) ;

static void 
application_closed( FbEv* ev, Window win, gpointer data) ;

static void 
xkb_refresh_gui(t_xkb *data) ;

/* ------------------------------------------------------------------ *
 *                     Panel Plugin Interface                         *
 * ------------------------------------------------------------------ */

static void 
xkb_constructor (Plugin *plugin, char** fp);

static void 
xkb_destructor (Plugin *plugin);

static void 
xkb_orientation_changed(Plugin *plugin,
                             GtkOrientation orientation,
                             t_xkb *xkb);

static gboolean 
xkb_set_size(Plugin *plugin,gint size,
                  t_xkb *xkb);

static void 
xkb_free_data(Plugin *plugin,t_xkb *xkb);

static void 
xkb_save(Plugin *plugin, t_xkb *xkb);

static void 
xkb_configure(Plugin *plugin, t_xkb *xkb);

static void 
xkb_about(Plugin *plugin, t_xkb *xkb);

/* create widgets and connect to signals */
static void 
xkb_constructor (Plugin *plugin, char** fp)
{
  t_xkb *xkb;
  line s;

  const char *initial_group;

  xkb = g_new(t_xkb, 1);

  xkb->display_type = IMAGE;
  xkb->enable_perapp = TRUE;
  xkb->default_group = 0;

    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "xkb: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "DisplayType"))
                    xkb->display_type = atoi(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "PerAppLayout"))
                    xkb->enable_perapp = str2num(bool_pair, s.t[1], 0);
                else if (!g_ascii_strcasecmp(s.t[0], "DefaultGroup"))
                    xkb->default_group = atoi(s.t[1]);
                else {
                    ERR( "xkb: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "xkb: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }

  xkb->size = plugin->panel->height;

  xkb->btn = gtk_button_new();
  gtk_button_set_relief(GTK_BUTTON(xkb->btn), GTK_RELIEF_NONE);
  // gtk_container_add(GTK_CONTAINER(xkb->plugin->pwid), xkb->btn);

  gtk_widget_show(xkb->btn);
  g_signal_connect(xkb->btn, "clicked", G_CALLBACK(change_group), xkb);

  xkb->vbox = gtk_vbox_new(FALSE, 0);
  gtk_container_add(GTK_CONTAINER(xkb->btn), xkb->vbox);

  xkb->label = gtk_label_new("");
  gtk_label_set_use_markup (GTK_LABEL (xkb->label), TRUE);
  gtk_container_add(GTK_CONTAINER(xkb->vbox), xkb->label);
  xkb->image = gtk_image_new();
  gtk_container_add(GTK_CONTAINER(xkb->vbox), xkb->image);

  gtk_widget_show(xkb->vbox);

  initial_group = initialize_xkb(xkb);

  xkb_refresh_gui(xkb);

  channel = g_io_channel_unix_new(get_connection_number());
  source_id = g_io_add_watch(channel, G_IO_IN | G_IO_PRI, 
      (GIOFunc) &gio_callback, (gpointer) xkb);

  /* track signals about window change */
  win_change_hanler = g_signal_connect( G_OBJECT (fbev), 
      "active_window", G_CALLBACK(active_window_changed), xkb);

  win_close_hanler = g_signal_connect( G_OBJECT (fbev), 
      "destroy_window", G_CALLBACK(application_closed), xkb);

  gtk_widget_show_all(xkb->btn);

  xkb->plugin = plugin;
  plugin->priv = xkb;
  plugin->pwid = xkb->btn;
  
    RET(1);
error:
	xkb_destructor( plugin );
    RET(0);
}

static void 
xkb_destructor (Plugin *plugin)
{
  t_xkb *xkb = (t_xkb*)plugin->priv;
  xkb_free( xkb );
}

gboolean 
xkb_set_size(Plugin *plugin, gint size,
                  t_xkb *xkb)
{
  GtkOrientation orientation;
  DBG ("setting size %d", size);
  xkb->size = size;
/*
  orientation = xfce_panel_plugin_get_orientation (plugin);
  if (orientation == GTK_ORIENTATION_HORIZONTAL)
    gtk_widget_set_size_request(xkb->btn, -1, xkb->size);
  else
    gtk_widget_set_size_request(xkb->btn, xkb->size, -1);
  */
  set_new_locale(xkb);
  return TRUE;
}

void 
xkb_free_data(Plugin *plugin, t_xkb *xkb)
{
  xkb_free(xkb);
}

void xkb_save(Plugin *plugin, t_xkb *xkb)
{
  gchar *filename;
//  filename = xfce_panel_plugin_save_location(plugin, TRUE);
  if (filename)
  {
    xkb_save_config(xkb, filename);
    g_free(filename);
  }
}

void 
xkb_configure(Plugin *plugin, t_xkb *xkb)
{
//  xfce_panel_plugin_block_menu(plugin); 
  dlg = xkb_options_dlg_create();
  xkb_options_dlg_set_xkb(dlg, xkb);
  gtk_dialog_run(GTK_DIALOG(dlg->dialog));
  //xfce_panel_plugin_unblock_menu(plugin);
}

void 
xkb_about(Plugin *plugin, t_xkb *xkb)
{
  GtkWidget *about;
  const gchar* authors[2] = {
    "Alexander Iliev <sasoiliev@mamul.org>", 
    NULL
  };
  about = gtk_about_dialog_new();
  gtk_about_dialog_set_name(GTK_ABOUT_DIALOG(about), 
      _("Keyboard Layout Switcher"));
  gtk_about_dialog_set_logo(GTK_ABOUT_DIALOG(about), 
      NULL);
  gtk_about_dialog_set_authors(GTK_ABOUT_DIALOG(about), 
      (const gchar**) authors);
  gtk_about_dialog_set_comments(GTK_ABOUT_DIALOG(about), 
      _("Allows you to switch the keyboard layout and\n"
        "displays the currently selected layout."));
  gtk_about_dialog_set_website(GTK_ABOUT_DIALOG(about), 
      "http://xfce-goodies.berlios.de");
  gtk_about_dialog_set_website_label(GTK_ABOUT_DIALOG(about), 
      _("Other plugins available here"));
  gtk_dialog_run(GTK_DIALOG(about));
  gtk_widget_destroy (about); 
}

/* ----------------- xkb plugin stuff -----------------------*/

static void
xkb_save_config(t_xkb *xkb, gchar *filename)
{
/*
  XfceRc* rcfile = xfce_rc_simple_open(filename, FALSE);
  if (!rcfile) 
  {
    return;
  }

  xfce_rc_set_group(rcfile, NULL);

  xfce_rc_write_int_entry(rcfile, "display_type", 
      xkb->display_type);
  xfce_rc_write_int_entry(rcfile, "per_app_layout", 
      (xkb->enable_perapp) ? 1 : 0);
  xfce_rc_write_int_entry(rcfile, "default_group", 
      xkb->default_group);

  xfce_rc_close(rcfile);
*/
}

void 
active_window_changed(FbEv *ev, gpointer data) 
{
	Window *win = fb_ev_active_window( ev );
	DBG( "win = %p, pid=%d", win, get_net_wm_pid(win) );
	if( win == None )
		return;
  react_active_window_changed(get_net_wm_pid( win ), (t_xkb *) data);
}

void 
application_closed( FbEv* ev, Window win, gpointer data) 
{
	DBG("win: %p", win);
  react_application_closed(get_net_wm_pid(win));  
}

void 
change_group(GtkButton *btn, gpointer data) 
{
  do_change_group(1, (t_xkb *) data);
}

void 
xkb_refresh_gui(t_xkb *data) 
{
  t_xkb *plugin = (t_xkb *) data;

  switch (plugin->display_type) 
  {
    case TEXT:
      gtk_widget_hide(plugin->image);
      gtk_widget_show(plugin->label);
      break;
    case IMAGE:
      if (is_current_group_flag_available()) 
      {
        gtk_widget_hide(plugin->label);
        gtk_widget_show(plugin->image);
      }
      break;
    default: break;
  }

  /* Part of the image may remain visible after display type change */
  gtk_widget_queue_draw_area(plugin->btn, 0, 0, 
      plugin->size, plugin->size);
}

static void 
xkb_free(t_xkb *xkb) 
{
  g_signal_handler_disconnect(fbev, win_change_hanler);
//  g_signal_handler_disconnect(netk_screen, win_close_hanler);

  g_source_remove(source_id);
  deinitialize_xkb();

  g_return_if_fail(xkb != NULL);

  gtk_widget_destroy(xkb->btn);

  g_free(xkb);
}

/* ----------------- xkb options dialog callbacks -----------------------*/

static void 
xkb_display_type_changed(GtkComboBox *cb, gpointer *data) 
{
  t_xkb *xkb = (t_xkb *) data;
  xkb->display_type = gtk_combo_box_get_active(cb);
  xkb_refresh_gui(xkb);
}

static void 
xkb_enable_perapp_changed(GtkToggleButton *tb, gpointer *data) 
{
  t_xkb_options_dlg *dlg = (t_xkb_options_dlg *) data;
  dlg->xkb->enable_perapp = gtk_toggle_button_get_active(tb);
  gtk_widget_set_sensitive(dlg->per_app_default_layout_menu, 
      dlg->xkb->enable_perapp);
}

static void 
xkb_def_lang_changed(GtkComboBox *cb, gpointer *data) 
{
  t_xkb *xkb = (t_xkb *) data;
  xkb->default_group = gtk_combo_box_get_active(cb);
}

/* ----------------- xkb options dialog -----------------------*/

static t_xkb_options_dlg*
xkb_options_dlg_create()
{
  int x;
  GtkWidget *vbox, *hbox, *display_type_frame,
            *per_app_frame, *alignment1, *alignment2, *hbox3, *label4;

  dlg = g_new0(t_xkb_options_dlg, 1);

  dlg->dialog = gtk_dialog_new_with_buttons (
      _("Configure Keyboard Layout Switcher"), 
      NULL,
      GTK_DIALOG_NO_SEPARATOR,
      GTK_STOCK_CLOSE, 
      GTK_RESPONSE_OK,
      NULL
  );
 
  vbox = gtk_vbox_new(FALSE, 2);
  gtk_widget_show(vbox);
  gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dlg->dialog)->vbox), vbox);

  display_type_frame = gtk_frame_new (NULL);
  gtk_frame_set_label (GTK_FRAME (display_type_frame), _("Show layout as"));
  gtk_widget_show (display_type_frame);
  gtk_box_pack_start (GTK_BOX (vbox), display_type_frame, TRUE, TRUE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (display_type_frame), 5);

  alignment2 = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_widget_show (alignment2);
  gtk_container_add (GTK_CONTAINER (display_type_frame), alignment2);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment2), 4, 4, 10, 10);
  
  hbox = gtk_hbox_new(FALSE, 2);
  gtk_widget_show(hbox);
  gtk_container_add (GTK_CONTAINER (alignment2), hbox);

  dlg->display_type_optmenu = gtk_combo_box_new_text ();
  gtk_combo_box_append_text (GTK_COMBO_BOX (dlg->display_type_optmenu), _("image"));
  gtk_combo_box_append_text (GTK_COMBO_BOX (dlg->display_type_optmenu), _("text"));
  gtk_box_pack_start(GTK_BOX(hbox), dlg->display_type_optmenu, TRUE, TRUE, 2);

  per_app_frame = gtk_frame_new (NULL);
  gtk_frame_set_label (GTK_FRAME (per_app_frame), _("Per application settings"));
  gtk_widget_show (per_app_frame);
  gtk_box_pack_start (GTK_BOX (vbox), per_app_frame, TRUE, TRUE, 2);
  gtk_container_set_border_width (GTK_CONTAINER (per_app_frame), 5);

  alignment1 = gtk_alignment_new (0.5, 0.5, 1, 1);
  gtk_widget_show (alignment1);
  gtk_container_add (GTK_CONTAINER (per_app_frame), alignment1);
  gtk_alignment_set_padding (GTK_ALIGNMENT (alignment1), 4, 4, 10, 10);

  dlg->per_app_vbox = gtk_vbox_new (FALSE, 2);
  gtk_widget_show (dlg->per_app_vbox);
  gtk_container_add (GTK_CONTAINER (alignment1), dlg->per_app_vbox);

  dlg->per_app_checkbutton = gtk_check_button_new_with_mnemonic(_("_Remember layout for each application"));
  gtk_widget_show (dlg->per_app_checkbutton);
  gtk_box_pack_start (GTK_BOX (dlg->per_app_vbox), dlg->per_app_checkbutton, FALSE, FALSE, 2);
  gtk_toggle_button_set_active((GtkToggleButton*) dlg->per_app_checkbutton, TRUE);

  hbox3 = gtk_hbox_new (FALSE, 2);
  gtk_widget_show (hbox3);
  gtk_box_pack_start (GTK_BOX (dlg->per_app_vbox), hbox3, TRUE, TRUE, 2);

  label4 = gtk_label_new (_("Default layout:"));
  gtk_widget_show (label4);
  gtk_box_pack_start (GTK_BOX (hbox3), label4, FALSE, FALSE, 2);

  dlg->per_app_default_layout_menu = gtk_combo_box_new_text ();
  gtk_widget_show (dlg->per_app_default_layout_menu);
  gtk_box_pack_start (GTK_BOX (hbox3), dlg->per_app_default_layout_menu, FALSE, TRUE, 2);
  
  for (x = 0; x < get_group_count(); x++) 
  {
    gtk_combo_box_append_text(
        GTK_COMBO_BOX(dlg->per_app_default_layout_menu), 
        get_symbol_name_by_res_no(x));
  }



  gtk_widget_show_all(vbox);
  
  g_signal_connect_swapped( (gpointer)dlg->dialog, "response",
                            G_CALLBACK (free_xkb_options_dlg), NULL);
  
  return dlg;
}

void
free_xkb_options_dlg(GtkDialog *dialog, gint arg1, gpointer user_data)
{
  DBG("destroy options dialog\n");
  gtk_widget_hide(dlg->dialog);
  gtk_widget_destroy(dlg->dialog);
  
  g_free(dlg);
  dlg = NULL;
}

static void
xkb_options_dlg_set_xkb(t_xkb_options_dlg *dlg, t_xkb *xkb)
{
  dlg->xkb = xkb;

  gtk_combo_box_set_active(GTK_COMBO_BOX(dlg->display_type_optmenu), xkb->display_type);

  gtk_combo_box_set_active((GtkComboBox*)dlg->per_app_default_layout_menu, xkb->default_group);

  gtk_toggle_button_set_active((GtkToggleButton*)dlg->per_app_checkbutton, xkb->enable_perapp);

  g_signal_connect(dlg->display_type_optmenu, "changed", G_CALLBACK(xkb_display_type_changed), xkb);
  g_signal_connect(dlg->per_app_checkbutton, "toggled", G_CALLBACK(xkb_enable_perapp_changed), dlg);
  g_signal_connect(dlg->per_app_default_layout_menu, "changed", G_CALLBACK(xkb_def_lang_changed), xkb);
}


PluginClass xkb_plugin_class = {
    fname: NULL,
    count: 0,

    type : "xkb",
    name : N_("Kayboard Layout switcher"),
    version: "1.0",
    description : N_("Switch between available keyboard layouts"),

    constructor : xkb_constructor,
    destructor  : xkb_destructor,
//    config : xkb_config,
//    save : save_config,
};
