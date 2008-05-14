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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

#include "dbg.h"

#include "glib-mem.h"

typedef enum {
  CURSOR_STANDARD,
  CURSOR_DND
} CursorType;

enum {
  TARGET_URILIST,
  TARGET_UTF8_STRING,
  TARGET_STRING,
  TARGET_TEXT,
  TARGET_COMPOUND_TEXT
};

enum {
    COL_ICON = 0,
    COL_TITLE,
    COL_BTN,
    N_COLS
};

static const GtkTargetEntry target_table[] = {
    { "text/uri-list", 0, TARGET_URILIST},
    { "UTF8_STRING", 0, TARGET_UTF8_STRING },
    { "COMPOUND_TEXT", 0, 0 },
    { "TEXT",          0, 0 },
    { "STRING",        0, 0 }
};

static const char desktop_ent[] = "Desktop Entry";

typedef struct btn_t {
    Plugin* plugin;
    GtkWidget* widget;
    gchar *desktop_id;
    gchar *image;
    gchar *action;
    gchar *tooltip;
/*  NOTE: Users can override the values specified in desktop file,
          and we should process these special cease. */
    guchar customize_image : 1;
    guchar customize_action : 1;
    guchar customize_tooltip : 1;
} btn_t;

typedef struct launchbar {
    GtkWidget *box;
    GtkTooltips *tips;
    GSList* btns;
    int iconsize;
    GtkWidget* config_dlg;
} launchbar;

void btn_free( btn_t* btn )
{
    g_free( btn->desktop_id );
    g_free( btn->image );
    g_free( btn->action );
    g_free( btn->tooltip );
    g_slice_free( btn_t, btn );
}

static gboolean
on_button_event(GtkWidget *widget, GdkEventButton *event, btn_t *b )
{
    GtkWidget *image;

    if( event->button == 1 )    /* left button */
    {
        image = gtk_bin_get_child(GTK_BIN(widget));
        g_assert(b != NULL);
        if (event->type == GDK_BUTTON_RELEASE) {
            if ((event->x >=0 && event->x < widget->allocation.width)
                  && (event->y >=0 && event->y < widget->allocation.height)) {

                g_spawn_command_line_async(b->action, NULL);
            }
            gtk_misc_set_padding (GTK_MISC(image), 0, 0);

            //system(b->action);
        } else if (event->type == GDK_BUTTON_PRESS) {

            gtk_misc_set_padding (GTK_MISC(image), 0, 3);
            //ERR("here\n");
        }
        return TRUE;
    }
    else if(event->button == 3) /* right click */
    {
        GtkMenu* popup = lxpanel_get_panel_menu( b->plugin->panel, b->plugin, TRUE );
        GtkWidget* item;
        char* title;

#if 0
        item = gtk_image_menu_item_new_with_label( _("Add Button") );
        gtk_menu_shell_append( popup, item );
        item = gtk_image_menu_item_new_with_label( _("Button Properties") );
        gtk_menu_shell_append( popup, item );
        /*
        title = g_strdup_printf( _("Remove \"%s\""), b-> );
        item = gtk_image_menu_item_new_with_label( _("Remove ") );
        */
        item = gtk_image_menu_item_new_with_label( _("Remove Button") );
        gtk_menu_shell_append( popup, item );
#endif
        gtk_widget_show_all( (GtkWidget*)popup );

        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, event->button, event->time );
        return TRUE;
    }
    return FALSE;
}

static void
launchbar_destructor(Plugin *p)
{
    launchbar *lb = (launchbar *)p->priv;

    ENTER;
    /* g_object_unref( lb->tips ); */

    gtk_widget_destroy(lb->box);
    g_slist_foreach( lb->btns, (GFunc)btn_free, NULL );
    g_slice_free(launchbar, lb);
    RET();
}


