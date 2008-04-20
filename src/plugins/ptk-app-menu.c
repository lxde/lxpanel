/*
* ptk-app-menu.c
*
* Description: Generate menu from desktop files according to the spec on freedesktop.org
*
*
* Author: Hong Jen Yee (PCMan) <pcman.tw (AT) gmail.com>, (C) 2006
*
* Copyright: GNU Lesser General Public License Version 2
*
*/

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <stdio.h>
#include <sys/stat.h>
#include <string.h>
#include "ptk-app-menu.h"

/* Compatibility macros for older versions of glib */
#if ! GLIB_CHECK_VERSION(2, 10, 0)
/* older versions of glib don't provde g_slice API */
#define g_slice_alloc(size)         g_malloc(size)
#define g_slice_alloc0(size)        g_malloc0(size)
#define g_slice_new(type)           g_new(type, 1)
#define g_slice_new0(type)          g_new0(type, 1)
#define g_slice_free(type, mem)     g_free(mem)
#define g_slice_free1(size, mem)    g_free(mem)
#endif

#include "misc.h" /* Misc functions for lxpanel */

#define ICON_SIZE        18

GQuark PTK_APP_MENU_ITEM_ID = 0;

GtkWidget* ptk_app_menu_new();

static const char desktop_ent[] = "Desktop Entry";
static const char app_dir_name[] = "applications";

static time_t* times = NULL;


typedef struct _CatInfo
{
    char* title;
    char* directory_file;
    char* icon;
    const char** sub_cats;
}CatInfo;

typedef struct _PtkAppMenuItem
{
    char* name;
    char* icon;
    char* exec;
}PtkAppMenuItem;

static const char* development_cats[]={
   "Development",
   "Translation",
   "Building","Debugger",
   "IDE",
   "GUIDesigner",
   "Profiling",
   "RevisionControl",
   "WebDevelopment",
   NULL
};

static const char* office_cats[] = {
   "Office",
   "Dictionary",
   "Chart",
   "Calendar",
   "ContactManagement",
   "Database",
   NULL
};

static const char* graphics_cats[] = {
   "Graphics",
   "2DGraphics",
   "3DGraphics",
   "VectorGraphics",
   "RasterGraphics",
   "Viewer",
   NULL
};

static const char* network_cats[] = {
   "Network",
   "Dialup",
   "Email",
   "WebBrowser",
   "InstantMessaging",
   "IRCClient",
   "FileTransfer",
   "News",
   "P2P",
   "RemoteAccess",
   "Telephony",
   NULL
};

static const char* settings_cats[] = {
   "Settings",
   "DesktopSettings",
   "HardwareSettings",
   "Accessibility",
   NULL
};

static const char* system_cats[] = {
   "System",
   "Core",
   "Security",
   "PackageManager",
   NULL
};

static const char* audiovideo_cats[] ={
   "AudioVideo",
   "Audio",
   "Video",
   "Mixer",
   "Sequencer",
   "Tuner",
   "TV",
   "AudioVideoEditing",
   "Player",
   "Recorder",
   "DiscBurning",
   "Music",
   NULL
};

static const char* game_cats[] = {
   "Game",
   "Amusement",
   NULL
};

static const char* education_cats[] = {
   "Education",
   NULL
};

static const char* utility_cats[] = {
   "Utility",
   NULL
};

static const CatInfo known_cats[]=
{
    {N_("Other"), "Other", "gnome-other", NULL},
    {N_("Game"), "Games", "gnome-joystick", game_cats},
    {N_("Education"), "Education", "gnome-amusements", education_cats},
    {N_("Development"), "Development", "gnome-devel", development_cats},
    {N_("Audio & Video"), "Multimedia", "gnome-multimedia", audiovideo_cats},
    {N_("Graphics"), "Graphics", "gnome-graphics", graphics_cats},
    {N_("Settings"), "Settings", "gnome-settings", settings_cats},
    {N_("System Tools"), "System-Tools", "gnome-system", system_cats},
    {N_("Network"), "Internet", "gnome-globe", network_cats},
    {N_("Office"), "Office", "gnome-applications", office_cats},
    {N_("Accessories"), "Accessories", "gnome-util", utility_cats}
};

