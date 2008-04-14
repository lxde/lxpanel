/**
 * Copyright (c) 2006 LxDE Developers, see the file AUTHORS for details.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "plugin.h"
#include "panel.h"
#include "misc.h"
#include "bg.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <glib/gi18n.h>

#include "dbg.h"

void configure(Panel* p);
void restart(void);
void gtk_run(void);
void panel_config_save(Panel* panel);
static void logout(void);

Command commands[] = {
    //{ "configure", N_("Preferences"), configure },
    { "run", N_("Run"), gtk_run },
    { "restart", N_("Restart"), restart },
    { "logout", N_("Logout"), logout },
    { NULL, NULL },
};

static char* file_manager_cmd = NULL;
static char* terminal_cmd = NULL;
static char* logout_cmd = NULL;

static GtkWidget *dialog = NULL;
static GtkSizeGroup *sg;

//width
static GtkWidget *width_spinb, *width_opt;
static GtkAdjustment *width_adj;

//height
static GtkWidget *height_spinb, *height_opt;
static GtkAdjustment *height_adj;

//margin
static GtkWidget *margin_label, *margin_spinb;
static GtkAdjustment *margin_adj;

//allign
static GtkWidget *allign_opt;

//edge
static GtkWidget *edge_opt;

//transparency
static GtkWidget *tr_checkb,  *tr_colorl, *tr_colorb;;

//background
static GtkWidget *bg_checkdis, *bg_checkb, *bg_selfileb;

//properties
static GtkWidget *prop_dt_checkb, *prop_st_checkb;

extern GSList* all_panels;
extern gchar *cprofile;
extern int config;
extern FILE *pconf;

void panel_global_config_save( Panel* p, FILE *fp);
void panel_plugin_config_save( Panel* p, FILE *fp);

static void update_opt_menu(GtkWidget *w, int ind);
static void update_toggle_button(GtkWidget *w, gboolean n);
static void modify_plugin( GtkTreeView* view );
static void on_entry_changed( GtkEditable* edit, gpointer user_data );

/* older versions of glib don't provde these API */
#if ! GLIB_CHECK_VERSION(2, 8, 0)
#include <errno.h>

int g_mkdir_with_parents(const gchar *pathname, int mode)
{
    struct stat statbuf;
    char *dir, *sep;
    dir = g_strdup( pathname );
    sep = dir[0] == '/' ? dir + 1 : dir;
    do {
        sep = strchr( sep, '/' );
        if( G_LIKELY( sep ) )
            *sep = '\0';

        if( stat( dir, &statbuf) == 0 )
        {
            if( ! S_ISDIR(statbuf.st_mode) )    /* parent not dir */
                goto err;
        }
        else    /* stat failed */
        {
            if( errno == ENOENT )   /* not exists */
            {
                if( mkdir( dir, mode ) == -1 )
                    goto err;
            }
            else
                goto err;   /* unknown error */
        }

        if( G_LIKELY( sep ) )
        {
            *sep = '/';
            ++sep;
        }
        else
            break;
    }while( sep );
    g_free( dir );
    return 0;
err:
    g_free( dir );
    return -1;
}
#endif

static void
response_event(GtkDialog *widget, gint arg1, gpointer user_data)
{
    Panel* panel = (Panel*)user_data;
    ENTER;
    switch (arg1) {
    /* FIXME: what will happen if the user exit lxpanel without
              close this config dialog?
              Then the config won't be save, I guess. */
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_CLOSE:
    case GTK_RESPONSE_NONE:
        panel_config_save( panel );
        /* NOTE: NO BREAK HERE*/
        gtk_widget_destroy(dialog);
        dialog = NULL;
        break;
    }
    RET();
}

static void
update_panel_geometry( Panel* p )
{
    calculate_position(p);
    gdk_window_move_resize(p->topgwin->window, p->ax, p->ay, p->aw, p->ah);

    panel_set_wm_strut( p );
}

static void
set_edge( GtkComboBox *widget,  Panel* p )
{
    int edge;

    ENTER;
    edge = gtk_combo_box_get_active(widget) + 1;
    p->edge = edge;
    panel_set_orientation( p );
    update_panel_geometry(p);
    panel_update_background( p );
    RET();
}

static void
set_allign( GtkComboBox *widget,  Panel* p )
{
    int allign;
    gboolean t;

    ENTER;
    allign = gtk_combo_box_get_active(widget) + 1;
    t = (allign != ALLIGN_CENTER);
    gtk_widget_set_sensitive(margin_label, t);
    gtk_widget_set_sensitive(margin_spinb, t);
    p->allign = allign;
    update_panel_geometry(p);
    RET();
}

static void
set_margin( GtkSpinButton* spin,  Panel* p  )
{
    p->margin = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry(p);
}


