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
#include "gtkbgbox.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <glib/gi18n.h>

//#define DEBUG
#include "dbg.h"

void configure(void);
void restart(void);
void gtk_run(void);
static void logout(void);

command commands[] = {
    { "configure", N_("Preferences"), configure },
    { "run", N_("Run"), gtk_run },
    { "restart", N_("Restart"), restart },
    { "logout", N_("Logout"), logout },
    { NULL, NULL },
};

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

//properties
static GtkWidget *prop_dt_checkb, *prop_st_checkb;

extern panel *p;
extern gchar *cprofile;
extern int config;
extern FILE *pconf;

void global_config_save(FILE *fp);
void plugin_config_save(FILE *fp);

static void update_opt_menu(GtkWidget *w, int ind);
static void update_toggle_button(GtkWidget *w, gboolean n);
static void modify_plugin( GtkTreeView* view );

static int
mk_profile_dir()
{
    gchar fname[1024];
    struct stat buf;
    int ret;

    ENTER;
    sprintf(fname, "%s/.lxpanel", getenv("HOME"));
    if ((ret = stat(fname, &buf))) {
        LOG(LOG_INFO, "creating %s\n", fname);
        mkdir(fname, 0755);
        ret = stat(fname, &buf);
    }
    if (ret)
        RET(0);
    if (!(S_ISDIR(buf.st_mode) && (S_IWUSR & buf.st_mode) && (S_IXUSR & buf.st_mode)))
        RET(0);
    RET(1);
}


static void
response_event(GtkDialog *widget, gint arg1, gpointer user_data)
{
    gchar fname[1024];
    FILE *fp;

    ENTER;
    switch (arg1) {
    /* FIXME: what will happen if the user exit lxpanel without
              close this config dialog?
              Then the config won't be save, I guess. */
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_CLOSE:
    case GTK_RESPONSE_NONE:
        if (!mk_profile_dir()) {
            ERR("can't make ~/.lxpanel direcory\n");
            RET();
        }
        sprintf(fname, "%s/.lxpanel/%s", getenv("HOME"), cprofile);
        LOG(LOG_WARN, "saving profile %s as %s\n", cprofile, fname);
        if (!(fp = fopen(fname, "w"))) {
            ERR("can't open for write %s:", fname);
            perror(NULL);
            RET();
        }
        global_config_save(fp);
        plugin_config_save(fp);
        fclose(fp);
        /* NOTE: NO BREAK HERE*/
        gtk_widget_destroy(dialog);
        dialog = NULL;
        break;
    }
    RET();
}

static void
update_panel_geometry()
{
    calculate_position(p);
    gdk_window_move_resize(p->topgwin->window, p->ax, p->ay, p->aw, p->ah);

    panel_set_wm_strut( p );
}

static void
set_edge(GtkComboBox *widget, gpointer bp)
{
    int edge;

    ENTER;
    edge = gtk_combo_box_get_active(widget) + 1;
    p->edge = edge;
    panel_set_orientation( p );
    update_panel_geometry();
    RET();
}

static void
set_allign(GtkComboBox *widget, gpointer bp)
{
    int allign;
    gboolean t;

    ENTER;
    allign = gtk_combo_box_get_active(widget) + 1;
    t = (allign != ALLIGN_CENTER);
    gtk_widget_set_sensitive(margin_label, t);
    gtk_widget_set_sensitive(margin_spinb, t);
    p->allign = allign;
    update_panel_geometry();
    RET();
}

static void
set_margin( GtkSpinButton* spin, gpointer user_data )
{
    p->margin = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry();
}


GtkWidget *
mk_position()
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
set_width( GtkSpinButton* spin, gpointer user_data )
{
    p->width = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry();
}

static void
set_height( GtkSpinButton* spin, gpointer user_data )
{
    p->height = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry();
}

static void
set_width_type(GtkWidget *item, gpointer bp)
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

    update_panel_geometry();
    RET();
}


GtkWidget *
mk_size()
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
transparency_toggle(GtkWidget *b, gpointer bp)
{
    gboolean t;

    ENTER;
    t = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b));
    gtk_widget_set_sensitive(tr_colorl, t);
    gtk_widget_set_sensitive(tr_colorb, t);

    //FIXME: Update background immediately.
    RET();
}