static void
drag_data_received_cb (GtkWidget        *widget,
      GdkDragContext   *context,
      gint              x,
      gint              y,
      GtkSelectionData *sd,
      guint             info,
      guint             time,
      btn_t              *b)
{
    gchar *s, *e, *end, *str, *tmp;

    ENTER;
    if (sd->length <= 0)
        RET();
    if (info == TARGET_URILIST) {
        DBG("uri drag received: info=%d len=%d data=%s\n", info, sd->length, sd->data);
        s = (gchar *)sd->data;
        end = s + sd->length;
        str = g_strdup(b->action);
        while (s < end) {
            while (s < end && g_ascii_isspace(*s))
                s++;
            e = s;
            while (e < end && !g_ascii_isspace(*e))
                e++;
            if (s != e) {
                *e = 0;
                s = g_filename_from_uri(s, NULL, NULL);
                if (s) {
                    //strlen(s);
                    //strlen(str);
                    tmp = g_strconcat(str, " '", s, "'", NULL);
                    g_free(str);
                    g_free(s);
                    str = tmp;
                }
            }
            s = e+1;
        }
        DBG("cmd=<%s>\n", str);
        g_spawn_command_line_async(str, NULL);
        g_free(str);

        //gtk_drag_finish (context, TRUE, FALSE, time);
    }
    RET();
}

static int
read_button(Plugin *p, char** fp)
{
    launchbar *lb = (launchbar *)p->priv;
    gchar *fname;
    GtkWidget *button;
    line s;
    int w, h;
    btn_t* btn;

    ENTER;

    btn = g_slice_new0( btn_t );
    btn->plugin = p;

    s.len = 256;
    fname= NULL;

    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "launchbar: illegal token %s\n", s.str);
                RET(0);
            }
            if (s.type == LINE_VAR) {
                if( !g_ascii_strcasecmp(s.t[0], "id") )
                    btn->desktop_id = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "image")) {
                    btn->customize_image = 1;
                    btn->image = g_strdup(s.t[1]);
                    fname = expand_tilda(s.t[1]);
                }
                else if (!g_ascii_strcasecmp(s.t[0], "tooltip")) {
                    btn->customize_tooltip = 1;
                    btn->tooltip = g_strdup(s.t[1]);
                }
                else if (!g_ascii_strcasecmp(s.t[0], "action")) {
                    btn->customize_action = 1;
                    btn->action = g_strdup(s.t[1]);
                }
                else {
                    ERR( "launchbar: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "launchbar: illegal in this context %s\n", s.str);
                goto error;
            }
        }
        DBG("action=%s\n", action);
    }

    if( btn->desktop_id ) {
        gchar *desktop_file = NULL;
        gchar *full_id = NULL;
        GKeyFile* desktop = g_key_file_new();
        full_id = g_strconcat( "applications/", btn->desktop_id, NULL );
        if( g_key_file_load_from_data_dirs( desktop, full_id, &desktop_file,
                                            G_KEY_FILE_NONE, NULL ) )
        {
            gchar *icon = NULL, *title = NULL;
            icon = g_key_file_get_string( desktop, desktop_ent, "Icon", NULL);
            title = g_key_file_get_locale_string( desktop, desktop_ent,
                                                "Name", NULL, NULL);
            if( !fname && icon ){
                gchar* sep;
                /* not a full path, remove the extension */
                if( icon[0] != '/' && (sep = strchr( icon, '.' )) )
                    fname = g_strndup( icon, (sep - icon) );
                else
                    fname = icon;
            }
            if( ! btn->customize_action ) {
                gchar* exec;
                exec = g_key_file_get_string( desktop, desktop_ent, "Exec", NULL);
                btn->action = translate_exec_to_cmd( exec, icon, title, desktop_file );
                g_free( exec );
            }
            if( ! btn->customize_tooltip )
                btn->tooltip = title;
            if( fname != icon )
                g_free( icon );
            if( btn->tooltip != title )
                g_free( title );
        }
        g_free( full_id );
        g_free( desktop_file );
        g_key_file_free( desktop );
    }

    // button
    if (p->panel->orientation == ORIENT_HORIZ) {
        w = 10000;
        //h = GTK_WIDGET(p->panel->box)->allocation.height;
        h = p->panel->ah;
    } else {
        //w = GTK_WIDGET(p->panel->box)->allocation.width;
        w = p->panel->aw;
        h = 10000;
    }

    button = fb_button_new_from_file( fname, w, h, 0x202020, TRUE );
    btn->widget = button;

    //gtk_container_set_border_width(GTK_CONTAINER(button), 0);
    g_signal_connect ( button, "button-release-event",
          G_CALLBACK (on_button_event), (gpointer) btn );
    g_signal_connect ( button, "button-press-event",
          G_CALLBACK (on_button_event), (gpointer) btn );

    GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);

    // DnD support
    gtk_drag_dest_set (GTK_WIDGET(button),
          GTK_DEST_DEFAULT_ALL, //GTK_DEST_DEFAULT_HIGHLIGHT,
          target_table, G_N_ELEMENTS (target_table),
          GDK_ACTION_COPY);
    g_signal_connect ( button, "drag_data_received",
          G_CALLBACK (drag_data_received_cb),  (gpointer) btn );

    gtk_box_pack_start(GTK_BOX(lb->box), button, FALSE, FALSE, 0);

    /* append is more time-consuming, but we really care about the order. */
    lb->btns = g_slist_append( lb->btns, btn );

    gtk_widget_show(button);
    plugin_widget_set_background( button, p->panel );

    g_free(fname);

    // tooltip
    if ( btn->tooltip ) {
        gtk_tooltips_set_tip(GTK_TOOLTIPS (lb->tips), button, btn->tooltip, NULL);
    }
    RET(1);

 error:
    g_free(fname);
    btn_free( btn );
    RET(0);
}