GtkWidget *
mk_position( Panel* p )
{
    GtkWidget *hbox, *hbox2, *label, *frame;
    GtkWidget *t;


    ENTER;
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL (label),_("<b>Position</b>"));
    gtk_frame_set_label_widget(GTK_FRAME (frame), label);

    hbox2 = gtk_hbox_new(FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (hbox2), 6);
    gtk_container_add (GTK_CONTAINER (frame), hbox2);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_widget_set_size_request(hbox, 20, 1);
    gtk_box_pack_start(GTK_BOX (hbox2), hbox, FALSE, TRUE, 0);

    t = gtk_table_new(5, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(t), 3);
    gtk_table_set_col_spacings(GTK_TABLE(t), 5);
    gtk_box_pack_start(GTK_BOX (hbox2), t, FALSE, TRUE, 0);

    //Edge
    label = gtk_label_new(_("Edge:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_widget_show(label);
    gtk_table_attach(GTK_TABLE(t), label, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_size_group_add_widget(sg, label);

    edge_opt = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(edge_opt), _("Left"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(edge_opt), _("Right"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(edge_opt), _("Top"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(edge_opt), _("Bottom"));
    update_opt_menu(edge_opt, p->edge - 1);

    g_signal_connect(G_OBJECT(edge_opt), "changed", G_CALLBACK(set_edge), NULL);

    gtk_widget_show(edge_opt);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX (hbox), edge_opt, FALSE, TRUE, 0);
    gtk_table_attach(GTK_TABLE(t), hbox, 1, 2, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), edge_opt);

    //Alignment
    label = gtk_label_new(_("Alignment:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_widget_show(label);
    gtk_size_group_add_widget(sg, label);

    gtk_table_attach(GTK_TABLE(t), label, 0, 1, 1, 2, GTK_FILL, 0, 0, 0);

    allign_opt = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(allign_opt), _("Left"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(allign_opt), _("Center"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(allign_opt), _("Right"));
    update_opt_menu(allign_opt, p->allign - 1);
    g_signal_connect(G_OBJECT(allign_opt), "changed", G_CALLBACK(set_allign), NULL);
    gtk_widget_show(allign_opt);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX (hbox), allign_opt, FALSE, TRUE, 0);
    gtk_table_attach(GTK_TABLE(t), hbox, 1, 2, 1, 2, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), allign_opt);


    //Margin
    margin_label = gtk_label_new(_("Margin:"));
    gtk_misc_set_alignment(GTK_MISC(margin_label), 0.0, 0.5);
    gtk_widget_show(margin_label);

    gtk_table_attach(GTK_TABLE(t), margin_label, 2, 3, 1, 2, GTK_FILL, 0, 0, 0);

    margin_adj = (GtkAdjustment *) gtk_adjustment_new (0.0, 0.0, gdk_screen_width(), 1.0, 5.0, 5.0);
    margin_spinb = gtk_spin_button_new (margin_adj, 1.0, 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(margin_spinb), p->margin);
    g_signal_connect( margin_spinb, "value-changed",
                      G_CALLBACK(set_margin), NULL);
    gtk_table_attach(GTK_TABLE(t), margin_spinb, 3, 4, 1, 2, GTK_FILL, 0, 0, 0);
    gtk_table_set_col_spacing(GTK_TABLE(t), 1, 20);

    RET(frame);
}

static void
set_width(  Panel* p, GtkSpinButton* spin, gpointer user_data )
{
    p->width = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry(p);
}

static void
set_height( Panel* p, GtkSpinButton* spin, gpointer user_data )
{
    p->height = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry(p);
}

static void
set_width_type( Panel* p,GtkWidget *item, gpointer bp)
{
    int widthtype;
    gboolean t;

    ENTER;

    widthtype = gtk_combo_box_get_active(GTK_COMBO_BOX(item)) + 1;
    p->widthtype = widthtype;
    t = (widthtype != WIDTH_REQUEST);
    gtk_widget_set_sensitive(width_spinb, t);
    if (widthtype == WIDTH_PERCENT) {
        width_adj->upper = 100;
        width_adj->value = width_adj->upper;
    } else if  (widthtype == WIDTH_PIXEL) {
        width_adj->upper = gdk_screen_width();
        width_adj->value = width_adj->upper;
    } else
        RET();

    gtk_adjustment_changed(width_adj);
    gtk_adjustment_value_changed(width_adj);

    update_panel_geometry(p);
    RET();
}


static GtkWidget *
mk_size( Panel* p )
{
    GtkWidget *hbox, *hbox2, *label, *frame;
    GtkWidget *t;

    ENTER;
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL (label),_("<b>Size</b>"));
    gtk_frame_set_label_widget(GTK_FRAME (frame), label);

    hbox2 = gtk_hbox_new(FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (hbox2), 6);
    gtk_container_add (GTK_CONTAINER (frame), hbox2);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_widget_set_size_request(hbox, 20, 1);
    gtk_box_pack_start(GTK_BOX (hbox2), hbox, FALSE, TRUE, 0);

    t = gtk_table_new(3, 2, FALSE);
    gtk_table_set_row_spacings(GTK_TABLE(t), 3);
    gtk_table_set_col_spacings(GTK_TABLE(t), 5);
    gtk_box_pack_start(GTK_BOX (hbox2), t, FALSE, TRUE, 0);

    //width
    label = gtk_label_new(_("Width:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_widget_show(label);
    gtk_table_attach(GTK_TABLE(t), label, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_size_group_add_widget(sg, label);

    width_adj = (GtkAdjustment *) gtk_adjustment_new (20.0, 0.0, 2100.0, 1.0, 5.0, 5.0);
    width_spinb = gtk_spin_button_new (width_adj, 1.0, 0);
    gtk_adjustment_set_value(width_adj, p->width);
    g_signal_connect( width_spinb, "value-changed",
                      G_CALLBACK(set_width), NULL );
    gtk_table_attach(GTK_TABLE(t), width_spinb, 1, 2, 0, 1, GTK_FILL, 0, 0, 0);

    width_opt = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(width_opt), _("dynamic"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(width_opt), _("pixels"));
    gtk_combo_box_append_text(GTK_COMBO_BOX(width_opt), _("% of edge"));
    update_opt_menu(width_opt, p->widthtype - 1);
    g_signal_connect(G_OBJECT(width_opt), "changed",
                     G_CALLBACK(set_width_type), NULL);
    gtk_widget_show(width_opt);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX (hbox), width_opt, FALSE, TRUE, 0);
    gtk_table_attach(GTK_TABLE(t), hbox, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), width_opt);


    //height
    label = gtk_label_new(_("Height:"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_widget_show(label);
    gtk_table_attach(GTK_TABLE(t), label, 0, 1, 1, 2, GTK_FILL, 0, 0, 0);
    gtk_size_group_add_widget(sg, label);

    height_adj = (GtkAdjustment *) gtk_adjustment_new (30.0, 0.0, 300.0, 1.0, 5.0, 5.0);
    height_spinb = gtk_spin_button_new (height_adj, 1.0, 0);
    gtk_adjustment_set_value(height_adj, p->height);
    g_signal_connect( height_spinb, "value-changed",
                      G_CALLBACK(set_height), NULL );
    gtk_table_attach(GTK_TABLE(t), height_spinb, 1, 2, 1, 2, GTK_FILL, 0, 0, 0);

    height_opt = gtk_combo_box_new_text();
    gtk_combo_box_append_text(GTK_COMBO_BOX(height_opt), _("pixels"));
    update_opt_menu(height_opt, HEIGHT_PIXEL - 1);
    //g_signal_connect(G_OBJECT(height_opt), "changed", G_CALLBACK(set_height), NULL);
    gtk_widget_show(height_opt);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX (hbox), height_opt, FALSE, TRUE, 0);
    gtk_table_attach(GTK_TABLE(t), hbox, 2, 3, 1, 2, GTK_FILL, 0, 0, 0);
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), height_opt);

    RET(frame);
}

static void
transparency_toggle( GtkWidget *b, Panel* p)
{
    gboolean t;

    ENTER;
    t = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b));
    gtk_widget_set_sensitive(tr_colorl, t);
    gtk_widget_set_sensitive(tr_colorb, t);

    /* Update background immediately. */
    if (t&&!p->transparent) {
        p->transparent = 1;
        p->background = 0;
        panel_update_background( p );
        //restart();
    }
    RET();
}

GtkWidget *
mk_transparency( Panel* p )
{
    GtkWidget *label, *frame;

    ENTER;
    frame = gtk_hbox_new(FALSE, 0);

    tr_checkb = gtk_radio_button_new_with_label(NULL, _("Enable Transparency"));
    gtk_widget_show(tr_checkb);
    gtk_box_pack_start(GTK_BOX (frame), tr_checkb, FALSE, FALSE, 0);

    tr_colorl = gtk_label_new(_("Tint color:"));
    gtk_misc_set_alignment(GTK_MISC(tr_colorl), 0.0, 0.5);
    gtk_widget_show(tr_colorl);
    gtk_box_pack_start(GTK_BOX (frame), tr_colorl, FALSE, FALSE, 5);

    tr_colorb = gtk_color_button_new();
    gtk_color_button_set_use_alpha(GTK_COLOR_BUTTON(tr_colorb), TRUE);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON(tr_colorb), 65535/256*125);
    gtk_box_pack_start(GTK_BOX (frame), tr_colorb, FALSE, FALSE, 0);
    gtk_color_button_set_color(GTK_COLOR_BUTTON(tr_colorb), &p->gtintcolor);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON(tr_colorb), 256*p->alpha);

    if (!p->transparent) {
        gtk_widget_set_sensitive(tr_colorb, FALSE);
    }

    RET(frame);
}

