/**
 *
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

enum{
    COL_NAME,
    COL_EXPAND,
    COL_DATA,
    N_COLS
};

void panel_configure(Panel* p, int sel_page );
void restart(void);
void gtk_run(void);
void panel_config_save(Panel* panel);
static void logout(void);
static void save_global_config();

Command commands[] = {
    //{ "configure", N_("Preferences"), configure },
#ifndef DISABLE_MENU
    { "run", N_("Run"), gtk_run },
#endif
    { "restart", N_("Restart"), restart },
    { "logout", N_("Logout"), logout },
    { NULL, NULL },
};

static char* file_manager_cmd = NULL;
static char* terminal_cmd = NULL;
static char* logout_cmd = NULL;

extern GSList* all_panels;
extern gchar *cprofile;
extern int config;

/* GtkColotButton expects a number between 0 and 65535, but p->alpha has range
 * 0 to 255, and (2^(2n) - 1) / (2^n - 1) = 2^n + 1 = 257, with n = 8. */
static guint16 const alpha_scale_factor = 257;

void panel_global_config_save( Panel* p, FILE *fp);
void panel_plugin_config_save( Panel* p, FILE *fp);

static void update_opt_menu(GtkWidget *w, int ind);
static void update_toggle_button(GtkWidget *w, gboolean n);
static void modify_plugin( GtkTreeView* view );
static gboolean on_entry_focus_out( GtkWidget* edit, GdkEventFocus *evt, gpointer user_data );

static void
response_event(GtkDialog *widget, gint arg1, Panel* panel )
{
    switch (arg1) {
    /* FIXME: what will happen if the user exit lxpanel without
              close this config dialog?
              Then the config won't be save, I guess. */
    case GTK_RESPONSE_DELETE_EVENT:
    case GTK_RESPONSE_CLOSE:
    case GTK_RESPONSE_NONE:
        panel_config_save( panel );
        /* NOTE: NO BREAK HERE*/
        gtk_widget_destroy(GTK_WIDGET(widget));
        break;
    }
    return;
}

static void
update_panel_geometry( Panel* p )
{
    /* Guard against being called early in panel creation. */
    if (p->topgwin != NULL)
    {
        calculate_position(p);
        gtk_widget_set_size_request(p->topgwin, p->aw, p->ah);
        gdk_window_move(p->topgwin->window, p->ax, p->ay);
        panel_update_background(p);
        panel_establish_autohide(p);
        panel_set_wm_strut(p);
    }
}

static gboolean edge_selector(Panel* p, int edge)
{
    return (p->edge == edge);
}

/* If there is a panel on this edge and it is not the panel being configured, set the edge unavailable. */
gboolean panel_edge_available(Panel* p, int edge)
{
    GSList* l;
    for (l = all_panels; l != NULL; l = l->next)
        {
        Panel* pl = (Panel*) l->data;
        if ((pl != p) && (pl->edge == edge))
            return FALSE;
        }
    return TRUE;
}

static void set_edge(Panel* p, int edge)
{
    p->edge = edge;
    update_panel_geometry(p);
    panel_set_panel_configuration_changed(p);
}

static void edge_bottom_toggle(GtkToggleButton *widget, Panel *p)
{
    if (gtk_toggle_button_get_active(widget))
        set_edge(p, EDGE_BOTTOM);
}

static void edge_top_toggle(GtkToggleButton *widget, Panel *p)
{
    if (gtk_toggle_button_get_active(widget))
        set_edge(p, EDGE_TOP);
}

static void edge_left_toggle(GtkToggleButton *widget, Panel *p)
{
    if (gtk_toggle_button_get_active(widget))
        set_edge(p, EDGE_LEFT);
}

static void edge_right_toggle(GtkToggleButton *widget, Panel *p)
{
    if (gtk_toggle_button_get_active(widget))
        set_edge(p, EDGE_RIGHT);
}

static void set_alignment(Panel* p, int align)
{
    if (p->margin_control) 
        gtk_widget_set_sensitive(p->margin_control, (align != ALLIGN_CENTER));
    p->allign = align;
    update_panel_geometry(p);
}

static void align_left_toggle(GtkToggleButton *widget, Panel *p)
{
    if (gtk_toggle_button_get_active(widget))
        set_alignment(p, ALLIGN_LEFT);
}

static void align_center_toggle(GtkToggleButton *widget, Panel *p)
{
    if (gtk_toggle_button_get_active(widget))
        set_alignment(p, ALLIGN_CENTER);
}

static void align_right_toggle(GtkToggleButton *widget, Panel *p)
{
    if (gtk_toggle_button_get_active(widget))
        set_alignment(p, ALLIGN_RIGHT);
}

static void
set_margin( GtkSpinButton* spin,  Panel* p  )
{
    p->margin = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry(p);
}

static void
set_width(  GtkSpinButton* spin, Panel* p )
{
    p->width = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry(p);
}

static void
set_height( GtkSpinButton* spin, Panel* p )
{
    p->height = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry(p);
}

