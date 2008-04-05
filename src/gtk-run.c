/*
 * gtk-run.c: Little application launcher
 * Copyright (C) 2006 Hong Jen Tee (PCMan) pcman.tw(AT)gmail.com
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <string.h>
#include "misc.h"

extern Panel *p;	/* FIXME: this should be removed */

static gboolean setup_auto_complete( gpointer entry )
{
    GtkListStore* store;
    GList *list = NULL, *l;
    gchar **dirname;
    gchar **dirnames = g_strsplit( g_getenv("PATH"), ":", 0 );
    GtkEntryCompletion* comp = gtk_entry_completion_new();
    gtk_entry_completion_set_minimum_key_length( comp, 2 );
    gtk_entry_completion_set_inline_completion( comp, TRUE );
#if GTK_CHECK_VERSION( 2, 8, 0 )
    gtk_entry_completion_set_popup_set_width( comp, TRUE );
    gtk_entry_completion_set_popup_single_match( comp, FALSE );
#endif
     store = gtk_list_store_new( 1, G_TYPE_STRING );

    for( dirname = dirnames; *dirname; ++dirname )
    {
        GDir *dir = g_dir_open( *dirname, 0, NULL );
        const char *name;
        if( ! dir )
            continue;
        while( ( name = g_dir_read_name( dir ) ) )
        {
            char* filename = g_build_filename( *dirname, name, NULL );
            if( g_file_test( filename, G_FILE_TEST_IS_EXECUTABLE ) )
            {
                if( !g_list_find_custom( list, name, (GCompareFunc)strcmp ) )
                    list = g_list_prepend( list, g_strdup( name ) );
            }
            g_free( filename );
        }
        g_dir_close( dir );
    }
    g_strfreev( dirnames );

    for( l = list; l; l = l->next )
    {
        GtkTreeIter it;
        gtk_list_store_append( store, &it );
        gtk_list_store_set( store, &it, 0, l->data, -1 );
        g_free( l->data );
    }
    g_list_free( list );

    gtk_entry_completion_set_model( comp, (GtkTreeModel*)store );
    g_object_unref( store );
    gtk_entry_completion_set_text_column( comp, 0 );
    gtk_entry_set_completion( (GtkEntry*)entry, comp );
    g_object_unref( G_OBJECT(comp) );
    return FALSE;
}

/*
static void show_error( GtkWindow* parent_win, const char* msg )
{
    GtkWidget* dlg = gtk_message_dialog_new( parent_win,
                                             GTK_DIALOG_MODAL,
                                             GTK_MESSAGE_ERROR,
                                             GTK_BUTTONS_OK, msg );
    gtk_dialog_run( (GtkDialog*)dlg );
    gtk_widget_destroy( dlg );
}
*/

static void on_response( GtkDialog* dlg, gint response, gpointer user_data )
{
    GtkEntry* entry = (GtkEntry*)user_data;
    if( G_LIKELY(response == GTK_RESPONSE_OK) )
    {
        GError* err = NULL;
        if( !g_spawn_command_line_async( gtk_entry_get_text(entry), &err ) )
        {
            show_error( (GtkWindow*)dlg, err->message );
            g_error_free( err );
            g_signal_stop_emission_by_name( dlg, "response" );
            return;
        }
    }
    g_source_remove_by_user_data( entry ); /* remove timeout */
    gtk_widget_destroy( (GtkWidget*)dlg );
}

void gtk_run()
{
    GtkWidget *win, *entry, *hbox;
    win = gtk_dialog_new_with_buttons( _("Run"),
                                       NULL,
                                       GTK_DIALOG_NO_SEPARATOR,
                                       GTK_STOCK_OK, GTK_RESPONSE_OK,
                                       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                       NULL );
    gtk_dialog_set_default_response( (GtkDialog*)win, GTK_RESPONSE_OK );
    entry = gtk_entry_new();

    /* fix background */
    //gtk_widget_set_style(win, p->defstyle);

    gtk_entry_set_activates_default( (GtkEntry*)entry, TRUE );
    gtk_box_pack_start( (GtkBox*)((GtkDialog*)win)->vbox,
                         gtk_label_new(_("Enter the command you want to execute:")),
                         FALSE, FALSE, 8 );
    hbox = gtk_hbox_new( FALSE, 2 );
    gtk_box_pack_start( (GtkBox*)hbox,
                         gtk_image_new_from_stock( GTK_STOCK_EXECUTE, GTK_ICON_SIZE_DIALOG ),
                         FALSE, FALSE, 4 );
    gtk_box_pack_start( (GtkBox*)hbox, entry, TRUE, TRUE, 4 );
    gtk_box_pack_start( (GtkBox*)((GtkDialog*)win)->vbox,
                         hbox, FALSE, FALSE, 8 );
    g_signal_connect( win, "response", G_CALLBACK(on_response), entry );
    gtk_window_set_position( (GtkWindow*)win, GTK_WIN_POS_CENTER );
    gtk_window_set_default_size( (GtkWindow*)win, 360, -1 );
    gtk_widget_show_all( win );
    /* g_timeout_add( 500, setup_auto_complete, entry ); */
    setup_auto_complete( entry );
    gtk_widget_show( win );
}