static void
background_toggle( GtkWidget *b, Panel* p)
{
    ENTER;
    gtk_widget_set_sensitive(bg_selfileb, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b)));
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b))) {
        if (!p->background) {
            p->transparent = 0;
            p->background = 1;
            /* Update background immediately. */
            panel_update_background( p );
            //restart();
            }
    }

    RET();
}

static void
background_changed(GtkFileChooser *file_chooser,  Panel* p )
{
    ENTER;

    p->transparent = 0;
    p->background = 1;
    p->background_file = g_strdup(gtk_file_chooser_get_filename(file_chooser));
    /* Update background immediately. */
    panel_update_background( p );
    //restart();
    RET();
}

GtkWidget *
mk_appearanceimg( Panel* p )
{
    GtkWidget *frame;

    ENTER;
    frame = gtk_vbox_new(FALSE, 0);

    bg_checkb = gtk_radio_button_new_with_label(NULL, _("Enable Image:"));
    gtk_box_pack_start(GTK_BOX(frame), GTK_WIDGET(bg_checkb), FALSE, FALSE, 5);

    bg_selfileb = gtk_file_chooser_button_new (_("Select a background image file"), GTK_FILE_CHOOSER_ACTION_OPEN);
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (bg_selfileb), PACKAGE_DATA_DIR "/lxpanel/images");

    gtk_box_pack_start(GTK_BOX (frame), bg_selfileb, FALSE, FALSE, 0);

    if (p->background_file)
        gtk_file_chooser_set_filename (GTK_FILE_CHOOSER (bg_selfileb), p->background_file);

    if (!p->background)
        gtk_widget_set_sensitive(bg_selfileb, FALSE);

    g_signal_connect (GTK_FILE_CHOOSER (bg_selfileb), "file-set", G_CALLBACK (background_changed), NULL);

    RET(frame);
}

static void
background_disable_toggle( GtkWidget *b, Panel* p )
{
    ENTER;
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b))) {
        if (p->background!=0||p->transparent!=0) {
            p->background = 0;
            p->transparent = 0;
            /* Update background immediately. */
            panel_update_background( p );
            //restart();
        }
    }

    RET();
}

static void
on_font_color_set( GtkColorButton* clr,  Panel* p )
{
    gtk_color_button_get_color( clr, &p->gfontcolor );
    /* FIXME: need some better mechanism to update the panel */
    if( p->usefontcolor )
        gtk_widget_queue_draw( p->topgwin );
}

static void
on_use_font_color_toggled( GtkToggleButton* btn,   Panel* p )
{
    GtkWidget* clr = (GtkWidget*)g_object_get_data( btn, "clr" );
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)))
        gtk_widget_set_sensitive( clr, TRUE );
    else
        gtk_widget_set_sensitive( clr, FALSE );
    p->usefontcolor = gtk_toggle_button_get_active( btn );
    /* FIXME: need some better mechanism to update the panel */
    gtk_widget_queue_draw( p->topgwin );
}

GtkWidget *
mk_appearance( Panel* p )
{
    GtkWidget *top_vbox, *vbox, *vbox2, *label, *frame, *incframe, *hbox, *fnt, *clr, *use_fnt_clr;
    GSList *check_group;

    ENTER;
    top_vbox = gtk_vbox_new( FALSE, 1 );

    frame = gtk_frame_new(NULL);
    gtk_box_pack_start( (GtkBox*)top_vbox, frame, FALSE, TRUE, 0 );

    gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL (label),_("<b>Background</b>"));
    gtk_frame_set_label_widget(GTK_FRAME (frame), label);

    vbox2 = gtk_vbox_new(FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (vbox2), 6);
    gtk_container_add (GTK_CONTAINER (frame), vbox2);

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX (vbox2), vbox, FALSE, TRUE, 0);
    gtk_widget_set_size_request(vbox, 20, 1);

    vbox = gtk_vbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX (vbox2), vbox, FALSE, TRUE, 0);

    /* Disable Background option */
    bg_checkdis = gtk_radio_button_new_with_label(NULL, _("None (Use system theme)"));
    gtk_widget_show(bg_checkdis);

    gtk_box_pack_start(GTK_BOX (vbox), bg_checkdis, FALSE, FALSE, 0);

    incframe = mk_transparency( p );
    gtk_box_pack_start(GTK_BOX (vbox), incframe, FALSE, FALSE, 0);

    incframe = mk_appearanceimg( p );
    gtk_box_pack_start(GTK_BOX (vbox), incframe, FALSE, FALSE, 0);

    /* set group */
    check_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(bg_checkdis));
    gtk_radio_button_set_group(GTK_RADIO_BUTTON(bg_checkb), check_group);
    check_group = gtk_radio_button_get_group(GTK_RADIO_BUTTON(bg_checkb));
    gtk_radio_button_set_group(GTK_RADIO_BUTTON(tr_checkb), check_group);

    /* default */
    if (p->background)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bg_checkb), TRUE);
    else if (p->transparent)
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tr_checkb), TRUE);
    else
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(bg_checkdis), TRUE);

    frame = gtk_frame_new(NULL);
    gtk_box_pack_start( (GtkBox*)top_vbox, frame, FALSE, TRUE, 0 );

    gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL (label),_("<b>Font</b>"));
    gtk_frame_set_label_widget(GTK_FRAME (frame), label);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
    gtk_container_add (GTK_CONTAINER (frame), hbox);

//  fnt = gtk_font_button_new();
//  gtk_box_pack_start( (GtkBox*)hbox, fnt, TRUE, TRUE, 4 );

    use_fnt_clr = gtk_check_button_new_with_label( _("Custom Color") );
    gtk_box_pack_start( (GtkBox*)hbox, use_fnt_clr, FALSE, FALSE, 4 );
    gtk_toggle_button_set_active( (GtkToggleButton*) use_fnt_clr, p->usefontcolor );

    clr = gtk_color_button_new();
    gtk_box_pack_start( (GtkBox*)hbox, clr, FALSE, FALSE, 4 );
    gtk_color_button_set_color( (GtkColorButton*)clr, &p->gfontcolor );
    g_signal_connect( clr, "color-set", G_CALLBACK( on_use_font_color_toggled ), p );

    gtk_widget_show_all( top_vbox );

    /* background */
    g_signal_connect(bg_checkdis, "toggled", G_CALLBACK(background_disable_toggle), p);
    g_signal_connect(bg_checkb, "toggled", G_CALLBACK(background_toggle), p);
    g_signal_connect(tr_checkb, "toggled", G_CALLBACK(transparency_toggle), p);

    /* font */
    g_object_set_data( use_fnt_clr, "clr", clr );
    g_signal_connect(use_fnt_clr, "toggled", G_CALLBACK(on_use_font_color_toggled), p);

    RET(top_vbox);
}

static void
set_dock_type(GtkToggleButton* toggle,  Panel* p )
{
    p->setdocktype = gtk_toggle_button_get_active(toggle) ? 1 : 0;
    panel_set_dock_type( p );
    update_panel_geometry(p);
    /* FIXME: apparently, this doesn't work,
              but we don't know the reason yet! */
}