static int find_cat( char** cats )
{
    char** cat;
    for( cat = cats; *cat; ++cat )
    {
        int i;
        /* Skip other */
        for( i = 1; i < G_N_ELEMENTS(known_cats); ++i )
        {
            const char** sub_cats = known_cats[i].sub_cats;
            while( *sub_cats )
            {
                if( 0 == strncmp(*cat, "X-", 2) ) /*  Desktop specific*/
                    return -1;
                if( 0 == strcmp( *sub_cats, *cat ) )
                    return i;
                ++sub_cats;
            }
        }
    }
    return -1;
}

static void app_dirs_foreach( GFunc func, gpointer user_data );

static int compare_menu_item_titles( gpointer a, gpointer b )
{
    const gchar *title_a, *title_b;
    title_a = gtk_label_get_text( GTK_LABEL(gtk_bin_get_child(GTK_BIN(a))) );
    title_b = gtk_label_get_text( GTK_LABEL(gtk_bin_get_child(GTK_BIN(b))) );
    return g_ascii_strcasecmp(title_a, title_b);
}

static int find_menu_item_by_name( gpointer a, gpointer b )
{
    PtkAppMenuItem* data = g_object_get_qdata( G_OBJECT(a), PTK_APP_MENU_ITEM_ID );
    const char* name = (char*)b;
    return strcmp(data->name, name);
}

/* Moved to misc.c of lxpanel to be used in other plugins */
#if 0
static char* translate_exec( const char* exec, const char* icon,
                             const char* title, const char* fpath )
{
    GString* cmd = g_string_sized_new( 256 );
    for( ; *exec; ++exec )
    {
        if( G_UNLIKELY(*exec == '%') )
        {
            ++exec;
            if( !*exec )
                break;
            switch( *exec )
            {
            case 'c':
                g_string_append( cmd, title );
                break;
            case 'i':
                if( icon )
                {
                    g_string_append( cmd, "--icon " );
                    g_string_append( cmd, icon );
                }
                break;
            case 'k':
                {
                    char* uri = g_filename_to_uri( fpath, NULL, NULL );
                    g_string_append( cmd, uri );
                    g_free( uri );
                    break;
                }
            case '%':
                g_string_append_c( cmd, '%' );
                break;
            }
        }
        else
            g_string_append_c( cmd, *exec );
    }
    return g_string_free( cmd, FALSE );
}
#endif

static char* load_cat_title( GKeyFile* kf, CatInfo* inf )
{
    char* ret = NULL;
    char* fn = g_strconcat( "desktop-directories/", inf->directory_file, ".directory", NULL );
    if( g_key_file_load_from_data_dirs( kf, fn, NULL, 0, NULL ) )
        ret = g_key_file_get_locale_string( kf, desktop_ent, "Name", NULL, NULL );
    g_free( fn );
    return ret;
}

static void  unload_old_icons( GtkWidget* menu )
{
    GList* items = gtk_container_get_children( GTK_CONTAINER(menu) );
    GList* l;
    for( l = items; l; l = l->next )
    {
        GtkWidget* sub_menu = gtk_menu_item_get_submenu( GTK_MENU_ITEM(l->data) );
        GtkWidget* img = gtk_image_menu_item_get_image( GTK_IMAGE_MENU_ITEM(l->data) );
        if( ! g_object_get_qdata( G_OBJECT(l->data), PTK_APP_MENU_ITEM_ID ) )
            continue;
        if( img )
            gtk_widget_destroy( img );
        if( sub_menu )
            unload_old_icons( sub_menu );
    }
    g_list_free( items );
}

static void on_menu_item_size_request( GtkWidget* item,
                                       GtkRequisition* req,
                                       gpointer user_data )
{
    int min_height = ICON_SIZE + (GTK_CONTAINER(item)->border_width +
             item->style->ythickness) * 2;
    if( req->height < min_height ) {
        req->height = min_height;
    }
    if( req->width < ICON_SIZE )
       req->width = ICON_SIZE;
}