static int
launchbar_constructor(Plugin *p, char **fp)
{
    launchbar *lb;
    line s;
    GtkRequisition req;
    static char default_config[] =
        "button {\n"
            "id=pcmanfm.desktop\n"
        "}\n"
        "button {\n"
            "id=gnome-terminal.desktop\n"
        "}\n"
        "button {\n"
            "id=firefox.desktop\n"
        "}\n"
        "}\n";
    char *config_default = default_config;
    static gchar *launchbar_rc = "style 'launchbar-style'\n"
        "{\n"
        "GtkWidget::focus-line-width = 0\n"
        "GtkWidget::focus-padding = 0\n"
        "GtkButton::default-border = { 0, 0, 0, 0 }\n"
        "GtkButton::default-outside-border = { 0, 0, 0, 0 }\n"
        "}\n"
        "widget '*launchbar*' style 'launchbar-style'";

    ENTER;
    gtk_rc_parse_string(launchbar_rc);

    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS( p->pwid, GTK_NO_WINDOW );

    gtk_widget_set_name(p->pwid, "launchbar");
    get_button_spacing(&req, GTK_CONTAINER(p->pwid), "");

    lb = g_slice_new0(launchbar);
    g_return_val_if_fail(lb != NULL, 0);
    p->priv = lb;
    lb->box = p->panel->my_box_new(FALSE, 0);

    gtk_container_add( (GtkContainer*)p->pwid, lb->box );

    gtk_container_set_border_width (GTK_CONTAINER (lb->box), 0);
    gtk_widget_show(lb->box);

    /* Use the shared tooltip object provided by the panel, and
       we don't need to create a new one. */
    lb->tips = p->panel->tooltips;

    if  (p->panel->orientation == ORIENT_HORIZ)
        lb->iconsize = GTK_WIDGET(p->panel->box)->allocation.height;
    else
        lb->iconsize = GTK_WIDGET(p->panel->box)->allocation.width;

    if( ! fp )
        fp = &config_default;

    s.len = 256;
    while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_NONE) {
            ERR( "launchbar: illegal token %s\n", s.str);
            goto error;
        }
        if (s.type == LINE_BLOCK_START) {
            if (!g_ascii_strcasecmp(s.t[0], "button")) {
                if (!read_button(p, fp)) {
                    ERR( "launchbar: can't init button\n");
                    goto error;
                }
            } else {
                ERR( "launchbar: unknown var %s\n", s.t[0]);
                goto error;
            }
        } else {
            ERR( "launchbar: illegal in this context %s\n", s.str);
            goto error;
        }
    }

    RET(1);

 error:
    launchbar_destructor(p);
    RET(0);

}

static void save_config( Plugin* p, FILE* fp )
{
    launchbar *lb = (launchbar *)p->priv;
    GSList* l;
    for( l = lb->btns; l; l = l->next ) {
        btn_t* btn = (btn_t*)l->data;
        lxpanel_put_line( fp, "Button {" );
        if( btn->desktop_id )
            lxpanel_put_str( fp, "id", btn->desktop_id );
        if( btn->customize_image )
            lxpanel_put_str( fp, "image", btn->image );
        if( btn->customize_tooltip )
            lxpanel_put_str( fp, "tooltip", btn->tooltip );
        if( btn->customize_action )
            lxpanel_put_str( fp, "action", btn->action );
        lxpanel_put_line( fp, "}" );
    }
}

