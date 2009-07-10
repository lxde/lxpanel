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
xkb_configure(Plugin * p, GtkWindow * parent);

static t_xkb_options_dlg*
xkb_options_dlg_create(t_xkb * xkb);

static void
free_xkb_options_dlg(GtkDialog *dialog, gint arg1, gpointer user_data);

static void
xkb_options_dlg_set_xkb(t_xkb_options_dlg *dlg, t_xkb *xkb);

static gboolean 
change_group(GtkWidget * widget, GdkEventButton * event, gpointer data) ;

static void 
active_window_changed(FbEv *ev, gpointer data) ;

static void 
application_closed( FbEv* ev, Window win, gpointer data) ;

static void 
xkb_refresh_gui(t_xkb * plugin);

/* ------------------------------------------------------------------ *
 *                     Panel Plugin Interface                         *
 * ------------------------------------------------------------------ */

static int
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
xkb_about(Plugin *plugin, t_xkb *xkb);

/* create widgets and connect to signals */
static int
xkb_constructor (Plugin *plugin, char** fp)
{
  t_xkb *xkb;
  line s;

  const char *initial_group;

  xkb = g_new0(t_xkb, 1);
  xkb->plugin = plugin;
  plugin->priv = xkb;

  xkb->size = plugin->panel->icon_size;
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
                }
            } else {
                ERR( "xkb: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }


  plugin->pwid = gtk_event_box_new();
  gtk_widget_add_events(plugin->pwid, GDK_BUTTON_PRESS_MASK);
  xkb->btn = gtk_button_new();
  gtk_container_add(GTK_CONTAINER(plugin->pwid), xkb->btn);
  gtk_button_set_relief(GTK_BUTTON(xkb->btn), GTK_RELIEF_NONE);
  GTK_WIDGET_UNSET_FLAGS(xkb->btn, GTK_CAN_FOCUS);
  GTK_WIDGET_UNSET_FLAGS(xkb->btn, GTK_CAN_DEFAULT);

  gtk_widget_show(xkb->btn);
  g_signal_connect(xkb->btn, "button-press-event", G_CALLBACK(change_group), xkb);

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
  set_new_locale(xkb);

  channel = g_io_channel_unix_new(get_connection_number());
  source_id = g_io_add_watch(channel, G_IO_IN | G_IO_PRI, 
      (GIOFunc) &gio_callback, (gpointer) xkb);

  /* track signals about window change */
  win_change_hanler = g_signal_connect( G_OBJECT (fbev), 
      "active_window", G_CALLBACK(active_window_changed), xkb);

//  win_close_hanler = g_signal_connect( G_OBJECT (fbev), 
//      "destroy_window", G_CALLBACK(application_closed), xkb);

  gtk_widget_show(plugin->pwid);

  
  
    RET(1);
error:
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
//  GtkOrientation orientation;
//  DBG ("setting size %d", size);
//  xkb->size = size;
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

/* Callback when the configuration dialog is to be shown. */
static void xkb_configure(Plugin * p, GtkWindow * parent)
{
    t_xkb * xkb = (t_xkb *) p->priv; 
    dlg = xkb_options_dlg_create(xkb);
    xkb_options_dlg_set_xkb(dlg, xkb);
    gtk_widget_set_size_request(GTK_WIDGET(dlg->dialog), 400, -1);	/* Improve geometry */
    gtk_dialog_run(GTK_DIALOG(dlg->dialog));
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

void 
active_window_changed(FbEv *ev, gpointer data) 
{
	Window * win = fb_ev_active_window( ev );
	DBG( "win = %p, pid=%d", win, get_net_wm_pid(win) );
	if( *win == None )
		return;
  react_active_window_changed(get_net_wm_pid( *win ), (t_xkb *) data);
}

void 
application_closed( FbEv* ev, Window win, gpointer data) 
{
	DBG("win: %p", win);
  react_application_closed(get_net_wm_pid(win));  
}

static gboolean 
change_group(GtkWidget *widget,  GdkEventButton * event, gpointer data) 
{
    t_xkb * xkb = (t_xkb *) data;

    /* Standard right-click handling. */
    if (plugin_button_press_event(widget, event, xkb->plugin))
        return TRUE;

  do_change_group(1, xkb);
}

void xkb_refresh_gui(t_xkb * plugin) 
{
    if ((plugin->display_type == TEXT) || ( ! is_current_group_flag_available())) 
    {
        gtk_widget_hide(plugin->image);
        gtk_widget_show(plugin->label);
    }
    else
    {
        gtk_widget_hide(plugin->label);
        gtk_widget_show(plugin->image);
    }
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
  set_new_locale(xkb);
}

static void 
xkb_enable_perapp_changed(GtkToggleButton *tb, gpointer *data) 
{
  t_xkb_options_dlg *dlg = (t_xkb_options_dlg *) data;
  dlg->xkb->enable_perapp = gtk_toggle_button_get_active(tb);
  gtk_widget_set_sensitive(dlg->per_app_default_layout_menu, 
      dlg->xkb->enable_perapp);
  set_new_locale(dlg->xkb);
}

static void 
xkb_def_lang_changed(GtkComboBox *cb, gpointer *data) 
{
  t_xkb *xkb = (t_xkb *) data;
  xkb->default_group = gtk_combo_box_get_active(cb);
  set_new_locale(xkb);
}

/* ----------------- xkb options dialog -----------------------*/

static t_xkb_options_dlg*
xkb_options_dlg_create(t_xkb * xkb)
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
  panel_apply_icon(GTK_WINDOW(dlg->dialog));

 
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

  panel_config_save(dlg->xkb->plugin->panel);
  set_new_locale(dlg->xkb);
  
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

/* Callback when the configuration is to be saved. */
static void xkb_save_configuration(Plugin * p, FILE * fp)
{
    t_xkb * xkb = (t_xkb *) p->priv;
    lxpanel_put_int(fp, "DisplayType", xkb->display_type);
    lxpanel_put_int(fp, "PerAppLayout", xkb->enable_perapp);
    lxpanel_put_int(fp, "DefaultGroup", xkb->default_group);
}

/* Callback when panel configuration changes. */
static void xkb_panel_configuration_changed(Plugin * p)
{
    /* Do a full redraw. */
    t_xkb * xkb = (t_xkb *) p->priv;
    xkb->size = p->panel->icon_size;
    set_new_locale(xkb);
}

PluginClass xkb_plugin_class = {

    PLUGINCLASS_VERSIONING,

    type : "xkb",
    name : N_("Keyboard Layout Switcher"),
    version: "1.0",
    description : N_("Switch between available keyboard layouts"),

    /* Due to static variable usage; when context migrated to t_xkb, can be removed. */
    one_per_system : TRUE,

    constructor : xkb_constructor,
    destructor  : xkb_destructor,
    config : xkb_configure,
    save : xkb_save_configuration,
    panel_configuration_changed : xkb_panel_configuration_changed
};