static gboolean on_menu_item_expose( GtkWidget* item,
                                     GdkEventExpose* evt,
                                     gpointer user_data )
{
    GtkWidget* img;
    GdkPixbuf* pix;
    PtkAppMenuItem* data = (PtkAppMenuItem*)user_data;
    if( !data )
        return FALSE;
    if( !GTK_IS_IMAGE_MENU_ITEM(item) )
    return FALSE;
    img = GTK_WIDGET(gtk_image_menu_item_get_image((GtkImageMenuItem *) item));
    if( img )
        return FALSE;
    if( G_UNLIKELY(!data) || G_UNLIKELY(!data->icon) )
        return FALSE;
    pix = NULL;
    if( data->icon[0] == '/' )
    {
        pix = gdk_pixbuf_new_from_file_at_size(data->icon, ICON_SIZE, ICON_SIZE, NULL);
    }
    else
    {
        GtkIconInfo* inf;
        inf = gtk_icon_theme_lookup_icon(gtk_icon_theme_get_default(), data->icon, ICON_SIZE, 0);
        if( inf )
        {
            pix = gdk_pixbuf_new_from_file_at_size( gtk_icon_info_get_filename(inf), ICON_SIZE, ICON_SIZE, NULL);
            gtk_icon_info_free ( inf );
        }
    }
    if( G_LIKELY(pix) )
    {
        img = gtk_image_new_from_pixbuf( pix );
        if( G_LIKELY(pix) )
            g_object_unref( pix );
    }
    else
    {
        img = gtk_image_new();
        gtk_image_set_pixel_size( GTK_IMAGE(img), ICON_SIZE );
    }
    gtk_image_menu_item_set_image( (GtkImageMenuItem *) item, img );
    return FALSE;
}

static void on_app_menu_item_activate( GtkMenuItem* item, PtkAppMenuItem* data )
{
    GError* err = NULL;
    /* FIXME: support startup notification */
    g_debug("run command: %s", data->exec);
    if( !g_spawn_command_line_async( data->exec, &err ) )
    {
        /* FIXME: show error message */
        g_error_free( err );
    }
}

static void ptk_app_menu_item_free( PtkAppMenuItem* data )
{
    g_free( data->name );
    g_free( data->icon );
    g_free( data->exec );
    g_slice_free( PtkAppMenuItem, data );
}

static void do_load_dir( int prefix_len,
                         const char* path,
                         GList** sub_menus )
{
    GDir* dir = g_dir_open( path, 0, NULL );
    const char* name;
    GKeyFile*  file;

    if( G_UNLIKELY( ! dir ) )
        return;

    file = g_key_file_new();

    while( (name = g_dir_read_name( dir )) )
    {
        char* fpath;
        char **cats;
        char **only_show_in;

        if( name[0] =='.' )
            continue;
        fpath = g_build_filename( path, name, NULL );
        if( g_file_test(fpath, G_FILE_TEST_IS_DIR) )
        {
            do_load_dir( prefix_len, fpath, sub_menus );
            g_free( fpath );
            continue;
        }
        if( ! g_str_has_suffix( name, ".desktop" ) )
        {
            g_free( fpath );
            continue;
        }
        if( ! g_key_file_load_from_file( file, fpath, 0, NULL ) )
        {
            g_free( fpath );
            continue;
        }
        if( g_key_file_get_boolean( file, desktop_ent, "NoDisplay", NULL ) )
        {
            g_free( fpath );
            continue;
        }
        only_show_in = g_key_file_get_string_list( file, desktop_ent, "OnlyShowIn", NULL, NULL );
        if( only_show_in )
        {
            g_free( fpath );
            g_strfreev( only_show_in );
            continue;
        }
        cats = g_key_file_get_string_list( file, desktop_ent, "Categories", NULL, NULL );
        if( cats )
        {
            int i = find_cat( cats );
            if( i >= 0 )
            {
                GtkWidget* menu_item;
                char *title, *exec, *icon;
                /* FIXME: processing duplicated items */
                exec = g_key_file_get_string( file, desktop_ent, "Exec", NULL);
                if( exec )
                {
                    title = g_key_file_get_locale_string( file, desktop_ent, "Name", NULL, NULL);
                    if( title )
                    {
                        PtkAppMenuItem* data;
                        GList* prev;
                        prev =g_list_find_custom( sub_menus[i], (fpath + prefix_len),
                              (GCompareFunc) find_menu_item_by_name );
                        if( ! prev )
                        {
                            menu_item = gtk_image_menu_item_new_with_label( title );
                            data = g_slice_new0(PtkAppMenuItem);
                        }
                        else
                        {
                            GtkLabel* label;
                            menu_item = GTK_WIDGET(prev->data);
                            label = GTK_LABEL(gtk_bin_get_child(GTK_BIN(menu_item)));
                            data = (PtkAppMenuItem*)g_object_get_qdata( G_OBJECT(menu_item), PTK_APP_MENU_ITEM_ID );
                            gtk_label_set_text( label, title );
                            g_free( data->name );
                            g_free( data->exec );
                            g_free( data->icon );
                        }
                        data->name = g_strdup( fpath + prefix_len );
                        data->exec = exec ? translate_exec_to_cmd( exec, data->icon, title, fpath ) : NULL;
                        g_free( title );
                        g_signal_connect( menu_item, "expose-event",
                      G_CALLBACK(on_menu_item_expose), data );
                        g_signal_connect( menu_item,
                                          "size-request",
                      G_CALLBACK(on_menu_item_size_request), data );
                        icon = g_strdup( g_key_file_get_string( file, desktop_ent, "Icon", NULL) );
                        if( icon )
                        {
                            char* dot = strchr( icon, '.' );
                            if( icon[0] !='/' && dot )
                                *dot = '\0';
                        }
                        data->icon = icon;
                        if( !prev )
                        {
                            g_signal_connect( menu_item, "activate",
                          G_CALLBACK(on_app_menu_item_activate), data );
                            g_object_set_qdata_full( G_OBJECT(menu_item), PTK_APP_MENU_ITEM_ID, data,
                             (GDestroyNotify) ptk_app_menu_item_free );
                            sub_menus[i] = g_list_insert_sorted( sub_menus[i],
                                 (gpointer) menu_item,
                                 (GCompareFunc) compare_menu_item_titles );
                        }
                    } /* if( title ) */
                    g_free( exec );
                } /* if( exec ) */
            }
            g_strfreev(cats);
        }
        g_free( fpath );
    }
    g_key_file_free( file );
    g_dir_close( dir );
}