GtkWidget *
mk_transparency()
{
    GtkWidget *hbox, *hbox2, *label, *frame;

    ENTER;
    frame = gtk_frame_new(NULL);
    gtk_frame_set_shadow_type(GTK_FRAME (frame), GTK_SHADOW_NONE);
    gtk_container_set_border_width (GTK_CONTAINER (frame), 6);
    label = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL (label),_("<b>Transparency</b>"));
    gtk_frame_set_label_widget(GTK_FRAME (frame), label);

    hbox2 = gtk_hbox_new(FALSE, 0);
    gtk_container_set_border_width (GTK_CONTAINER (hbox2), 6);
    gtk_container_add (GTK_CONTAINER (frame), hbox2);

    hbox = gtk_hbox_new(FALSE, 0);
    gtk_box_pack_start(GTK_BOX (hbox2), hbox, FALSE, TRUE, 0);
    gtk_widget_set_size_request(hbox, 20, 1);

    hbox = gtk_hbox_new(FALSE, 5);
    gtk_box_pack_start(GTK_BOX (hbox2), hbox, FALSE, TRUE, 0);

    tr_checkb = gtk_check_button_new_with_label(_("Enable Transparency"));
    gtk_widget_show(tr_checkb);
    gtk_box_pack_start(GTK_BOX (hbox), tr_checkb, FALSE, FALSE, 0);
    update_toggle_button(tr_checkb, p->transparent);
    g_signal_connect(G_OBJECT(tr_checkb), "toggled", G_CALLBACK(transparency_toggle), NULL);
    //gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tr_checkb), FALSE);

    tr_colorl = gtk_label_new(_("Tint color:"));
    gtk_misc_set_alignment(GTK_MISC(tr_colorl), 0.0, 0.5);
    gtk_widget_show(tr_colorl);
    gtk_box_pack_start(GTK_BOX (hbox), tr_colorl, FALSE, FALSE, 5);
    //gtk_widget_set_sensitive(tr_colorl, FALSE);

    tr_colorb = gtk_color_button_new();
    gtk_color_button_set_use_alpha(GTK_COLOR_BUTTON(tr_colorb), TRUE);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON(tr_colorb), 65535/256*125);
    gtk_box_pack_start(GTK_BOX (hbox), tr_colorb, FALSE, FALSE, 0);
    gtk_color_button_set_color(GTK_COLOR_BUTTON(tr_colorb), &p->gtintcolor);
    gtk_color_button_set_alpha (GTK_COLOR_BUTTON(tr_colorb), 256*p->alpha);
    //gtk_widget_set_sensitive(tr_colorb, FALSE);

    RET(frame);
}

static void
set_dock_type(GtkToggleButton* toggle, gpointer user_data)
{
    p->setdocktype = gtk_toggle_button_get_active(toggle) ? 1 : 0;
    panel_set_dock_type( p );
    update_panel_geometry();
    /* FIXME: apparently, this doesn't work,
              but we don't know the reason yet! */
}

static void
set_struct(GtkToggleButton* toggle, gpointer user_data)
{
    p->setstrut = gtk_toggle_button_get_active(toggle) ? 1 : 0;
    update_panel_geometry();
}

GtkWidget *
mk_properties()
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

    prop_dt_checkb = gtk_check_button_new_with_label(_("Set Dock Type"));
    update_toggle_button(prop_dt_checkb, p->setdocktype);
    g_signal_connect( prop_dt_checkb, "toggled",
                      G_CALLBACK(set_dock_type), NULL );
    gtk_box_pack_start(GTK_BOX (vbox), prop_dt_checkb, FALSE, FALSE, 0);

    prop_st_checkb = gtk_check_button_new_with_label(_("Set Strut"));
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
    plugin* pl;

    if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
    {
        GtkTreeView* view = gtk_tree_selection_get_tree_view( tree_sel );
        GtkWidget *edit_btn = GTK_WIDGET(g_object_get_data( G_OBJECT(view), "edit_btn" ));
        gtk_tree_model_get( model, &it, 1, &pl, -1 );
        gtk_label_set_text( GTK_LABEL(label), _(pl->class->description) );
        gtk_widget_set_sensitive( edit_btn, pl->class->config != NULL );
    }
}