static void orientation_changed( Plugin* p )
{
    launchbar *lb = (launchbar *)p->priv;
    GtkBox* newbox;
    newbox = GTK_BOX(recreate_box( GTK_BOX(lb->box), p->panel->orientation ));
    if( GTK_WIDGET(newbox) != lb->box ) {
        /* Since the old box has been destroyed,
        we need to re-add the new box to the container */
        lb->box = GTK_WIDGET(newbox);
        gtk_container_add(GTK_CONTAINER(p->pwid), lb->box);
    }
}

static void
on_response( GtkDialog* dlg, int response, Plugin* p )
{
    launchbar *lb = (launchbar *)p->priv;
    gtk_widget_destroy( GTK_WIDGET(dlg) );
    lb->config_dlg = NULL;
}

static void on_add_btn_response( GtkDialog* dlg, int response, int* ret )
{
    *ret = response;
    gtk_main_quit();
}

static void on_add_btn( GtkButton* widget, Plugin* p )
{
    launchbar *lb = (launchbar *)p->priv;
    GtkTreeView* view = (GtkTreeView*)g_object_get_data( (GObject *) lb->config_dlg, "view" );
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(view);
    GtkTreeIter it;
    GtkListStore* list;
    GtkFileChooserDialog* dlg;
    GtkFileFilter* filter;
    int response;

    /*
    if( !gtk_tree_selection_get_selected( tree_sel, &list, &it ) )
        return;
    */
    list = (GtkListStore*)gtk_tree_view_get_model( view );

    /* FIXME: We should have a better interface for this in the fututure.
              1. We can borrow the menu from menu plugin (PtkAppMenu).
              2. We can borrow the app chooser from PCManFM.
    */
    dlg = gtk_file_chooser_dialog_new(_("Select Application"),
                                       lb->config_dlg,
                                       GTK_FILE_CHOOSER_ACTION_OPEN,
                                       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                       GTK_STOCK_ADD, GTK_RESPONSE_OK, NULL );
    filter = gtk_file_filter_new();
    gtk_file_filter_set_name( filter, "*.desktop" );
    gtk_file_filter_add_pattern( filter, "*.desktop" );
    gtk_file_chooser_add_filter( dlg, filter );
    g_object_unref( filter );
    gtk_file_chooser_set_local_only( dlg, TRUE );
    gtk_file_chooser_set_current_folder( dlg, "/usr/share/applications" );

    gtk_widget_set_sensitive( lb->config_dlg, FALSE );
    g_signal_connect( dlg, "response", on_add_btn_response, &response );
    gtk_window_present( dlg );
    gtk_main();
    gtk_widget_set_sensitive( lb->config_dlg, TRUE );

    if( response == GTK_RESPONSE_OK ) {
        char* filename = gtk_file_chooser_get_filename( dlg );
        if( filename ) {
            if( g_str_has_suffix( filename, ".desktop" ) ) {
                char* desktop_id = g_path_get_basename( filename );
                char *config, *pconfig;
                config = pconfig = g_strdup_printf( "id=%s\n}\n", desktop_id );
                g_free( desktop_id );
                /* Make a fake config entry, and let read_button() parst it. */
                /* FIXME: This is a quick hack, which is dirty but easy and useful.
                          Need to be re-written in the future.
                */
                if( read_button( p, &pconfig ) ) {
                    GSList* l;
                    btn_t* btn;
                    l = g_slist_last( lb->btns );
                    btn = (btn_t*)l->data;
                    gtk_list_store_append( list, &it );
                    gtk_list_store_set( list, &it,
                                        COL_ICON, NULL, /* FIXME: need to be implemented */
                                        COL_TITLE, (btn->tooltip ? btn->tooltip : btn->action),
                                        COL_BTN, btn,
                                        -1 );
                }
                g_free( config );
            }
            g_free( filename );
        }
    }

    gtk_widget_destroy( dlg );
}

static void on_remove_btn( GtkButton* widget, Plugin* p )
{
    launchbar *lb = (launchbar *)p->priv;
    GtkTreeView* view = (GtkTreeView*)g_object_get_data( lb->config_dlg, "view" );
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(view);
    GtkTreeIter it;
    GtkListStore* list;
    btn_t* btn;

    if( !gtk_tree_selection_get_selected( tree_sel, &list, &it ) )
        return;
    gtk_tree_model_get( (GtkTreeModel*)list, &it,
                         COL_BTN, &btn, -1 );
    gtk_list_store_remove( list, &it );
    if( btn ) {
        lb->btns = g_slist_remove( lb->btns, btn );
        gtk_widget_destroy( btn->widget );
        btn_free( btn );
    }
}