static void set_width_type( GtkWidget *item, Panel* p )
{
    GtkWidget* spin;
    int widthtype;
    gboolean t;
    widthtype = gtk_combo_box_get_active(GTK_COMBO_BOX(item)) + 1;
    p->widthtype = widthtype;

    spin = (GtkWidget*)g_object_get_data(G_OBJECT(item), "width_spin" );
    t = (widthtype != WIDTH_REQUEST);
    gtk_widget_set_sensitive( spin, t );
    if (widthtype == WIDTH_PERCENT)
    {
        gtk_spin_button_set_range( (GtkSpinButton*)spin, 0, 100 );
        gtk_spin_button_set_value( (GtkSpinButton*)spin, 100 );
    }
    else if (widthtype == WIDTH_PIXEL)
    {
        if ((p->edge == EDGE_TOP) || (p->edge == EDGE_BOTTOM))
        {
            gtk_spin_button_set_range( (GtkSpinButton*)spin, 0, gdk_screen_width() );
            gtk_spin_button_set_value( (GtkSpinButton*)spin, gdk_screen_width() );
        }
        else
        {
            gtk_spin_button_set_range( (GtkSpinButton*)spin, 0, gdk_screen_height() );
            gtk_spin_button_set_value( (GtkSpinButton*)spin, gdk_screen_height() );
        }
    } else
        return;

    update_panel_geometry(p);
}

static void set_log_level( GtkWidget *cbox, Panel* p)
{
    configured_log_level = gtk_combo_box_get_active(GTK_COMBO_BOX(cbox));
    if (!log_level_set_on_commandline)
        log_level = configured_log_level;
    ERR("panel-pref: log level configured to %d, log_level is now %d\n",
            configured_log_level, log_level);
}

static void transparency_toggle( GtkWidget *b, Panel* p)
{
    GtkWidget* tr = (GtkWidget*)g_object_get_data(G_OBJECT(b), "tint_clr");
    gboolean t;

    ENTER;

    t = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b));
    gtk_widget_set_sensitive(tr, t);
/*
    gtk_widget_set_sensitive(tr_colorl, t);
    gtk_widget_set_sensitive(tr_colorb, t);
*/
    /* Update background immediately. */
    if (t&&!p->transparent) {
        p->transparent = 1;
        p->background = 0;
        panel_update_background( p );
    }
    RET();
}

static void background_file_helper(Panel * p, GtkWidget * toggle, GtkFileChooser * file_chooser)
{
    char * file = g_strdup(gtk_file_chooser_get_filename(file_chooser));
    if (file != NULL)
    {
        g_free(p->background_file);
        p->background_file = file;
    }

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle)))
    {
        if ( ! p->background)
        {
            p->transparent = FALSE;
            p->background = TRUE;
            panel_update_background(p);
        }
    }
}

static void background_toggle( GtkWidget *b, Panel* p)
{
    GtkWidget * fc = (GtkWidget*) g_object_get_data(G_OBJECT(b), "img_file");
    gtk_widget_set_sensitive(fc, gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(b)));
    background_file_helper(p, b, GTK_FILE_CHOOSER(fc));
}

static void background_changed(GtkFileChooser *file_chooser,  Panel* p )
{
    GtkWidget * btn = GTK_WIDGET(g_object_get_data(G_OBJECT(file_chooser), "bg_image"));
    background_file_helper(p, btn, file_chooser);
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
        }
    }

    RET();
}

static void
on_font_color_set( GtkColorButton* clr,  Panel* p )
{
    gtk_color_button_get_color( clr, &p->gfontcolor );
    panel_set_panel_configuration_changed(p);
}

static void
on_tint_color_set( GtkColorButton* clr,  Panel* p )
{
    gtk_color_button_get_color( clr, &p->gtintcolor );
    p->tintcolor = gcolor2rgb24(&p->gtintcolor);
    p->alpha = gtk_color_button_get_alpha( clr ) / alpha_scale_factor;
    panel_update_background( p );
}

static void
on_use_font_color_toggled( GtkToggleButton* btn,   Panel* p )
{
    GtkWidget* clr = (GtkWidget*)g_object_get_data( G_OBJECT(btn), "clr" );
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)))
        gtk_widget_set_sensitive( clr, TRUE );
    else
        gtk_widget_set_sensitive( clr, FALSE );
    p->usefontcolor = gtk_toggle_button_get_active( btn );
    panel_set_panel_configuration_changed(p);
}

static void
on_font_size_set( GtkSpinButton* spin, Panel* p )
{
    p->fontsize = (int)gtk_spin_button_get_value(spin);
    panel_set_panel_configuration_changed(p);
}

static void
on_use_font_size_toggled( GtkToggleButton* btn,   Panel* p )
{
    GtkWidget* clr = (GtkWidget*)g_object_get_data( G_OBJECT(btn), "clr" );
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn)))
        gtk_widget_set_sensitive( clr, TRUE );
    else
        gtk_widget_set_sensitive( clr, FALSE );
    p->usefontsize = gtk_toggle_button_get_active( btn );
    panel_set_panel_configuration_changed(p);
}


static void
set_dock_type(GtkToggleButton* toggle,  Panel* p )
{
    p->setdocktype = gtk_toggle_button_get_active(toggle) ? 1 : 0;
    panel_set_dock_type( p );
    update_panel_geometry(p);
}

static void
set_strut(GtkToggleButton* toggle,  Panel* p )
{
    p->setstrut = gtk_toggle_button_get_active(toggle) ? 1 : 0;
    update_panel_geometry(p);
}

static void
set_autohide(GtkToggleButton* toggle,  Panel* p )
{
    p->autohide = gtk_toggle_button_get_active(toggle) ? 1 : 0;
    update_panel_geometry(p);
}

static void
set_height_when_minimized( GtkSpinButton* spin,  Panel* p  )
{
    p->height_when_hidden = (int)gtk_spin_button_get_value(spin);
    update_panel_geometry(p);
}

static void
set_icon_size( GtkSpinButton* spin,  Panel* p  )
{
    p->icon_size = (int)gtk_spin_button_get_value(spin);
    panel_set_panel_configuration_changed(p);
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
        gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );
        gtk_label_set_text( GTK_LABEL(label), _(pl->class->description) );
        gtk_widget_set_sensitive( edit_btn, pl->class->config != NULL );
    }
}