static void init_plugin_list( GtkTreeView* view, GtkWidget* label )
{
    /* extern panel *p; */
    GtkListStore* list;
    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    GtkTreeSelection* tree_sel;
    GList* l;
    GtkTreeIter it;

    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
            _("Currently loaded plugins"),
            render, "text", 0, NULL );
    gtk_tree_view_append_column( view, col );

    list = gtk_list_store_new( 2, G_TYPE_STRING, G_TYPE_POINTER );
    for( l = p->plugins; l; l = l->next )
    {
        GtkTreeIter it;
        plugin* pl = (plugin*)l->data;
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
    if( response == GTK_RESPONSE_OK )
    {
        GtkTreeView* view;
        GtkTreeSelection* tree_sel;
        GtkTreeIter it;
        GtkTreeModel* model;

        view = (GtkTreeView*)g_object_get_data( dlg, "avail-plugins" );
        tree_sel = gtk_tree_view_get_selection( view );
        if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        {
            char* type = NULL;
            plugin* pl;
            gtk_tree_model_get( model, &it, 1, &type, -1 );
            if( pl = plugin_load( type ) )
            {
                GtkTreePath* tree_path;

                pl->panel = p;
                plugin_start( pl, NULL );
                p->plugins = g_list_append(p->plugins, pl);
                /* FIXME: will show all cause problems? */
                gtk_widget_show_all( pl->pwid );

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

    classes = plugin_get_available_classes();

    parent_win = gtk_widget_get_toplevel( (GtkWidget*)_view );
    dlg = gtk_dialog_new_with_buttons( _("Add plugin to panel"),
                                       parent_win, 0,
                                       GTK_STOCK_CANCEL,
                                       GTK_RESPONSE_CANCEL,
                                       GTK_STOCK_ADD,
                                       GTK_RESPONSE_OK, NULL );
    /* gtk_widget_set_sensitive( parent_win, FALSE ); */
    scroll = gtk_scrolled_window_new( NULL, NULL );
    gtk_scrolled_window_set_shadow_type( (GtkScrolledWindow*)scroll,
                                          GTK_SHADOW_IN );
    gtk_scrolled_window_set_policy((GtkScrolledWindow*)scroll,
                                   GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC );
    gtk_box_pack_start( (GtkBox*)GTK_DIALOG(dlg)->vbox, scroll,
                         TRUE, TRUE, 4 );
    view = gtk_tree_view_new();
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
        plugin_class* pc = (plugin_class*)tmp->data;
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
    plugin* pl;

    if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
    {
        tree_path = gtk_tree_model_get_path( model, &it );
        gtk_tree_model_get( model, &it, 1, &pl, -1 );
        if( gtk_tree_path_get_indices(tree_path)[0] >= gtk_tree_model_iter_n_children( model, NULL ) )
            gtk_tree_path_prev( tree_path );
        gtk_list_store_remove( GTK_LIST_STORE(model), &it );
        p->plugins = g_list_remove( p->plugins, pl );
        plugin_stop( pl ); /* free the plugin widget & its data */
        plugin_put( pl ); /* free th lib if necessary */

        gtk_tree_selection_select_path( tree_sel, tree_path );
        gtk_tree_path_free( tree_path );
    }
}

void modify_plugin( GtkTreeView* view )
{
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection( view );
    GtkTreeModel* model;
    GtkTreeIter it;
    plugin* pl;

    if( ! gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        return;

    gtk_tree_model_get( model, &it, 1, &pl, -1 );
    if( pl->class->config )
        pl->class->config( pl, (GtkWindow*)gtk_widget_get_toplevel(GTK_WIDGET(view)) );
}

static int get_widget_index( plugin* pl )
{
    GList* l;
    int i;
    for( i = 0, l = p->plugins; l; l = l->next )
    {
        plugin* _pl = (plugin*)l->data;
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

    if( ! gtk_tree_model_get_iter_first( model, &it ) )
        return;
    if( gtk_tree_selection_iter_is_selected( tree_sel, &it ) )
        return;
    do{
        if( gtk_tree_selection_iter_is_selected(tree_sel, &it) )
        {
            plugin* pl;
            gtk_tree_model_get( model, &it, 1, &pl, -1 );
            gtk_list_store_move_before( GTK_LIST_STORE( model ),
                                        &it, &prev );

            i = 0;
            for( l = p->plugins; l; l = l->next, ++i )
            {
                if( l->data == pl  )
                {
                    p->plugins = g_list_insert( p->plugins, pl, i - 1);
                    p->plugins = g_list_delete_link( p->plugins, l);
                }
            }
            if( pl->pwid )
            {
                gtk_box_reorder_child( GTK_BOX(p->box), pl->pwid, get_widget_index( pl ) );
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
    plugin* pl;
    int i;

    if( ! gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        return;
    next = it;

    if( ! gtk_tree_model_iter_next( model, &next) )
        return;

    gtk_tree_model_get( model, &it, 1, &pl, -1 );

    gtk_list_store_move_after( GTK_LIST_STORE( model ), &it, &next );

    i = 0;
    for( l = p->plugins; l; l = l->next, ++i )
    {
        if( l->data == pl  )
        {
            p->plugins = g_list_insert( p->plugins, pl, i + 2);
            p->plugins = g_list_delete_link( p->plugins, l);
        }
    }
    if( pl->pwid )
    {
        gtk_box_reorder_child( GTK_BOX(p->box), pl->pwid, get_widget_index( pl ) );
    }
}

static GtkWidget *
mk_tab_plugins()
{
    GtkWidget *hbox, *vbox, *label;
    GtkWidget *scroll, *plugin_list, *button;

    hbox = gtk_hbox_new( FALSE, 2 );

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

    init_plugin_list( GTK_TREE_VIEW( plugin_list ), label );

    RET(hbox);
}

static GtkWidget *
mk_tab_general()
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
    frame = mk_position();
    gtk_box_pack_start(GTK_BOX (page), frame, FALSE, TRUE, 0);

    //size
    frame = mk_size();
    gtk_box_pack_start(GTK_BOX (page), frame, FALSE, TRUE, 0);

    //transparency
    frame = mk_transparency();
    gtk_box_pack_start(GTK_BOX (page), frame, FALSE, TRUE, 0);

    //properties
    frame = mk_properties();
    gtk_box_pack_start(GTK_BOX (page), frame, FALSE, TRUE, 0);
    /*
    gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW (sw), page);
    */
    RET(page);
}


static GtkWidget *
mk_dialog()
{
    GtkWidget *sw, *nb, *label;

    DBG("creating dialog\n");
    dialog = gtk_dialog_new_with_buttons (_("lxpanel configurator"),
          NULL,
          0, //GTK_DIALOG_DESTROY_WITH_PARENT,
          GTK_STOCK_CLOSE,
          GTK_RESPONSE_CLOSE,
          NULL);
    DBG("connecting sugnal to %p\n",  dialog);
    g_signal_connect (G_OBJECT(dialog), "response",     (GCallback) response_event,       NULL);
    g_signal_connect (G_OBJECT(dialog), "destroy",      (GCallback) dialog_destroy_event, NULL);
    g_signal_connect (G_OBJECT(dialog), "delete_event", (GCallback) dialog_delete_event,  NULL);
    gtk_window_set_modal(GTK_WINDOW(dialog), FALSE);
    gtk_window_set_position( GTK_WINDOW(dialog), GTK_WIN_POS_CENTER );

    /*
    gtk_window_set_skip_taskbar_hint(GTK_WINDOW(dialog), TRUE);
    gtk_window_set_skip_pager_hint(GTK_WINDOW(dialog), TRUE);
    */
    nb = gtk_notebook_new();
    gtk_notebook_set_show_border (GTK_NOTEBOOK(nb), FALSE);
    gtk_container_add (GTK_CONTAINER (GTK_DIALOG(dialog)->vbox), nb);

    sw = mk_tab_general();
    label = gtk_label_new(_("General"));
    gtk_misc_set_padding(GTK_MISC(label), 4, 1);
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), sw, label);

    sw = mk_tab_plugins();
    label = gtk_label_new(_("Plugins"));
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
configure(void)
{
    ENTER;
    if( ! dialog )
        dialog = mk_dialog();
    gtk_window_present((GtkWindow*)dialog);
    RET();
}

void
global_config_save(FILE *fp)
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

    lxpanel_put_str(fp, "LogoutCommand", p->logout_command );
    lxpanel_put_line(fp, "}\n");
}

void
plugin_config_save(FILE *fp)
{
    GList* l;
    for( l = p->plugins; l; l = l->next )
    {
        plugin* pl = (plugin*)l->data;
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

void restart(void)
{
    ENTER;
    RET();
}

void logout(void)
{
    const char* logout_command = p->logout_command;
    /* If LXSession is running, _LXSESSION_PID will be set */
    if( ! logout_command && getenv("_LXSESSION_PID") )
        logout_command = "lxsession -exit";

    if( logout_command ) {
        GError* err = NULL;
        if( ! g_spawn_command_line_async( logout_command, &err ) ) {
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
    g_free( *val );
    *val = g_strdup( gtk_entry_get_text(GTK_ENTRY(edit)) );
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
    GtkWidget* dlg = gtk_dialog_new_with_buttons( title, GTK_WINDOW(parent), 0,
                                                  GTK_STOCK_CLOSE,
                                                  GTK_RESPONSE_CLOSE,
                                                  NULL );

    /* this is a dirty hack.  We need to check if this response is GTK_RESPONSE_CLOSE or not. */
    g_signal_connect( dlg, "response", G_CALLBACK(gtk_widget_destroy), NULL );
    if( apply_func )
        g_object_set_data( G_OBJECT(dlg), "apply_func", apply_func );
    if( plugin )
        g_object_set_data( G_OBJECT(dlg), "plugin", plugin );

    gtk_box_set_spacing( GTK_BOX(GTK_DIALOG(dlg)->vbox), 4 );
    gtk_container_set_border_width( GTK_CONTAINER(GTK_DIALOG(dlg)->vbox), 8 );

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
    gtk_widget_show_all( dlg );
    return dlg;
}