static void
set_struct(GtkToggleButton* toggle,  Panel* p )
{
    p->setstrut = gtk_toggle_button_get_active(toggle) ? 1 : 0;
    update_panel_geometry(p);
}

GtkWidget *
mk_properties( Panel* p )
{
    GtkWidget *vbox, *hbox, *hbox2, *label, *frame;

    ENTER;
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL (label),_("<b>Properties</b>"));
    gtk_frame_set_label_widget(GTK_FRAME (frame), label);

    hbox2 = gtk_hbox_new(FALSE, 0);
    gtk_container_add (GTK_CONTAINER (frame), hbox2);
    gtk_container_set_border_width (GTK_CONTAINER (hbox2), 6);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX (hbox2), hbox, FALSE, TRUE, 0);
    gtk_widget_set_size_request(hbox, 20, 1);

    vbox = gtk_vbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX (hbox2), vbox, FALSE, TRUE, 0);

    /* Explaination from Ruediger Arp <ruediger@gmx.net>:
        "Set Dock Type", it is referring to the behaviour of
        dockable applications such as those found in WindowMaker (e.g.
        http://www.cs.mun.ca/~gstarkes/wmaker/dockapps ) and other
        lightweight window managers. These dockapps are probably being
        treated in some special way.
    */
    prop_dt_checkb = gtk_check_button_new_with_label(
                        _("Make window managers treat the panel as dock"));
    update_toggle_button(prop_dt_checkb, p->setdocktype);
    g_signal_connect( prop_dt_checkb, "toggled",
                      G_CALLBACK(set_dock_type), NULL );
    gtk_box_pack_start(GTK_BOX (vbox), prop_dt_checkb, FALSE, FALSE, 0);

    /* Explaination from Ruediger Arp <ruediger@gmx.net>:
        "Set Strut": Reserve panel's space so that it will not be
        covered by maximazied windows.
        This is clearly an option to avoid the panel being
        covered/hidden by other applications so that it always is
        accessible. The panel "steals" some screen estate which cannot
        be accessed by other applications.
        GNOME Panel acts this way, too.
    */
    prop_st_checkb = gtk_check_button_new_with_label(
                        _("Reserve space, and not covered by maximized windows"));
    update_toggle_button(prop_st_checkb, p->setstrut);
    g_signal_connect( prop_st_checkb, "toggled",
                      G_CALLBACK(set_struct), NULL );
    gtk_box_pack_start(GTK_BOX (vbox), prop_st_checkb, FALSE, FALSE, 0);

    RET(frame);
}

static void
dialog_destroy_event(GtkWidget * widget, GdkEvent * event, gpointer data)
{
    ENTER;
    dialog = NULL;
    RET();
}

static gint
dialog_delete_event( GtkWidget *widget, GdkEvent  *event, gpointer   data )
{

    ENTER;
    //if (!p->self_destroy)
    RET(FALSE);
}

static void
on_sel_plugin_changed( GtkTreeSelection* tree_sel, GtkWidget* label )
{
    GtkTreeIter it;
    GtkTreeModel* model;
    Plugin* pl;

    if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
    {
        GtkTreeView* view = gtk_tree_selection_get_tree_view( tree_sel );
        GtkWidget *edit_btn = GTK_WIDGET(g_object_get_data( G_OBJECT(view), "edit_btn" ));
        gtk_tree_model_get( model, &it, 1, &pl, -1 );
        gtk_label_set_text( GTK_LABEL(label), _(pl->class->description) );
        gtk_widget_set_sensitive( edit_btn, pl->class->config != NULL );
    }
}

static void init_plugin_list( Panel* p, GtkTreeView* view, GtkWidget* label )
{
    GtkListStore* list;
    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    GtkTreeSelection* tree_sel;
    GList* l;
    GtkTreeIter it;

    g_object_set_data( view, "panel", p );

    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
            _("Currently loaded plugins"),
            render, "text", 0, NULL );
    gtk_tree_view_append_column( view, col );

    list = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_POINTER );
    for( l = p->plugins; l; l = l->next )
    {
        GtkTreeIter it;
        Plugin* pl = (Plugin*)l->data;
        gtk_list_store_append( list, &it );
        gtk_list_store_set( list, &it,
                            0, _(pl->class->name),
                            1, pl, -1);
    }
    gtk_tree_view_set_model( view, GTK_TREE_MODEL( list ) );
    g_signal_connect( view, "row-activated",
                      G_CALLBACK(modify_plugin), NULL );
    tree_sel = gtk_tree_view_get_selection( view );
    gtk_tree_selection_set_mode( tree_sel, GTK_SELECTION_BROWSE );
    g_signal_connect( tree_sel, "changed",
                      G_CALLBACK(on_sel_plugin_changed), label);
    if( gtk_tree_model_get_iter_first( GTK_TREE_MODEL(list), &it ) )
        gtk_tree_selection_select_iter( tree_sel, &it );
}

static void on_add_plugin_response( GtkDialog* dlg,
                                    int response,
                                    GtkTreeView* _view )
{
    Panel* p = (Panel*) g_object_get_data( _view, "panel" );
    if( response == GTK_RESPONSE_OK )
    {
        GtkTreeView* view;
        GtkTreeSelection* tree_sel;
        GtkTreeIter it;
        GtkTreeModel* model;

        view = (GtkTreeView*)g_object_get_data( G_OBJECT(dlg), "avail-plugins" );
        tree_sel = gtk_tree_view_get_selection( view );
        if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        {
            char* type = NULL;
            Plugin* pl;
            gtk_tree_model_get( model, &it, 1, &type, -1 );
            if( pl = plugin_load( type ) )
            {
                GtkTreePath* tree_path;

                pl->panel = p;
                plugin_start( pl, NULL );
                p->plugins = g_list_append(p->plugins, pl);
                /* FIXME: will show all cause problems? */
                gtk_widget_show_all( pl->pwid );

                /* update background of the newly added plugin */
                plugin_widget_set_background( pl->pwid, pl->panel );

                model = gtk_tree_view_get_model( _view );
                gtk_list_store_append( (GtkListStore*)model, &it );
                gtk_list_store_set( (GtkListStore*)model, &it,
                                    0, _(pl->class->name),
                                    1, pl, -1 );
                tree_sel = gtk_tree_view_get_selection( _view );
                gtk_tree_selection_select_iter( tree_sel, &it );
                if( tree_path = gtk_tree_model_get_path( model, &it ) )
                {
                    gtk_tree_view_scroll_to_cell( _view, tree_path, NULL, FALSE, 0, 0 );
                    gtk_tree_path_free( tree_path );
                }
            }
            g_free( type );
        }
    }
/*
    gtk_widget_set_sensitive( (GtkWidget*)gtk_window_get_transient_for( (GtkWindow*)dlg ),
                               TRUE );
*/
    gtk_widget_destroy( (GtkWidget*)dlg );
}