static void on_up_btn( GtkButton* widget, Plugin* p )
{
    launchbar *lb = (launchbar *)p->priv;
    btn_t *btn;
    GtkTreeView* view = (GtkTreeView*)g_object_get_data( lb->config_dlg, "view" );
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(view);
    GtkTreeIter it;
    GtkTreePath* path;
    GtkListStore* list;

    if( !gtk_tree_selection_get_selected( tree_sel, &list, &it ) )
        return;
    gtk_tree_model_get( (GtkTreeModel*)list, &it, COL_BTN, &btn, -1 );
    path = gtk_tree_model_get_path( (GtkTreeModel*)list, &it );
    if( gtk_tree_path_get_indices(path)[0] > 0 ) {
        if( gtk_tree_path_prev(path) ) {
            GtkTreeIter it2;
            if( gtk_tree_model_get_iter( (GtkTreeModel*)list, &it2, path ) ) {
                int i = gtk_tree_path_get_indices(path)[0];
                lb->btns = g_slist_remove( lb->btns, btn );
                lb->btns = g_slist_insert( lb->btns, btn, i );
                gtk_list_store_move_before( (GtkTreeModel*)list, &it, &it2 );
                gtk_box_reorder_child( lb->box, btn->widget, i );
            }
        }
    }
    gtk_tree_path_free( path );
}

static void on_down_btn( GtkButton* widget, Plugin* p )
{
    launchbar *lb = (launchbar *)p->priv;
    btn_t *btn;
    GtkTreeView* view = (GtkTreeView*)g_object_get_data( lb->config_dlg, "view" );
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(view);
    GtkTreeIter it;
    GtkTreePath* path;
    GtkListStore* list;
    int n;

    if( !gtk_tree_selection_get_selected( tree_sel, &list, &it ) )
        return;
    gtk_tree_model_get( (GtkTreeModel*)list, &it, COL_BTN, &btn, -1 );
    path = gtk_tree_model_get_path( (GtkTreeModel*)list, &it );
    n = gtk_tree_model_iter_n_children( (GtkTreeModel*)list, NULL );
    if( gtk_tree_path_get_indices(path)[0] < n - 1 ) {
        GtkTreeIter it2;
        gtk_tree_path_next(path);
        if( gtk_tree_model_get_iter( (GtkTreeModel*)list, &it2, path ) ) {
            int i = gtk_tree_path_get_indices(path)[0];
            lb->btns = g_slist_insert( lb->btns, btn, i + 1 );
            lb->btns = g_slist_remove( lb->btns, btn );
            gtk_list_store_move_after( (GtkTreeModel*)list, &it, &it2 );
            gtk_box_reorder_child( lb->box, btn->widget, i );
        }
    }
    gtk_tree_path_free( path );
}

static void init_btn_list( Plugin* p, GtkTreeView* view )
{
    launchbar *lb = (launchbar *)p->priv;
    GtkListStore *list;
    GSList* l;
    GtkTreeViewColumn* col;
    GtkCellRenderer* render;
    GtkTreeSelection* tree_sel = gtk_tree_view_get_selection(view);

    gtk_tree_selection_set_mode(tree_sel, GTK_SELECTION_BROWSE);

    list = gtk_list_store_new( N_COLS,
                               GDK_TYPE_PIXBUF,
                               G_TYPE_STRING,
                               G_TYPE_POINTER,
                               G_TYPE_POINTER );
    col = gtk_tree_view_column_new();
    gtk_tree_view_column_set_title( col, _("Buttons") );

    render = gtk_cell_renderer_pixbuf_new();
    gtk_tree_view_column_pack_start( col, render, FALSE );
    gtk_tree_view_column_set_attributes( col, render, "pixbuf", COL_ICON, NULL );

    render = gtk_cell_renderer_text_new();
    gtk_tree_view_column_pack_start( col, render, TRUE );
    gtk_tree_view_column_add_attribute( col, render, "text", COL_TITLE );

    gtk_tree_view_append_column( view, col );

    for( l = lb->btns; l; l = l->next ) {
        GtkTreeIter it;
        GdkPixbuf* pix;
        char* fname;

        btn_t* btn = (btn_t*)l->data;
#if 0
        fname = expand_tilda( btn->image );
        if( fname ) {
            if( fname[0] == '/' ) /* file */
                pix = gdk_pixbuf_new_from_file( fname, NULL );
            else {
                //pix =
            }
        }
        else
            pix = NULL;
        g_free( fname );
#endif
        gtk_list_store_append( list, &it );
        gtk_list_store_set( list, &it,
                            COL_ICON, NULL,
                            COL_TITLE, (btn->tooltip ? btn->tooltip : btn->action),
                            COL_BTN, btn, -1 );
    }

    gtk_tree_view_set_model( view, (GtkTreeModel*)list );
    g_object_unref( list );

    g_object_set_data( lb->config_dlg, "view", view );
}