static void
on_plugin_expand_toggled(GtkCellRendererToggle* render, char* path, GtkTreeView* view)
{
    GtkTreeModel* model;
    GtkTreeIter it;
    GtkTreePath* tp = gtk_tree_path_new_from_string( path );
    model = gtk_tree_view_get_model( view );
    if( gtk_tree_model_get_iter( model, &it, tp ) )
    {
        Plugin* pl;
        gboolean old_expand, expand, fill;
        guint padding;
        GtkPackType pack_type;

        gtk_tree_model_get( model, &it, COL_DATA, &pl, COL_EXPAND, &expand, -1 );

        if (pl->class->expand_available)
        {
            /* Only honor "stretch" if allowed by the plugin. */
            expand = ! expand;
            pl->expand = expand;
            gtk_list_store_set( (GtkListStore*)model, &it, COL_EXPAND, expand, -1 );

            /* Query the old packing of the plugin widget.
             * Apply the new packing with only "expand" modified. */
            gtk_box_query_child_packing( GTK_BOX(pl->panel->box), pl->pwid, &old_expand, &fill, &padding, &pack_type );
            gtk_box_set_child_packing( GTK_BOX(pl->panel->box), pl->pwid, expand, fill, padding, pack_type );
        }
    }
    gtk_tree_path_free( tp );
}

static void on_stretch_render(GtkTreeViewColumn * column, GtkCellRenderer * renderer, GtkTreeModel * model, GtkTreeIter * iter, gpointer data)
{
    /* Set the control visible depending on whether stretch is available for the plugin.
     * The g_object_set method is touchy about its parameter, so we can't pass the boolean directly. */
    Plugin * pl;
    gtk_tree_model_get(model, iter, COL_DATA, &pl, -1);
    g_object_set(renderer,
        "visible", ((pl->class->expand_available) ? TRUE : FALSE),
        NULL);
}