static void on_add_plugin( GtkButton* btn, GtkTreeView* _view )
{
    GtkWidget* dlg, *parent_win, *scroll;
    GList* classes;
    GList* tmp;
    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    GtkTreeView* view;
    GtkListStore* list;
    GtkTreeSelection* tree_sel;

    Panel* p = (Panel*) g_object_get_data( _view, "panel" );

    classes = plugin_get_available_classes();

    parent_win = gtk_widget_get_toplevel( (GtkWidget*)_view );
    dlg = gtk_dialog_new_with_buttons( _("Add plugin to panel"),
                                       GTK_WINDOW(parent_win), 0,
                                       GTK_STOCK_CANCEL,
                                       GTK_RESPONSE_CANCEL,
                                       GTK_STOCK_ADD,
                                       GTK_RESPONSE_OK, NULL );

    /* fix background */
    if (p->background)
        gtk_widget_set_style(dlg, p->defstyle);

    /* gtk_widget_set_sensitive( parent_win, FALSE ); */
    scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_shadow_type( (GtkScrolledWindow*)scroll,
                                          GTK_SHADOW_IN );
    gtk_scrolled_window_set_policy((GtkScrolledWindow*)scroll,
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC );
    gtk_box_pack_start( (GtkBox*)GTK_DIALOG(dlg)->vbox, scroll,
                         TRUE, TRUE, 4 );
    view = (GtkTreeView*)gtk_tree_view_new();
    gtk_container_add( (GtkContainer*)scroll, view );
    tree_sel = gtk_tree_view_get_selection( view );
    gtk_tree_selection_set_mode( tree_sel, GTK_SELECTION_BROWSE );

    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
                                            _("Available plugins"),
                                            render, "text", 0, NULL );
    gtk_tree_view_append_column( view, col );

    list = gtk_list_store_new( 2,
                               G_TYPE_STRING,
                               G_TYPE_STRING );

    for( tmp = classes; tmp; tmp = tmp->next ) {
        PluginClass* pc = (PluginClass*)tmp->data;
        if( ! pc->invisible ) {
            /* FIXME: should we display invisible plugins? */
            GtkTreeIter it;
            gtk_list_store_append( list, &it );
            gtk_list_store_set( list, &it,
                                0, _(pc->name),
                                1, pc->type, -1 );
            /* g_debug( "%s (%s)", pc->type, _(pc->name) ); */
        }
    }

    gtk_tree_view_set_model( view, GTK_TREE_MODEL(list) );
    g_object_unref( list );

    g_signal_connect( dlg, "response",
                      on_add_plugin_response, _view );
    g_object_set_data( dlg, "avail-plugins", view );
    g_object_weak_ref( dlg, plugin_class_list_free, classes );

    gtk_window_set_default_size( (GtkWindow*)dlg, 320, 400 );
    gtk_widget_show_all( dlg );
}

static void on_remove_plugin( GtkButton* btn, GtkTreeView* view )
{
    GtkTreeIter it;
    GtkTreePath* tree_path;
    GtkTreeModel* model;
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    Plugin* pl;

    Panel* p = (Panel*) g_object_get_data( view, "panel" );

    if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
    {
        tree_path = gtk_tree_model_get_path( model, &it );
        gtk_tree_model_get( model, &it, 1, &pl, -1 );
        if( gtk_tree_path_get_indices(tree_path)[0] >= gtk_tree_model_iter_n_children( model, NULL ) )
            gtk_tree_path_prev( tree_path );
        gtk_list_store_remove( GTK_LIST_STORE(model), &it );
        p->plugins = g_list_remove( p->plugins, pl );
        plugin_stop( pl ); /* free the plugin widget & its data */
        plugin_put( pl ); /* free the lib if necessary */

        gtk_tree_selection_select_path( tree_sel, tree_path );
        gtk_tree_path_free( tree_path );
    }
}