static void load_dir( const char* path, GList** sub_menus )
{
    do_load_dir( strlen( path ) + 1, path, sub_menus );
}

#if defined( PTK_APP_MENU_DEMO )
static GtkWidget* app_menu = NULL;
static void on_menu( GtkWidget* btn, gpointer user_data )
{
    if( ptk_app_menu_need_reload() )
    {
        if( app_menu )
            gtk_widget_destroy( app_menu );
        app_menu = ptk_app_menu_new();
    }
    else if( !app_menu )
        app_menu = ptk_app_menu_new();
    gtk_menu_popup(GTK_MENU(app_menu), NULL, NULL, NULL, NULL, 0, 0 );
}
#endif

static void on_app_menu_destroy( gpointer user_data, GObject* menu )
{
    g_signal_handler_disconnect( gtk_icon_theme_get_default(),
                                 GPOINTER_TO_INT(user_data) );
}

gboolean ptk_app_menu_item_has_data( GtkMenuItem* item )
{
   return (g_object_get_qdata( G_OBJECT(item), PTK_APP_MENU_ITEM_ID ) != NULL);
}

/*
 * Insert application menus into specified menu
 * menu: The parent menu to which the items should be inserted
 * pisition: Position to insert items.
             Passing -1 in this parameter means append all items
             at the end of menu.
 */