static void init_plugin_list( Panel* p, GtkTreeView* view, GtkWidget* label )
{
    GtkListStore* list;
    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    GtkTreeSelection* tree_sel;
    GList* l;
    GtkTreeIter it;

    g_object_set_data( G_OBJECT(view), "panel", p );

    render = gtk_cell_renderer_text_new();
    col = gtk_tree_view_column_new_with_attributes(
            _("Currently loaded plugins"),
            render, "text", COL_NAME, NULL );
    gtk_tree_view_column_set_expand( col, TRUE );
    gtk_tree_view_append_column( view, col );

    render = gtk_cell_renderer_toggle_new();
    g_object_set( render, "activatable", TRUE, NULL );
    g_signal_connect( render, "toggled", G_CALLBACK( on_plugin_expand_toggled ), view );
    col = gtk_tree_view_column_new_with_attributes(
            _("Stretch"),
            render, "active", COL_EXPAND, NULL );
    gtk_tree_view_column_set_expand( col, FALSE );
    gtk_tree_view_column_set_cell_data_func(col, render, on_stretch_render, NULL, NULL);
    gtk_tree_view_append_column( view, col );

    list = gtk_list_store_new( N_COLS, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_POINTER );
    for( l = p->plugins; l; l = l->next )
    {
        GtkTreeIter it;
        Plugin* pl = (Plugin*)l->data;
        gtk_list_store_append( list, &it );
        gtk_list_store_set( list, &it,
                            COL_NAME, _(pl->class->name),
                            COL_EXPAND, pl->expand,
                            COL_DATA, pl,
                            -1);
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

static void on_add_plugin_row_activated( GtkTreeView *tree_view, 
                                         GtkTreePath *path, 
                                         GtkTreeViewColumn *col, 
                                         gpointer user_data) 
{
    GtkWidget *dlg;

    dlg = (GtkWidget *) user_data; 

    (void) tree_view;
    (void) path;
    (void) col;

    /* Emitting the "response" signal ourselves. */
    gtk_dialog_response(GTK_DIALOG(dlg), GTK_RESPONSE_OK);
}

static void on_add_plugin_response( GtkDialog* dlg,
                                    int response,
                                    GtkTreeView* _view )
{
    Panel* p = (Panel*) g_object_get_data( G_OBJECT(_view), "panel" );
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
            if ((pl = plugin_load(type)) != NULL)
            {
                GtkTreePath* tree_path;

                pl->panel = p;
                if (pl->class->expand_default) pl->expand = TRUE;
                plugin_start( pl, NULL );
                p->plugins = g_list_append(p->plugins, pl);
                panel_config_save(p);

                if (pl->pwid)
                {
                    gtk_widget_show(pl->pwid);

                    /* update background of the newly added plugin */
                    plugin_widget_set_background( pl->pwid, pl->panel );
                }

                model = gtk_tree_view_get_model( _view );
                gtk_list_store_append( (GtkListStore*)model, &it );
                gtk_list_store_set( (GtkListStore*)model, &it,
                                    COL_NAME, _(pl->class->name),
                                    COL_EXPAND, pl->expand,
                                    COL_DATA, pl, -1 );
                tree_sel = gtk_tree_view_get_selection( _view );
                gtk_tree_selection_select_iter( tree_sel, &it );
                if ((tree_path = gtk_tree_model_get_path(model, &it)) != NULL)
                {
                    gtk_tree_view_scroll_to_cell( _view, tree_path, NULL, FALSE, 0, 0 );
                    gtk_tree_path_free( tree_path );
                }
            }
            g_free( type );
        }
    }
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

    Panel* p = (Panel*) g_object_get_data( G_OBJECT(_view), "panel" );

    classes = plugin_get_available_classes();

    parent_win = gtk_widget_get_toplevel( (GtkWidget*)_view );
    dlg = gtk_dialog_new_with_buttons( _("Add plugin to panel"),
                                       GTK_WINDOW(parent_win), 0,
                                       GTK_STOCK_CANCEL,
                                       GTK_RESPONSE_CANCEL,
                                       GTK_STOCK_ADD,
                                       GTK_RESPONSE_OK, NULL );
    panel_apply_icon(GTK_WINDOW(dlg));

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
    gtk_container_add( (GtkContainer*)scroll, GTK_WIDGET(view) );
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

    /* Populate list of available plugins.
     * Omit plugins that can only exist once per system if it is already configured. */
    for( tmp = classes; tmp; tmp = tmp->next ) {
        PluginClass* pc = (PluginClass*)tmp->data;
        if (( ! pc->one_per_system ) || ( ! pc->one_per_system_instantiated))
        {
            GtkTreeIter it;
            gtk_list_store_append( list, &it );
            gtk_list_store_set( list, &it,
                                0, _(pc->name),
                                1, pc->type,
                                -1 );
            /* g_debug( "%s (%s)", pc->type, _(pc->name) ); */
        }
    }

    gtk_tree_view_set_model( view, GTK_TREE_MODEL(list) );
    g_object_unref( list );

    /* 
     * The user can add a plugin either by clicking the "Add" button, or by
     * double-clicking the plugin.
     */
    g_signal_connect( dlg, "response",
                      G_CALLBACK(on_add_plugin_response), _view );
    g_signal_connect( view, "row-activated",
                      G_CALLBACK(on_add_plugin_row_activated), (gpointer) dlg);

    g_object_set_data( G_OBJECT(dlg), "avail-plugins", view );
    g_object_weak_ref( G_OBJECT(dlg), (GWeakNotify) plugin_class_list_free, classes );

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

    Panel* p = (Panel*) g_object_get_data( G_OBJECT(view), "panel" );

    if( gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
    {
        tree_path = gtk_tree_model_get_path( model, &it );
        gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );
        if( gtk_tree_path_get_indices(tree_path)[0] >= gtk_tree_model_iter_n_children( model, NULL ) )
            gtk_tree_path_prev( tree_path );
        gtk_list_store_remove( GTK_LIST_STORE(model), &it );
        gtk_tree_selection_select_path( tree_sel, tree_path );
        gtk_tree_path_free( tree_path );

        p->plugins = g_list_remove( p->plugins, pl );
        plugin_delete(pl);
        panel_config_save(p);
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

    gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );
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

    Panel* panel = (Panel*) g_object_get_data( G_OBJECT(view), "panel" );

    if( ! gtk_tree_model_get_iter_first( model, &it ) )
        return;
    if( gtk_tree_selection_iter_is_selected( tree_sel, &it ) )
        return;
    do{
        if( gtk_tree_selection_iter_is_selected(tree_sel, &it) )
        {
            Plugin* pl;
            gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );
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
            panel_config_save(panel);
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

    Panel* panel = (Panel*) g_object_get_data( G_OBJECT(view), "panel" );

    if( ! gtk_tree_selection_get_selected( tree_sel, &model, &it ) )
        return;
    next = it;

    if( ! gtk_tree_model_iter_next( model, &next) )
        return;

    gtk_tree_model_get( model, &it, COL_DATA, &pl, -1 );

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
    panel_config_save(panel);
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

void panel_configure( Panel* p, int sel_page )
{
    GtkBuilder* builder;
    GtkWidget *w, *w2, *tint_clr;

    if( p->pref_dialog )
    {
        panel_adjust_geometry_terminology(p);
        gtk_window_present(GTK_WINDOW(p->pref_dialog));
        return;
    }

    builder = gtk_builder_new();
    if( !gtk_builder_add_from_file(builder, PACKAGE_UI_DIR "/panel-pref.ui", NULL) )
    {
        g_object_unref(builder);
        return;
    }

    p->pref_dialog = (GtkWidget*)gtk_builder_get_object( builder, "panel_pref" );
    g_signal_connect(p->pref_dialog, "response", (GCallback) response_event, p);
    g_object_add_weak_pointer( G_OBJECT(p->pref_dialog), (gpointer) &p->pref_dialog );
    gtk_window_set_position( (GtkWindow*)p->pref_dialog, GTK_WIN_POS_CENTER );
    panel_apply_icon(GTK_WINDOW(p->pref_dialog));

    /* position */
    w = (GtkWidget*)gtk_builder_get_object( builder, "edge_bottom" );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), edge_selector(p, EDGE_BOTTOM));
    gtk_widget_set_sensitive(w, panel_edge_available(p, EDGE_BOTTOM));
    g_signal_connect(w, "toggled", G_CALLBACK(edge_bottom_toggle), p);
    w = (GtkWidget*)gtk_builder_get_object( builder, "edge_top" );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), edge_selector(p, EDGE_TOP));
    gtk_widget_set_sensitive(w, panel_edge_available(p, EDGE_TOP));
    g_signal_connect(w, "toggled", G_CALLBACK(edge_top_toggle), p);
    w = (GtkWidget*)gtk_builder_get_object( builder, "edge_left" );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), edge_selector(p, EDGE_LEFT));
    gtk_widget_set_sensitive(w, panel_edge_available(p, EDGE_LEFT));
    g_signal_connect(w, "toggled", G_CALLBACK(edge_left_toggle), p);
    w = (GtkWidget*)gtk_builder_get_object( builder, "edge_right" );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), edge_selector(p, EDGE_RIGHT));
    gtk_widget_set_sensitive(w, panel_edge_available(p, EDGE_RIGHT));
    g_signal_connect(w, "toggled", G_CALLBACK(edge_right_toggle), p);

    /* alignment */
    p->alignment_left_label = w = (GtkWidget*)gtk_builder_get_object( builder, "alignment_left" );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (p->allign == ALLIGN_LEFT));
    g_signal_connect(w, "toggled", G_CALLBACK(align_left_toggle), p);
    w = (GtkWidget*)gtk_builder_get_object( builder, "alignment_center" );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (p->allign == ALLIGN_CENTER));
    g_signal_connect(w, "toggled", G_CALLBACK(align_center_toggle), p);
    p->alignment_right_label = w = (GtkWidget*)gtk_builder_get_object( builder, "alignment_right" );
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), (p->allign == ALLIGN_RIGHT));
    g_signal_connect(w, "toggled", G_CALLBACK(align_right_toggle), p);

    /* margin */
    p->margin_control = w = (GtkWidget*)gtk_builder_get_object( builder, "margin" );
    gtk_spin_button_set_value( (GtkSpinButton*)w, p->margin );
    gtk_widget_set_sensitive(p->margin_control, (p->allign != ALLIGN_CENTER));
    g_signal_connect( w, "value-changed",
                      G_CALLBACK(set_margin), p);

    /* size */
    p->width_label = (GtkWidget*)gtk_builder_get_object( builder, "width_label");
    p->width_control = w = (GtkWidget*)gtk_builder_get_object( builder, "width" );
    gtk_widget_set_sensitive( w, p->widthtype != WIDTH_REQUEST );
    gint upper = 0;
    if( p->widthtype == WIDTH_PERCENT)
        upper = 100;
    else if( p->widthtype == WIDTH_PIXEL)
        upper = (((p->edge == EDGE_TOP) || (p->edge == EDGE_BOTTOM)) ? gdk_screen_width() : gdk_screen_height());
    gtk_spin_button_set_range( (GtkSpinButton*)w, 0, upper );
    gtk_spin_button_set_value( (GtkSpinButton*)w, p->width );
    g_signal_connect( w, "value-changed", G_CALLBACK(set_width), p );

    w = (GtkWidget*)gtk_builder_get_object( builder, "width_unit" );
    update_opt_menu( w, p->widthtype - 1 );
    g_object_set_data(G_OBJECT(w), "width_spin", p->width_control );
    g_signal_connect( w, "changed",
                     G_CALLBACK(set_width_type), p);

    p->height_label = (GtkWidget*)gtk_builder_get_object( builder, "height_label");
    p->height_control = w = (GtkWidget*)gtk_builder_get_object( builder, "height" );
    gtk_spin_button_set_range( (GtkSpinButton*)w, PANEL_HEIGHT_MIN, PANEL_HEIGHT_MAX );
    gtk_spin_button_set_value( (GtkSpinButton*)w, p->height );
    g_signal_connect( w, "value-changed", G_CALLBACK(set_height), p );

    w = (GtkWidget*)gtk_builder_get_object( builder, "height_unit" );
    update_opt_menu( w, HEIGHT_PIXEL - 1);

    w = (GtkWidget*)gtk_builder_get_object( builder, "icon_size" );
    gtk_spin_button_set_range( (GtkSpinButton*)w, PANEL_HEIGHT_MIN, PANEL_HEIGHT_MAX );
    gtk_spin_button_set_value( (GtkSpinButton*)w, p->icon_size );
    g_signal_connect( w, "value_changed", G_CALLBACK(set_icon_size), p );

    /* properties */

    /* Explaination from Ruediger Arp <ruediger@gmx.net>:
        "Set Dock Type", it is referring to the behaviour of
        dockable applications such as those found in WindowMaker (e.g.
        http://www.cs.mun.ca/~gstarkes/wmaker/dockapps ) and other
        lightweight window managers. These dockapps are probably being
        treated in some special way.
    */
    w = (GtkWidget*)gtk_builder_get_object( builder, "as_dock" );
    update_toggle_button( w, p->setdocktype );
    g_signal_connect( w, "toggled",
                      G_CALLBACK(set_dock_type), p );

    /* Explaination from Ruediger Arp <ruediger@gmx.net>:
        "Set Strut": Reserve panel's space so that it will not be
        covered by maximazied windows.
        This is clearly an option to avoid the panel being
        covered/hidden by other applications so that it always is
        accessible. The panel "steals" some screen estate which cannot
        be accessed by other applications.
        GNOME Panel acts this way, too.
    */
    w = (GtkWidget*)gtk_builder_get_object( builder, "reserve_space" );
    update_toggle_button( w, p->setstrut );
    g_signal_connect( w, "toggled",
                      G_CALLBACK(set_strut), p );

    w = (GtkWidget*)gtk_builder_get_object( builder, "autohide" );
    update_toggle_button( w, p->autohide );
    g_signal_connect( w, "toggled",
                      G_CALLBACK(set_autohide), p );

    w = (GtkWidget*)gtk_builder_get_object( builder, "height_when_minimized" );
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(w), p->height_when_hidden);
    g_signal_connect( w, "value-changed",
                      G_CALLBACK(set_height_when_minimized), p);

    /* transparancy */
    tint_clr = w = (GtkWidget*)gtk_builder_get_object( builder, "tint_clr" );
    gtk_color_button_set_color((GtkColorButton*)w, &p->gtintcolor);
    gtk_color_button_set_alpha((GtkColorButton*)w, alpha_scale_factor * p->alpha);
    if ( ! p->transparent )
        gtk_widget_set_sensitive( w, FALSE );
    g_signal_connect( w, "color-set", G_CALLBACK( on_tint_color_set ), p );

    /* background */
    {
        GtkWidget* none, *trans, *img;
        none = (GtkWidget*)gtk_builder_get_object( builder, "bg_none" );
        trans = (GtkWidget*)gtk_builder_get_object( builder, "bg_transparency" );
        img = (GtkWidget*)gtk_builder_get_object( builder, "bg_image" );

        g_object_set_data(G_OBJECT(trans), "tint_clr", tint_clr);

        if (p->background)
            gtk_toggle_button_set_active( (GtkToggleButton*)img, TRUE);
        else if (p->transparent)
            gtk_toggle_button_set_active( (GtkToggleButton*)trans, TRUE);
        else
            gtk_toggle_button_set_active( (GtkToggleButton*)none, TRUE);

        g_signal_connect(none, "toggled", G_CALLBACK(background_disable_toggle), p);
        g_signal_connect(trans, "toggled", G_CALLBACK(transparency_toggle), p);
        g_signal_connect(img, "toggled", G_CALLBACK(background_toggle), p);

        w = (GtkWidget*)gtk_builder_get_object( builder, "img_file" );
        g_object_set_data(G_OBJECT(img), "img_file", w);
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(w),
            ((p->background_file != NULL) ? p->background_file : gtk_icon_info_get_filename(gtk_icon_theme_lookup_icon(p->icon_theme, "lxpanel-backkground", 0, 0))));

        if (!p->background)
            gtk_widget_set_sensitive( w, FALSE);
        g_object_set_data( G_OBJECT(w), "bg_image", img );
        g_signal_connect( w, "file-set", G_CALLBACK (background_changed), p);
    }

    /* font color */
    w = (GtkWidget*)gtk_builder_get_object( builder, "font_clr" );
    gtk_color_button_set_color( (GtkColorButton*)w, &p->gfontcolor );
    g_signal_connect( w, "color-set", G_CALLBACK( on_font_color_set ), p );

    w2 = (GtkWidget*)gtk_builder_get_object( builder, "use_font_clr" );
    gtk_toggle_button_set_active( (GtkToggleButton*)w2, p->usefontcolor );
    g_object_set_data( G_OBJECT(w2), "clr", w );
    g_signal_connect(w2, "toggled", G_CALLBACK(on_use_font_color_toggled), p);
    if( ! p->usefontcolor )
        gtk_widget_set_sensitive( w, FALSE );

    /* font size */
    w = (GtkWidget*)gtk_builder_get_object( builder, "font_size" );
    gtk_spin_button_set_value( (GtkSpinButton*)w, p->fontsize );
    g_signal_connect( w, "value-changed",
                      G_CALLBACK(on_font_size_set), p);

    w2 = (GtkWidget*)gtk_builder_get_object( builder, "use_font_size" );
    gtk_toggle_button_set_active( (GtkToggleButton*)w2, p->usefontsize );
    g_object_set_data( G_OBJECT(w2), "clr", w );
    g_signal_connect(w2, "toggled", G_CALLBACK(on_use_font_size_toggled), p);
    if( ! p->usefontsize )
        gtk_widget_set_sensitive( w, FALSE );

    /* plugin list */
    {
        GtkWidget* plugin_list = (GtkWidget*)gtk_builder_get_object( builder, "plugin_list" );

        /* buttons used to edit plugin list */
        w = (GtkWidget*)gtk_builder_get_object( builder, "add_btn" );
        g_signal_connect( w, "clicked", G_CALLBACK(on_add_plugin), plugin_list );

        w = (GtkWidget*)gtk_builder_get_object( builder, "edit_btn" );
        g_signal_connect_swapped( w, "clicked", G_CALLBACK(modify_plugin), plugin_list );
        g_object_set_data( G_OBJECT(plugin_list), "edit_btn", w );

        w = (GtkWidget*)gtk_builder_get_object( builder, "remove_btn" );
        g_signal_connect( w, "clicked", G_CALLBACK(on_remove_plugin), plugin_list );
        w = (GtkWidget*)gtk_builder_get_object( builder, "moveup_btn" );
        g_signal_connect( w, "clicked", G_CALLBACK(on_moveup_plugin), plugin_list );
        w = (GtkWidget*)gtk_builder_get_object( builder, "movedown_btn" );
        g_signal_connect( w, "clicked", G_CALLBACK(on_movedown_plugin), plugin_list );

        w = (GtkWidget*)gtk_builder_get_object( builder, "plugin_desc" );
        init_plugin_list( p, (GtkTreeView*)plugin_list, w );
    }
    /* advanced, applications */
    w = (GtkWidget*)gtk_builder_get_object( builder, "file_manager" );
    if (file_manager_cmd)
        gtk_entry_set_text( (GtkEntry*)w, file_manager_cmd );
    g_signal_connect( w, "focus-out-event",
                      G_CALLBACK(on_entry_focus_out),
                      &file_manager_cmd);

    w = (GtkWidget*)gtk_builder_get_object( builder, "term" );
    if (terminal_cmd)
        gtk_entry_set_text( (GtkEntry*)w, terminal_cmd );
    g_signal_connect( w, "focus-out-event",
                      G_CALLBACK(on_entry_focus_out),
                      &terminal_cmd);

    /* If we are under LXSession, setting logout command is not necessary. */
    w = (GtkWidget*)gtk_builder_get_object( builder, "logout" );
    if( getenv("_LXSESSION_PID") ) {
        gtk_widget_hide( w );
        w = (GtkWidget*)gtk_builder_get_object( builder, "logout_label" );
        gtk_widget_hide( w );
    }
    else {
        if(logout_cmd)
            gtk_entry_set_text( (GtkEntry*)w, logout_cmd );
        g_signal_connect( w, "focus-out-event",
                        G_CALLBACK(on_entry_focus_out),
                        &logout_cmd);
    }

    w = (GtkWidget*)gtk_builder_get_object( builder, "log_level" );
    update_opt_menu(w, configured_log_level);
    g_signal_connect(w, "changed", G_CALLBACK(set_log_level), p);


    panel_adjust_geometry_terminology(p);
    gtk_widget_show(GTK_WIDGET(p->pref_dialog));
    w = (GtkWidget*)gtk_builder_get_object( builder, "notebook" );
    gtk_notebook_set_current_page( (GtkNotebook*)w, sel_page );

    g_object_unref(builder);
}