void modify_plugin( GtkTreeView* view )
{
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    GtkTreeModel* model;
    GtkTreeIter it;
    Plugin* pl;

    if( ! gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        return;

    gtk_tree_model_get( model, &it, 1, &pl, -1 );
    if( pl->class->config )
        pl->class->config( pl, (GtkWindow*)gtk_widget_get_toplevel(GTK_WIDGET(view)) );
}

static int get_widget_index(    Panel* p, Plugin* pl )
{
    GList* l;
    int i;
    for( i = 0, l = p->plugins; l; l = l->next )
    {
        Plugin* _pl = (Plugin*)l->data;
        if( _pl == pl )
            return i;
        if( _pl->pwid )
            ++i;
    }
    return -1;
}

static void on_moveup_plugin(  GtkButton* btn, GtkTreeView* view )
{
    GList *l;
    GtkTreeIter it, prev;
    GtkTreeModel* model = gtk_tree_view_get_model( view );
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    int i;

    Panel* panel = (Panel*) g_object_get_data( view, "panel" );

    if( ! gtk_tree_model_get_iter_first( model, &it ) )
        return;
    if( gtk_tree_selection_iter_is_selected( tree_sel, &it ) )
        return;
    do{
        if( gtk_tree_selection_iter_is_selected(tree_sel, &it) )
        {
            Plugin* pl;
            gtk_tree_model_get( model, &it, 1, &pl, -1 );
            gtk_list_store_move_before( GTK_LIST_STORE( model ),
                                        &it, &prev );

            i = 0;
            for( l = panel->plugins; l; l = l->next, ++i )
            {
                if( l->data == pl  )
                {
                    panel->plugins = g_list_insert( panel->plugins, pl, i - 1);
                    panel->plugins = g_list_delete_link( panel->plugins, l);
                }
            }
            if( pl->pwid )
            {
                gtk_box_reorder_child( GTK_BOX(panel->box), pl->pwid, get_widget_index( panel, pl ) );
            }
            return;
        }
        prev = it;
    }while( gtk_tree_model_iter_next( model, &it ) );
}

static void on_movedown_plugin(  GtkButton* btn, GtkTreeView* view )
{
    GList *l;
    GtkTreeIter it, next;
    GtkTreeModel* model;
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    Plugin* pl;
    int i;

    Panel* panel = (Panel*) g_object_get_data( view, "panel" );

    if( ! gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        return;
    next = it;

    if( ! gtk_tree_model_iter_next( model, &next) )
        return;

    gtk_tree_model_get( model, &it, 1, &pl, -1 );

    gtk_list_store_move_after( GTK_LIST_STORE( model ), &it, &next );

    i = 0;
    for( l = panel->plugins; l; l = l->next, ++i )
    {
        if( l->data == pl  )
        {
            panel->plugins = g_list_insert( panel->plugins, pl, i + 2);
            panel->plugins = g_list_delete_link( panel->plugins, l);
        }
    }
    if( pl->pwid )
    {
        gtk_box_reorder_child( GTK_BOX(panel->box), pl->pwid, get_widget_index( panel, pl ) );
    }
}

static GtkWidget *
mk_tab_plugins( Panel* p )
{
    GtkWidget *hbox, *vbox, *label;
    GtkWidget *scroll, *plugin_list, *button;

    hbox = gtk_hbox_new( FALSE, 2 );
    gtk_container_set_border_width( (GtkContainer*)hbox, 4 );

    vbox = gtk_vbox_new( FALSE, 2 );
    gtk_box_pack_start( GTK_BOX(hbox), vbox, TRUE, TRUE, 2 );

    /* Left pane */
    plugin_list = gtk_tree_view_new();
    /* plugin list */
    scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy( GTK_SCROLLED_WINDOW(scroll),
                            GTK_POLICY_AUTOMATIC,
                    GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type( GTK_SCROLLED_WINDOW(scroll),
                                 GTK_SHADOW_IN );
    gtk_container_add( GTK_CONTAINER(scroll), plugin_list );
    gtk_box_pack_start( GTK_BOX( vbox ), scroll, TRUE, TRUE, 4 );

    /* Label displaying plugin descriptions */
    label = gtk_label_new("");
    gtk_label_set_ellipsize(GTK_LABEL(label), PANGO_ELLIPSIZE_END);

    gtk_box_pack_start( GTK_BOX(vbox), label, FALSE, FALSE, 4 );

    vbox = gtk_vbox_new( FALSE, 2 );
    gtk_box_pack_start( GTK_BOX( hbox ), vbox, FALSE, FALSE, 2 );

    /* buttons used to edit plugin list */
    button = gtk_button_new_from_stock( GTK_STOCK_ADD );
    gtk_box_pack_start( GTK_BOX( vbox ), button, FALSE, FALSE, 2 );
    g_signal_connect( button, "clicked", G_CALLBACK(on_add_plugin), plugin_list );

    button = gtk_button_new_from_stock( GTK_STOCK_EDIT );
    gtk_box_pack_start( GTK_BOX( vbox ), button, FALSE, FALSE, 2 );
    g_signal_connect_swapped( button, "clicked", G_CALLBACK(modify_plugin), plugin_list );
    g_object_set_data( G_OBJECT(plugin_list), "edit_btn", button );

    button = gtk_button_new_from_stock( GTK_STOCK_REMOVE );
    gtk_box_pack_start( GTK_BOX( vbox ), button, FALSE, FALSE, 2 );
    g_signal_connect( button, "clicked", G_CALLBACK(on_remove_plugin), plugin_list );

    button = gtk_button_new();
    gtk_container_add( GTK_CONTAINER(button),
               gtk_image_new_from_stock(GTK_STOCK_GO_UP,
                                    GTK_ICON_SIZE_BUTTON) );
    gtk_box_pack_start( GTK_BOX( vbox ), button, FALSE, FALSE, 2 );
    g_signal_connect( button, "clicked", G_CALLBACK(on_moveup_plugin), plugin_list );

    button = gtk_button_new();
    gtk_container_add( GTK_CONTAINER(button),
               gtk_image_new_from_stock(GTK_STOCK_GO_DOWN,
                                    GTK_ICON_SIZE_BUTTON) );
    gtk_box_pack_start( GTK_BOX( vbox ), button, FALSE, FALSE, 2 );
    g_signal_connect( button, "clicked", G_CALLBACK(on_movedown_plugin), plugin_list );

    init_plugin_list( p, GTK_TREE_VIEW( plugin_list ), label );

    RET(hbox);
}

static GtkWidget *
mk_tab_general( Panel* p )
{
    GtkWidget *frame, *page;

    /*
    sw = gtk_scrolled_window_new(NULL, NULL);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw), GTK_SHADOW_NONE);
    gtk_container_set_border_width(GTK_CONTAINER(sw), 0);
    */
    page = gtk_vbox_new(FALSE, 1);

    sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    //position
    frame = mk_position( p );
    gtk_box_pack_start(GTK_BOX (page), frame, FALSE, TRUE, 0);

    //size
    frame = mk_size( p );
    gtk_box_pack_start(GTK_BOX (page), frame, FALSE, TRUE, 0);

    //properties
    frame = mk_properties( p );
    gtk_box_pack_start(GTK_BOX (page), frame, FALSE, TRUE, 0);
    /*
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (sw), page);
    */
    RET(page);
}

static GtkWidget *
mk_tab_appearance( Panel* p )
{
    GtkWidget *frame, *page;
    page = gtk_vbox_new(FALSE, 1);

    sg = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    //appearance
    frame = mk_appearance(p);
    gtk_box_pack_start(GTK_BOX (page), frame, FALSE, TRUE, 0);

    RET(page);
}

static GtkWidget *
mk_tab_app( Panel* p )
{
    GtkWidget *vbox, *label, *entry;
    GtkTable *tbl;

    vbox = gtk_vbox_new( FALSE, 2 );
    gtk_container_set_border_width( GTK_CONTAINER(vbox), 8 );

    label = gtk_label_new("");
    gtk_label_set_markup(GTK_LABEL(label), _("<b>Set Preferred Applications</b>"));
    gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
    gtk_box_pack_start( GTK_BOX(vbox), label, FALSE, FALSE, 2 );

    tbl = (GtkTable*)gtk_table_new( 3, 2, FALSE );
    gtk_box_pack_start( GTK_BOX(vbox), (GtkWidget*)tbl, TRUE, TRUE, 2 );

    gtk_table_set_col_spacings( tbl, 4 );
    gtk_table_set_row_spacings( tbl, 4 );

    label = gtk_label_new( _("File Manager:") );
    gtk_misc_set_alignment( label, 0, 0.5 );
    entry = gtk_entry_new();
    if (file_manager_cmd)
        gtk_entry_set_text( GTK_ENTRY(entry), file_manager_cmd );
    g_signal_connect( entry, "changed",
                      G_CALLBACK(on_entry_changed),
                      &file_manager_cmd);
    gtk_table_attach( tbl, label, 0, 1, 0, 1,
                      GTK_FILL, GTK_FILL, 2, 2 );
    gtk_table_attach( tbl, entry, 1, 2, 0, 1,
                      GTK_FILL|GTK_EXPAND, GTK_FILL, 2, 2 );

    label = gtk_label_new( _("Terminal Emulator:") );
    gtk_misc_set_alignment( label, 0, 0.5 );
    entry = gtk_entry_new();
    if (terminal_cmd)
        gtk_entry_set_text( GTK_ENTRY(entry), terminal_cmd );
    g_signal_connect( entry, "changed",
                      G_CALLBACK(on_entry_changed),
                      &terminal_cmd);
    gtk_table_attach( tbl, label, 0, 1, 1, 2,
                      GTK_FILL, GTK_FILL, 2, 2 );
    gtk_table_attach( tbl, entry, 1, 2, 1, 2,
                      GTK_FILL|GTK_EXPAND, GTK_FILL, 2, 2 );

    /* If we are under LXSession, setting logout command is unnecessary. */
    if( ! getenv("_LXSESSION_PID") ) {
        label = gtk_label_new( _("Logout Command:") );
        gtk_misc_set_alignment( label, 0, 0.5 );
        entry = gtk_entry_new();
        if(logout_cmd)
            gtk_entry_set_text( GTK_ENTRY(entry), logout_cmd );
        g_signal_connect( entry, "changed",
                        G_CALLBACK(on_entry_changed),
                        &logout_cmd);
        gtk_table_attach( tbl, label, 0, 1, 2, 3,
                        GTK_FILL, GTK_FILL, 2, 2 );
        gtk_table_attach( tbl, entry, 1, 2, 2, 3,
                        GTK_FILL|GTK_EXPAND, GTK_FILL, 2, 2 );
    }
    return vbox;
}

static GtkWidget* mk_dialog( Panel* p )
{
    GtkWidget *sw, *nb, *label;

    dialog = gtk_dialog_new_with_buttons (_("lxpanel configurator"),
          NULL,
          0, //GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_STOCK_CLOSE,
          GTK_RESPONSE_CLOSE,
          NULL);

    g_signal_connect (G_OBJECT(dialog), "response",     (GCallback) response_event, p);
    g_signal_connect (G_OBJECT(dialog), "destroy",      (GCallback) dialog_destroy_event, NULL);
    g_signal_connect (G_OBJECT(dialog), "delete_event", (GCallback) dialog_delete_event,  NULL);
    gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_position( GTK_WINDOW(dialog), GTK_WIN_POS_CENTER );

    /* fix background */
    if (p->background)
        gtk_widget_set_style(dialog, p->defstyle);

    /*
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(dialog), TRUE);
    */
    nb = gtk_notebook_new();
    gtk_notebook_set_show_border (GTK_NOTEBOOK(nb), FALSE);
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), nb);

    sw = mk_tab_general(p);
    label = gtk_label_new(_("General"));
    gtk_misc_set_padding(GTK_MISC(label), 4, 1);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), sw, label);

    sw = mk_tab_appearance(p);
    label = gtk_label_new(_("Appearance"));
    gtk_misc_set_padding(GTK_MISC(label), 4, 1);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), sw, label);

    sw = mk_tab_plugins(p);
    label = gtk_label_new(_("Plugins"));
    gtk_misc_set_padding(GTK_MISC(label), 4, 1);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), sw, label);

    sw = mk_tab_app(p);
    label = gtk_label_new(_("Applications"));
    gtk_misc_set_padding(GTK_MISC(label), 4, 1);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), sw, label);

    g_object_unref(sg);

    //gtk_widget_show_all(page);
    gtk_widget_show_all(dialog);

    RET(dialog);
}