void ptk_app_menu_insert_items( GtkMenu* menu, int position )
{
   GList* sub_menus[ G_N_ELEMENTS(known_cats) ] = {0};
   int i;
   GList *sub_items, *l;
   guint change_handler;
  GKeyFile* kf;

    if( G_UNLIKELY( PTK_APP_MENU_ITEM_ID == 0 ) )
        PTK_APP_MENU_ITEM_ID = g_quark_from_static_string( "PtkAppMenuItem" );

   app_dirs_foreach( (GFunc) load_dir, sub_menus );

    kf = g_key_file_new();

   for( i = 0; i < G_N_ELEMENTS(known_cats); ++i )
   {
      GtkMenu* sub_menu;
      GtkWidget* menu_item;
      PtkAppMenuItem* data;
      char* title;

      if( ! (sub_items = sub_menus[i]) )
         continue;
      sub_menu = GTK_MENU(gtk_menu_new());

      for( l = sub_items; l; l = l->next )
         gtk_menu_shell_append( GTK_MENU_SHELL(sub_menu), GTK_WIDGET(l->data) );
      g_list_free( sub_items );

      title = load_cat_title( kf, &known_cats[i] );
      menu_item = gtk_image_menu_item_new_with_label( title ? title : _(known_cats[i].title) );
      g_free( title );

      data = g_slice_new0( PtkAppMenuItem );
      data->icon = g_strdup(known_cats[i].icon);
      g_object_set_qdata_full( G_OBJECT(menu_item), PTK_APP_MENU_ITEM_ID, data, (GDestroyNotify) ptk_app_menu_item_free );
      g_signal_connect( menu_item, "expose-event", G_CALLBACK(on_menu_item_expose), data );
      g_signal_connect( menu_item, "size-request", G_CALLBACK(on_menu_item_size_request), data );
      on_menu_item_expose( menu_item, NULL, data );
      gtk_menu_item_set_submenu( GTK_MENU_ITEM(menu_item), GTK_WIDGET(sub_menu) );

      if( position == -1 )
         gtk_menu_shell_append( GTK_MENU_SHELL(menu), menu_item );
      else
      {
         gtk_menu_shell_insert( GTK_MENU_SHELL(menu), menu_item, position );
         ++position;
      }
   }

   g_key_file_free( kf );

   gtk_widget_show_all(GTK_WIDGET(menu));
   change_handler = g_signal_connect_swapped( gtk_icon_theme_get_default(), "changed", G_CALLBACK(unload_old_icons), menu );
   g_object_weak_ref( G_OBJECT(menu), on_app_menu_destroy, GINT_TO_POINTER(change_handler) );
}

GtkWidget* ptk_app_menu_new()
{
    GtkWidget* menu;
    menu = gtk_menu_new();
    ptk_app_menu_insert_items( GTK_MENU(menu), -1 );
    return menu;
}

static void app_dirs_foreach( GFunc func, gpointer user_data )
{
    const char** sys_dirs = (const char**)g_get_system_data_dirs();
    char* path;
    int i, len;
    struct stat dir_stat;

    len = g_strv_length((gchar **) sys_dirs);
    if( !times )
        times = g_new0( time_t, len + 2 );
    for( i = 0; i < len; ++i )
    {
        path = g_build_filename( sys_dirs[i], app_dir_name, NULL );
        if( stat( path, &dir_stat) == 0 )
        {
            times[i] = dir_stat.st_mtime;
            func( path, user_data );
        }
        g_free( path );
    }
    path = g_build_filename( g_get_user_data_dir(), app_dir_name, NULL );
    times[i] = dir_stat.st_mtime;
    if( stat( path, &dir_stat) == 0 )
    {
        times[i] = dir_stat.st_mtime;
        func( path, user_data );
    }
    g_free( path );
}

#if defined( PTK_APP_MENU_DEMO )
int main( int argc, char** argv )
{
    gtk_init(&argc, &argv);
    GtkWidget* window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title(GTK_WINDOW( window ), "Show Applications" );
    GtkWidget* button = gtk_button_new_with_label("Application Menu");
    g_signal_connect(button, "clicked", G_CALLBACK(on_menu), NULL );
    gtk_container_add( GTK_CONTAINER(window), button );
    g_signal_connect(window, "delete-event", G_CALLBACK(gtk_main_quit), NULL );
    gtk_widget_show_all(window);
    if( app_menu )
        gtk_widget_destroy( app_menu );
    gtk_main();
    return 0;
}
#endif

gboolean ptk_app_menu_need_reload( GtkWidget* menu )
{
    const char** sys_dirs = (const char**)g_get_system_data_dirs();
    char* path;
    int i, len;
    struct stat dir_stat;

    if( !times )
        return TRUE;
    len = g_strv_length((gchar **) sys_dirs);
    for( i = 0; i < len; ++i )
    {
        path = g_build_filename( sys_dirs[i], app_dir_name, NULL );
        if( stat( path, &dir_stat) == 0 )
        {
            if( times[i] != dir_stat.st_mtime )
            {
                g_free( path );
                return TRUE;
            }
        }
        g_free( path );
    }
    path = g_build_filename( g_get_user_data_dir(), app_dir_name, NULL );
    if( stat( path, &dir_stat) == 0 )
    {
        if( times[i] != dir_stat.st_mtime )
        {
            g_free( path );
            return TRUE;
        }
    }
    g_free( path );
    return FALSE;
}