static void launchbar_config( Plugin *p, GtkWindow* parent )
{
    GtkWidget *dlg, *hbox, *vbox, *scroll, *view, *btn, *img;
    launchbar *lb = (launchbar *)p->priv;

    if( !lb->config_dlg )
    {
        dlg = gtk_dialog_new_with_buttons( _(p->class->name),
                                        GTK_WIDGET(parent), 0,
                                        GTK_STOCK_CLOSE,
                                        GTK_RESPONSE_CLOSE,
                                        NULL );
        lb->config_dlg = dlg;

        hbox = gtk_hbox_new( FALSE, 4 );
        gtk_box_pack_start( (GtkBox*)GTK_DIALOG(dlg)->vbox, hbox, TRUE, TRUE, 2 );

        scroll = gtk_scrolled_window_new( NULL, NULL );
        gtk_scrolled_window_set_policy( (GtkScrolledWindow*)scroll,
                                         GTK_POLICY_AUTOMATIC,
                                         GTK_POLICY_AUTOMATIC );
        gtk_scrolled_window_set_shadow_type( (GtkScrolledWindow*)scroll,
                                             GTK_SHADOW_IN );
        gtk_box_pack_start( (GtkBox*)hbox, scroll, TRUE, TRUE, 2 );

        view = gtk_tree_view_new();
        gtk_container_add( (GtkContainer*)scroll, view );

        vbox = gtk_vbox_new( FALSE, 2 );
        gtk_box_pack_start( (GtkBox*)hbox, vbox, FALSE, FALSE, 2 );

        btn = gtk_button_new_from_stock( GTK_STOCK_ADD );
        g_signal_connect( btn, "clicked", G_CALLBACK( on_add_btn ), p );
        gtk_box_pack_start( (GtkBox*)vbox, btn, FALSE, FALSE, 2 );

        btn = gtk_button_new_from_stock( GTK_STOCK_REMOVE );
        g_signal_connect( btn, "clicked", G_CALLBACK( on_remove_btn ), p );
        gtk_box_pack_start( (GtkBox*)vbox, btn, FALSE, FALSE, 2 );

        btn = gtk_button_new();
        gtk_container_add( GTK_CONTAINER(btn),
                gtk_image_new_from_stock(GTK_STOCK_GO_UP,
                                        GTK_ICON_SIZE_BUTTON) );
        g_signal_connect( btn, "clicked", G_CALLBACK( on_up_btn ), p );
        gtk_box_pack_start( (GtkBox*)vbox, btn, FALSE, FALSE, 2 );

        btn = gtk_button_new();
        gtk_container_add( GTK_CONTAINER(btn),
                gtk_image_new_from_stock(GTK_STOCK_GO_DOWN,
                                        GTK_ICON_SIZE_BUTTON) );
        g_signal_connect( btn, "clicked", G_CALLBACK( on_down_btn ), p );
        gtk_box_pack_start( (GtkBox*)vbox, btn, FALSE, FALSE, 2 );

        g_signal_connect( dlg, "response", G_CALLBACK(on_response), p );

        gtk_window_set_default_size( (GtkWindow*)dlg, 320, 400 );

        init_btn_list( p, view );

        gtk_widget_show_all( dlg );
    }
    gtk_window_present( GTK_WINDOW(lb->config_dlg) );
}

PluginClass launchbar_plugin_class = {
    fname: NULL,
    count: 0,

    type : "launchbar",
    name : N_("Application Launch Bar"),
    version: "1.0",
    description : N_("Bar with buttons to launch application"),

    constructor : launchbar_constructor,
    destructor  : launchbar_destructor,
    config : launchbar_config,
    save : save_config,
    orientation : orientation_changed
};