static void
update_opt_menu(GtkWidget *w, int ind)
{
    int i;

    ENTER;
    /* this trick will trigger "changed" signal even if active entry is
     * not actually changing */
    i = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
    if (i == ind) {
        i = i ? 0 : 1;
        gtk_combo_box_set_active(GTK_COMBO_BOX(w), i);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(w), ind);
    RET();
}

static void
update_toggle_button(GtkWidget *w, gboolean n)
{
    gboolean c;

    ENTER;
    /* this trick will trigger "changed" signal even if active entry is
     * not actually changing */
    c = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(w));
    if (c == n) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), !n);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), n);
    RET();
}

void
configure( Panel* p )
{
    ENTER;
    if( ! dialog )
        dialog = mk_dialog( p );
    gtk_window_present((GtkWindow*)dialog);
    RET();
}

void
panel_global_config_save( Panel* p, FILE *fp)
{
    GdkColor c;

    fprintf(fp, "# lxpanel <profile> config file\n"
                "# see http://lxpanel.sf.net/docs.html for complete configuration guide\n");
    lxpanel_put_line(fp, "Global {");
    lxpanel_put_str(fp, "edge", num2str(edge_pair, gtk_combo_box_get_active(GTK_COMBO_BOX(edge_opt)) + 1, "none"));
    lxpanel_put_str(fp, "allign", num2str(allign_pair, gtk_combo_box_get_active(GTK_COMBO_BOX(allign_opt)) + 1, "none"));
    lxpanel_put_int(fp, "margin", (int)margin_adj->value);
    lxpanel_put_str(fp, "widthtype", num2str(width_pair, gtk_combo_box_get_active(GTK_COMBO_BOX(width_opt)) + 1, "none"));
    lxpanel_put_int(fp, "width", (int) width_adj->value);
    lxpanel_put_int(fp, "height", (int) height_adj->value);
    lxpanel_put_str(fp, "transparent", num2str(bool_pair, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tr_checkb)), "false"));
    gtk_color_button_get_color(GTK_COLOR_BUTTON(tr_colorb), &c);
    lxpanel_put_line(fp, "tintcolor = #%06x", gcolor2rgb24(&c));
    lxpanel_put_int(fp, "alpha", gtk_color_button_get_alpha(GTK_COLOR_BUTTON(tr_colorb)) * 0xff / 0xffff);
    lxpanel_put_str(fp, "setdocktype", num2str(bool_pair, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(prop_dt_checkb)), "true"));
    lxpanel_put_str(fp, "setpartialstrut", num2str(bool_pair, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(prop_st_checkb)), "true"));
    lxpanel_put_str(fp, "useFontColor", p->usefontcolor ? "true" : "false");
    lxpanel_put_line(fp, "FontColor = #%06x", gcolor2rgb24(&p->gfontcolor));
    lxpanel_put_str(fp, "Background", gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bg_checkb)) ? "true" : "false");
    lxpanel_put_str(fp, "BackgroundFile", p->background_file );
    lxpanel_put_line(fp, "}\n");
}

void
panel_plugin_config_save( Panel* p, FILE *fp)
{
    GList* l;
    for( l = p->plugins; l; l = l->next )
    {
        Plugin* pl = (Plugin*)l->data;
        lxpanel_put_line( fp, "Plugin {" );
        lxpanel_put_line( fp, "type = %s", pl->class->type );
        if( pl->expand )
            lxpanel_put_bool( fp, "expand", TRUE );
        if( pl->padding > 0 )
            lxpanel_put_int( fp, "padding", pl->padding );
        if( pl->border > 0 )
            lxpanel_put_int( fp, "border", pl->border );

        if( pl->class->save )
        {
            lxpanel_put_line( fp, "Config {" );
            pl->class->save( pl, fp );
            lxpanel_put_line( fp, "}" );
        }
        lxpanel_put_line( fp, "}\n" );
    }
}

void panel_config_save( Panel* p )
{
    gchar *fname, *dir;
    FILE *fp;

    dir = get_config_file( cprofile, "panels", FALSE );
    fname = g_build_filename( dir, p->name, NULL );
    g_free( dir );

    if (!(fp = fopen(fname, "w"))) {
        g_free( fname );
        ERR("can't open for write %s:", fname);
        perror(NULL);
        RET();
    }
    panel_global_config_save(p, fp);
    panel_plugin_config_save(p, fp);
    fclose(fp);
    g_free( fname );

    /* save the global config file */
    save_global_config();
}

void restart(void)
{
    /* This is defined in panel.c */
    extern gboolean is_restarting;
    ENTER;
    is_restarting = TRUE;

    /* processing any possible idle handlers before we restart */
    while (gtk_events_pending ())
        gtk_main_iteration ();
    gtk_main_quit();
    RET();
}

void logout(void)
{
    const char* logout_cmd = logout_cmd;
    /* If LXSession is running, _LXSESSION_PID will be set */
    if( ! logout_cmd && getenv("_LXSESSION_PID") )
        logout_cmd = "lxsession-logout";

    if( logout_cmd ) {
        GError* err = NULL;
        if( ! g_spawn_command_line_async( logout_cmd, &err ) ) {
            show_error( NULL, err->message );
            g_error_free( err );
        }
    }
    else {
        show_error( NULL, _("Logout command is not set") );
    }
}