void
panel_global_config_save( Panel* p, FILE *fp)
{
    fprintf(fp, "# lxpanel <profile> config file. Manually editing is not recommended.\n"
                "# Use preference dialog in lxpanel to adjust config when you can.\n\n");
    lxpanel_put_line(fp, "Global {");
    lxpanel_put_str(fp, "edge", num2str(edge_pair, p->edge, "none"));
    lxpanel_put_str(fp, "allign", num2str(allign_pair, p->allign, "none"));
    lxpanel_put_int(fp, "margin", p->margin);
    lxpanel_put_str(fp, "widthtype", num2str(width_pair, p->widthtype, "none"));
    lxpanel_put_int(fp, "width", p->width);
    lxpanel_put_int(fp, "height", p->height);
    lxpanel_put_bool(fp, "transparent", p->transparent );
    lxpanel_put_line(fp, "tintcolor=#%06x", gcolor2rgb24(&p->gtintcolor));
    lxpanel_put_int(fp, "alpha", p->alpha);
    lxpanel_put_bool(fp, "autohide", p->autohide);
    lxpanel_put_int(fp, "heightwhenhidden", p->height_when_hidden);
    lxpanel_put_bool(fp, "setdocktype", p->setdocktype);
    lxpanel_put_bool(fp, "setpartialstrut", p->setstrut);
    lxpanel_put_bool(fp, "usefontcolor", p->usefontcolor);
    lxpanel_put_int(fp, "fontsize", p->fontsize);
    lxpanel_put_line(fp, "fontcolor=#%06x", gcolor2rgb24(&p->gfontcolor));
    lxpanel_put_bool(fp, "usefontsize", p->usefontsize);
    lxpanel_put_bool(fp, "background", p->background );
    lxpanel_put_str(fp, "backgroundfile", p->background_file);
    lxpanel_put_int(fp, "iconsize", p->icon_size);
    lxpanel_put_int(fp, "loglevel", configured_log_level);
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

    /* ensure the 'panels' dir exists */
    if( ! g_file_test( dir, G_FILE_TEST_EXISTS ) )
        g_mkdir_with_parents( dir, 0755 );
    g_free( dir );

    if (!(fp = fopen(fname, "w"))) {
        ERR("can't open for write %s:", fname);
        g_free( fname );
        perror(NULL);
        RET();
    }
    panel_global_config_save(p, fp);
    panel_plugin_config_save(p, fp);
    fclose(fp);
    g_free( fname );

    /* save the global config file */
    save_global_config();
    p->config_changed = 0;
}