static void notify_apply_config( GtkWidget* widget )
{
    GSourceFunc apply_func;
    GtkWidget* dlg;

    dlg = gtk_widget_get_toplevel( widget );
    apply_func = g_object_get_data( G_OBJECT(dlg), "apply_func" );
    if( apply_func )
        apply_func( g_object_get_data(G_OBJECT(dlg), "plugin") );
}

static void on_entry_changed( GtkEditable* edit, gpointer user_data )
{
    char** val = (char**)user_data;
    const char *new_val;
    g_free( *val );
    new_val = gtk_entry_get_text(GTK_ENTRY(edit));
    *val = (new_val && *new_val) ? g_strdup( new_val ) : NULL;
    notify_apply_config( GTK_WIDGET(edit) );
}

static void on_spin_changed( GtkSpinButton* spin, gpointer user_data )
{
    int* val = (int*)user_data;
    *val = (int)gtk_spin_button_get_value( spin );
    notify_apply_config( GTK_WIDGET(spin) );
}

static void on_toggle_changed( GtkToggleButton* btn, gpointer user_data )
{
    gboolean* val = (gboolean*)user_data;
    *val = gtk_toggle_button_get_active( btn );
    notify_apply_config( GTK_WIDGET(btn) );
}

/* Parameters: const char* name, gpointer ret_value, GType type, ....NULL */
GtkWidget* create_generic_config_dlg( const char* title, GtkWidget* parent,
                                      GSourceFunc apply_func, gpointer plugin,
                                      const char* name, ... )
{
    va_list args;
    Panel* p = ((Plugin*)plugin)->panel;
    GtkWidget* dlg = gtk_dialog_new_with_buttons( title, GTK_WINDOW(parent), 0,
                                                  GTK_STOCK_CLOSE,
                                                  GTK_RESPONSE_CLOSE,
                                                  NULL );

    /* fix background */
    if (p->background)
        gtk_widget_set_style(dlg, p->defstyle);

    /* this is a dirty hack.  We need to check if this response is GTK_RESPONSE_CLOSE or not. */
    g_signal_connect( dlg, "response", G_CALLBACK(gtk_widget_destroy), NULL );
    if( apply_func )
        g_object_set_data( G_OBJECT(dlg), "apply_func", apply_func );
    if( plugin )
        g_object_set_data( G_OBJECT(dlg), "plugin", plugin );

    gtk_box_set_spacing( GTK_BOX(GTK_DIALOG(dlg)->vbox), 4 );

    va_start( args, name );
    while( name )
    {
        GtkWidget* label = gtk_label_new( name );
        GtkWidget* entry = NULL;
        gpointer val = va_arg( args, gpointer );
        GType type = va_arg( args, GType );
        switch( type )
        {
            case G_TYPE_STRING:
                entry = gtk_entry_new();
                gtk_entry_set_text( GTK_ENTRY(entry), *(char**)val );
                g_signal_connect( entry, "changed",
                  G_CALLBACK(on_entry_changed), val );
                break;
            case G_TYPE_INT:
            {
                /* FIXME: the range shouldn't be hardcoded */
                entry = gtk_spin_button_new_with_range( 0, 1000, 1 );
                gtk_spin_button_set_value( GTK_SPIN_BUTTON(entry), *(int*)val );
                g_signal_connect( entry, "value-changed",
                  G_CALLBACK(on_spin_changed), val );
                break;
            }
            case G_TYPE_BOOLEAN:
                entry = gtk_check_button_new();
                gtk_container_add( GTK_CONTAINER(entry), label );
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(entry), *(gboolean*)val );
                g_signal_connect( entry, "toggled",
                  G_CALLBACK(on_toggle_changed), val );
                break;
        }
        if( entry )
        {
            if( type == G_TYPE_BOOLEAN )
                gtk_box_pack_start( GTK_BOX(GTK_DIALOG(dlg)->vbox), entry, FALSE, FALSE, 2 );
            else
            {
                GtkWidget* hbox = gtk_hbox_new( FALSE, 2 );
                gtk_box_pack_start( GTK_BOX(hbox), label, FALSE, FALSE, 2 );
                gtk_box_pack_start( GTK_BOX(hbox), entry, TRUE, TRUE, 2 );
                gtk_box_pack_start( GTK_BOX(GTK_DIALOG(dlg)->vbox), hbox, FALSE, FALSE, 2 );
            }
        }
        name = va_arg( args, const char* );
    }
    va_end( args );

    /* weird... why this doesn't work? */
    /* gtk_container_set_border_width( GTK_CONTAINER(GTK_DIALOG(dlg)->vbox), 12 ); */
    gtk_container_set_border_width( GTK_CONTAINER(dlg), 8 );

    gtk_widget_show_all( dlg );
    return dlg;
}

char* get_config_file( const char* profile, const char* file_name, gboolean is_global )
{
    char* path;
    if( is_global )
    {
        path = g_build_filename( PACKAGE_DATA_DIR, "lxpanel/profile", profile, file_name, NULL );
    }
    else
    {
        char* dir = g_build_filename( g_get_user_config_dir(), "lxpanel" , profile, NULL);
        /* make sure the private profile dir exists */
        /* FIXME: Should we do this everytime this func gets called?
    *        Maybe simply doing this before saving config files is enough. */
        g_mkdir_with_parents( dir, 0700 );
        path = g_build_filename( dir,file_name, NULL);
        g_free( dir );
    }
    return path;
}

const char command_group[] = "Command";
void load_global_config()
{
    GKeyFile* kf = g_key_file_new();
    char* file = get_config_file( cprofile, "config", FALSE );
    gboolean loaded = g_key_file_load_from_file( kf, file, 0, NULL );
    if( ! loaded )
    {
        g_free( file );
        file = get_config_file( cprofile, "config", TRUE ); /* get the system-wide config file */
        loaded = g_key_file_load_from_file( kf, file, 0, NULL );
    }

    if( loaded )
    {
        file_manager_cmd = g_key_file_get_string( kf, command_group, "FileManager", NULL );
        terminal_cmd = g_key_file_get_string( kf, command_group, "Terminal", NULL );
        logout_cmd = g_key_file_get_string( kf, command_group, "Logout", NULL );
    }
    g_key_file_free( kf );
}

void save_global_config()
{
    char* file = get_config_file( cprofile, "config", FALSE );
    FILE* f = fopen( file, "w" );
    if( f )
    {
        fprintf( f, "[%s]\n", command_group );
        if( file_manager_cmd )
            fprintf( f, "FileManager=%s\n", file_manager_cmd );
        if( terminal_cmd )
            fprintf( f, "Terminal=%s\n", terminal_cmd );
        if( logout_cmd )
            fprintf( f, "Logout=%s\n", logout_cmd );
        fclose( f );
    }
}

void free_global_config()
{
    g_free( file_manager_cmd );
    g_free( terminal_cmd );
    g_free( logout_cmd );
}

extern const char*
lxpanel_get_file_manager()
{
    return file_manager_cmd ? file_manager_cmd : "pcmanfm %s";
}

extern const char*
lxpanel_get_terminal()
{
    return terminal_cmd ? terminal_cmd : "xterm -e";
}