void restart(void)
{
    /* This is defined in panel.c */
    extern gboolean is_restarting;
    ENTER;
    is_restarting = TRUE;

    gtk_main_quit();
    RET();
}

void logout(void)
{
    const char* l_logout_cmd = logout_cmd;
    /* If LXSession is running, _LXSESSION_PID will be set */
    if( ! l_logout_cmd && getenv("_LXSESSION_PID") )
        l_logout_cmd = "lxsession-logout";

    if( l_logout_cmd ) {
        GError* err = NULL;
        if( ! g_spawn_command_line_async( l_logout_cmd, &err ) ) {
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
        (*apply_func)( g_object_get_data(G_OBJECT(dlg), "plugin") );
}

static gboolean on_entry_focus_out( GtkWidget* edit, GdkEventFocus *evt, gpointer user_data )
{
    char** val = (char**)user_data;
    const char *new_val;
    g_free( *val );
    new_val = gtk_entry_get_text((GtkEntry*)edit);
    *val = (new_val && *new_val) ? g_strdup( new_val ) : NULL;
    notify_apply_config( edit );
    return FALSE;
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

static void on_file_chooser_btn_file_set(GtkFileChooser* btn, char** val)
{
    g_free( *val );
    *val = gtk_file_chooser_get_filename(btn);
    notify_apply_config( GTK_WIDGET(btn) );
}

static void on_browse_btn_clicked(GtkButton* btn, GtkEntry* entry)
{
    char* file;
    GtkFileChooserAction action = (GtkFileChooserAction) g_object_get_data(G_OBJECT(btn), "chooser-action");
    GtkWidget* dlg = GTK_WIDGET(g_object_get_data(G_OBJECT(btn), "dlg"));    
    GtkWidget* fc = gtk_file_chooser_dialog_new(
                                        (action == GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER) ? _("Select a directory") : _("Select a file"),
                                        GTK_WINDOW(dlg),
                                        action,
                                        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                        GTK_STOCK_OK, GTK_RESPONSE_OK,
                                        NULL);
    gtk_dialog_set_alternative_button_order(GTK_DIALOG(fc), GTK_RESPONSE_OK, GTK_RESPONSE_CANCEL, -1);
    file = (char*)gtk_entry_get_text(entry);
    if( file && *file )
        gtk_file_chooser_set_filename(GTK_FILE_CHOOSER(fc), file );
    if( gtk_dialog_run(GTK_DIALOG(fc)) == GTK_RESPONSE_OK )
    {
        file = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(fc));
        gtk_entry_set_text(entry, file);
        on_entry_focus_out(GTK_WIDGET(entry), NULL, g_object_get_data(G_OBJECT(dlg), "file-val"));
        g_free(file);
    }
    gtk_widget_destroy(fc);
}

inline void generic_config_dlg_save(gpointer panel_gpointer,GObject *where_the_object_was)
{
    Panel *panel = (Panel *)(panel_gpointer);
    panel_config_save(panel);
}

/* Handler for "response" signal from standard configuration dialog. */
static void generic_config_dlg_response(GtkWidget * widget, int response, Plugin * plugin)
{
    plugin->panel->plugin_pref_dialog = NULL;
    gtk_widget_destroy(widget);
}

/* Parameters: const char* name, gpointer ret_value, GType type, ....NULL */
GtkWidget* create_generic_config_dlg( const char* title, GtkWidget* parent,
                                      GSourceFunc apply_func, Plugin * plugin,
                                      const char* name, ... )
{
    va_list args;
    Panel* p = plugin->panel;
    GtkWidget* dlg = gtk_dialog_new_with_buttons( title, NULL, 0,
                                                  GTK_STOCK_CLOSE,
                                                  GTK_RESPONSE_CLOSE,
                                                  NULL );
    panel_apply_icon(GTK_WINDOW(dlg));

    g_signal_connect( dlg, "response", G_CALLBACK(generic_config_dlg_response), plugin);
    g_object_weak_ref(G_OBJECT(dlg), generic_config_dlg_save, p);
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
        enum _PluginConfType type = va_arg( args, enum _PluginConfType );
        switch( type )
        {
            case CONF_TYPE_STR:
            case CONF_TYPE_FILE_ENTRY: /* entry with a button to browse for files. */
            case CONF_TYPE_DIRECTORY_ENTRY: /* entry with a button to browse for directories. */
                entry = gtk_entry_new();
                if( *(char**)val )
                    gtk_entry_set_text( GTK_ENTRY(entry), *(char**)val );
                gtk_entry_set_width_chars(GTK_ENTRY(entry), 40);
                g_signal_connect( entry, "focus-out-event",
                  G_CALLBACK(on_entry_focus_out), val );
                break;
            case CONF_TYPE_INT:
            {
                /* FIXME: the range shouldn't be hardcoded */
                entry = gtk_spin_button_new_with_range( 0, 1000, 1 );
                gtk_spin_button_set_value( GTK_SPIN_BUTTON(entry), *(int*)val );
                g_signal_connect( entry, "value-changed",
                  G_CALLBACK(on_spin_changed), val );
                break;
            }
            case CONF_TYPE_BOOL:
                entry = gtk_check_button_new();
                gtk_container_add( GTK_CONTAINER(entry), label );
                gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(entry), *(gboolean*)val );
                g_signal_connect( entry, "toggled",
                  G_CALLBACK(on_toggle_changed), val );
                break;
            case CONF_TYPE_FILE:
                entry = gtk_file_chooser_button_new(_("Select a file"), GTK_FILE_CHOOSER_ACTION_OPEN);
                if( *(char**)val )
                    gtk_file_chooser_set_filename( GTK_FILE_CHOOSER(entry), *(char**)val );
                g_signal_connect( entry, "file-set",
                  G_CALLBACK(on_file_chooser_btn_file_set), val );
                break;
            case CONF_TYPE_TRIM:
                {
                entry = gtk_label_new(NULL);
                char *markup = g_markup_printf_escaped ("<span style=\"italic\">%s</span>", name );
                gtk_label_set_markup (GTK_LABEL (entry), markup);
                g_free (markup);
                }
                break;
        }
        if( entry )
        {
            if(( type == CONF_TYPE_BOOL ) || ( type == CONF_TYPE_TRIM ))
                gtk_box_pack_start( GTK_BOX(GTK_DIALOG(dlg)->vbox), entry, FALSE, FALSE, 2 );
            else
            {
                GtkWidget* hbox = gtk_hbox_new( FALSE, 2 );
                gtk_box_pack_start( GTK_BOX(hbox), label, FALSE, FALSE, 2 );
                gtk_box_pack_start( GTK_BOX(hbox), entry, TRUE, TRUE, 2 );
                gtk_box_pack_start( GTK_BOX(GTK_DIALOG(dlg)->vbox), hbox, FALSE, FALSE, 2 );
                if ((type == CONF_TYPE_FILE_ENTRY) || (type == CONF_TYPE_DIRECTORY_ENTRY))
                {
                    GtkWidget* browse = gtk_button_new_with_mnemonic(_("_Browse"));
                    gtk_box_pack_start( GTK_BOX(hbox), browse, TRUE, TRUE, 2 );
                    g_object_set_data(G_OBJECT(dlg), "file-val", val);
                    g_object_set_data(G_OBJECT(browse), "dlg", dlg);
                    
                    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
                    if (type == CONF_TYPE_DIRECTORY_ENTRY)
                    {
                      action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
                    }

                    g_object_set_data(G_OBJECT(browse), "chooser-action", GINT_TO_POINTER(action));
                    g_signal_connect( browse, "clicked", G_CALLBACK(on_browse_btn_clicked), entry );
                }
            }
        }
        name = va_arg( args, const char* );
    }
    va_end( args );

    gtk_container_set_border_width( GTK_CONTAINER(dlg), 8 );

    /* If there is already a plugin configuration dialog open, close it.
     * Then record this one in case the panel or plugin is deleted. */
    if (p->plugin_pref_dialog != NULL)
        gtk_widget_destroy(p->plugin_pref_dialog);
    p->plugin_pref_dialog = dlg;

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

static void save_global_config()
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
    return terminal_cmd ? terminal_cmd : "lxterminal -e %s";
}
