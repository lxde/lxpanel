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

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk-pixbuf-xlib/gdk-pixbuf-xlib.h>
#include <gdk/gdk.h>
#include <string.h>
#include <stdlib.h>

#include "misc.h"
#include "bg.h"

#include <glib-object.h>

//#define DEBUG
#include "dbg.h"

static GList *pcl = NULL;

#if 0
/* dummy type plugin for dynamic plugins registering new types */
static void
lx_type_plugin_iface_init(gpointer g_iface, gpointer g_iface_data);

G_DEFINE_TYPE_EXTENDED ( LXTypePlugin,
                         lx_type_plugin,
                         G_TYPE_OBJECT,
                         0,
                         G_IMPLEMENT_INTERFACE (G_TYPE_TYPE_PLUGIN,
                                         lx_type_plugin_iface_init))

void lx_type_plugin_init( LXTypePlugin* tp )
{
}

void lx_type_plugin_class_init(LXTypePluginClass* klass)
{
}

void lx_type_plugin_iface_init(gpointer g_iface, gpointer g_iface_data)
{
}

GTypePlugin* lx_type_plugin_get(const char* plugin_name)
{
    LXTypePlugin* tp = g_object_new(LX_TYPE_TYPE_PLUGIN, NULL);
    return tp;
}
#endif

/* counter for static (built-in) plugins must be greater then zero
 * so lxpanel will not try to unload them */

#define REGISTER_PLUGIN_CLASS(pc, dynamic) \
do {\
    extern PluginClass pc;\
    register_plugin_class(&pc, dynamic);\
} while (0)


static void
register_plugin_class(PluginClass *pc, int dynamic)
{
    pcl = g_list_append(pcl, pc);
    pc->dynamic = dynamic;
    if (!pc->dynamic)
        pc->count++;
    /* reloading netstatus results in segfault due to registering static type in dll.
     * so keep it always onboard until bug fix */
    if (!strcmp(pc->type, "netstatus"))
        pc->count++;
}

static void
init_plugin_class_list()
{
#ifdef STATIC_SEPARATOR
    REGISTER_PLUGIN_CLASS(separator_plugin_class, 0);
#endif

/* Remove image plugin since it seems to be useless. */
/*
#ifdef STATIC_IMAGE
    REGISTER_PLUGIN_CLASS(image_plugin_class, 0);
#endif
*/

#ifdef STATIC_LAUNCHBAR
    REGISTER_PLUGIN_CLASS(launchbar_plugin_class, 0);
#endif

#ifdef STATIC_DCLOCK
    REGISTER_PLUGIN_CLASS(dclock_plugin_class, 0);
#endif

#ifdef STATIC_WINCMD
    REGISTER_PLUGIN_CLASS(wincmd_plugin_class, 0);
#endif

#ifdef STATIC_DIRMENU
    REGISTER_PLUGIN_CLASS(dirmenu_plugin_class, 0);
#endif

#ifdef STATIC_TASKBAR
    REGISTER_PLUGIN_CLASS(taskbar_plugin_class, 0);
#endif

#ifdef STATIC_PAGER
    REGISTER_PLUGIN_CLASS(pager_plugin_class, 0);
#endif

#ifdef STATIC_TRAY
    REGISTER_PLUGIN_CLASS(tray_plugin_class, 0);
#endif

#ifndef DISABLE_MENU
#ifdef STATIC_MENU
    REGISTER_PLUGIN_CLASS(menu_plugin_class, 0);
#endif
#endif

#ifdef STATIC_SPACE
    REGISTER_PLUGIN_CLASS(space_plugin_class, 0);
#endif

    RET();
}

GList* plugin_find_class( const char* type )
{
    GList *tmp;
    PluginClass *pc = NULL;
    for (tmp = pcl; tmp; tmp = g_list_next(tmp)) {
        pc = (PluginClass *) tmp->data;
        if (!g_ascii_strcasecmp(type, pc->type)) {
            LOG(LOG_INFO, "   already have it\n");
            break;
        }
    }
    return tmp;
}

static PluginClass*
plugin_load_dynamic( const char* type, const gchar* path )
{
    PluginClass *pc = NULL;
    GModule *m;
    gpointer tmpsym;
    char class_name[ 128 ];
    m = g_module_open(path, G_MODULE_BIND_LAZY);
    if (!m) {
        /* ERR("error is %s\n", g_module_error()); */
        RET(NULL);
    }
    g_snprintf( class_name, 128, "%s_plugin_class", type );

    if (!g_module_symbol(m, class_name, &tmpsym)
         || (pc = tmpsym) == NULL
         || strcmp(type, pc->type)) {
        g_module_close(m);
        ERR("%s.so is not a lxpanel plugin\n", type);
        RET(NULL);
    }
    pc->gmodule = m;
    register_plugin_class(pc, 1);
    return pc;
}

Plugin *
plugin_load(char *type)
{
    GList *tmp;
    PluginClass *pc = NULL;
    Plugin *plug = NULL;

    ENTER;
    if (!pcl)
        init_plugin_class_list();

    tmp = plugin_find_class( type );

    if( tmp ) {
        pc = (PluginClass *) tmp->data;
    }
#ifndef DISABLE_PLUGINS_LOADING
    else if ( g_module_supported() ) {
        gchar path[ PATH_MAX ];
        
#if 0   /* put plugins in config dir is too dirty... */
        g_snprintf(path, PATH_MAX, "%s/.lxpanel/plugins/%s.so", getenv("HOME"), type);
        pc = plugin_load_dynamic( type, path );
#endif
        if( !pc ) {
            g_snprintf(path, PATH_MAX, PACKAGE_LIB_DIR "/lxpanel/plugins/%s.so", type);
            pc = plugin_load_dynamic( type, path );
        }
    }
#endif  /* DISABLE_PLUGINS_LOADING */

    /* nothing was found */
    if (!pc)
        RET(NULL);

    plug = g_new0(Plugin, 1);
    g_return_val_if_fail (plug != NULL, NULL);
    plug->class = pc;
    pc->count++;
    RET(plug);
}


void plugin_put(Plugin *this)
{
    PluginClass *pc = this->class;
    ENTER;
    plugin_class_unref( pc );
    g_free(this);
    RET();
}

int
plugin_start(Plugin *this, char** fp)
{
    ENTER;

    DBG("%s\n", this->class->type);

    if (!this->class->constructor(this, fp)) {
//        if (!this->class->invisible)
//            gtk_widget_destroy(this->pwid);
        RET(0);
    }

    if (!this->class->invisible && this->pwid ) {
        /* this->pwid is created by the plugin */
        //this->pwid = gtk_bgbox_new();
        gtk_widget_set_name(this->pwid, this->class->type);
        gtk_box_pack_start(GTK_BOX(this->panel->box), this->pwid, this->expand, TRUE,
              this->padding);
        gtk_container_set_border_width(GTK_CONTAINER(this->pwid), this->border);

        gtk_widget_show(this->pwid);
    }
    RET(1);
}


void plugin_stop(Plugin *this)
{
    ENTER;
    /* g_debug("%s\n", this->class->type); */
    this->class->destructor(this);
    this->panel->plug_num--;
    if (!this->class->invisible && this->pwid )
        gtk_widget_destroy(this->pwid);
    /* this->pwid is destroyed in the dtor of plugins */
    RET();
}

void plugin_class_unref( PluginClass* pc )
{
    --pc->count;
    if (pc->count == 0 && pc->dynamic) {
        pcl = g_list_remove(pcl, pc);
        /* pc points now somewhere inside loaded lib, so if g_module_close
         * will touch it after dlclose (and 2.6 does) it will result in segfault */
        g_module_close(pc->gmodule);
    }
}

/*
   Get a list of all available plugin classes
   Return a newly allocated GList which should be freed with
   plugin_class_list_free( list );
*/
GList* plugin_get_available_classes()
{
    GList* classes = NULL;
    gchar *path;
    char *dir_path;
    const char* file;
    GDir* dir;
    GList* l;
    PluginClass *pc;

    if (!pcl)
        init_plugin_class_list();

    for( l = pcl; l; l = l->next ) {
        pc = (PluginClass*)l->data;
        classes = g_list_prepend( classes, pc );
        ++pc->count;
    }

#ifndef DISABLE_PLUGINS_LOADING
#if 0   /* Put plugins in config dir is too dirty... */
    dir_path = g_build_filename( g_get_home_dir(), ".lxpanel/plugins", NULL );
    if( dir = g_dir_open( dir_path, 0, NULL ) ) {
        while( file = g_dir_read_name( dir ) ) {
            GModule *m;
            char* type;
            if( ! g_str_has_suffix( file, ".so" ) )
                  continue;
            type = g_strndup( file, strlen(file) - 3 );
            l = plugin_find_class( type );
            if( l == NULL ) { /* If it has not been loaded */
                path = g_build_filename( dir_path, file, NULL );
                if( pc = plugin_load_dynamic( type, path ) ) {
                    ++pc->count;
                    classes = g_list_prepend( classes, pc );
                }
                g_free( path );
            }
            g_free( type );
        }
        g_dir_close( dir );
    }
    g_free( dir_path );
#endif
    if( dir = g_dir_open( PACKAGE_LIB_DIR "/lxpanel/plugins", 0, NULL ) ) {
        while( file = g_dir_read_name( dir ) ) {
            GModule *m;
            char* type;
            if( ! g_str_has_suffix( file, ".so" ) )
                  continue;
            type = g_strndup( file, strlen(file) - 3 );
            l = plugin_find_class( type );
            if( l == NULL ) { /* If it has not been loaded */
                path = g_build_filename( PACKAGE_LIB_DIR "/lxpanel/plugins", file, NULL );
                if( pc = plugin_load_dynamic( type, path ) ) {
                    ++pc->count;
                    classes = g_list_prepend( classes, pc );
                }
                g_free( path );
            }
            g_free( type );
        }
        g_dir_close( dir );
    }
#endif
    /* classes = g_list_reverse( classes ); */
    return classes;
}

void plugin_class_list_free( GList* classes )
{
   g_list_foreach( classes, (GFunc)plugin_class_unref, NULL );
   g_list_free( classes );
}

void
plugin_widget_set_background( GtkWidget* w, Panel* p )
{
	static gboolean in_tray = FALSE;
	gboolean is_tray;

    if( ! w )
        return;

    is_tray = ( GTK_IS_CONTAINER(w) && strcmp( gtk_widget_get_name( w ), "tray" ) == 0 );

    if( ! GTK_WIDGET_NO_WINDOW( w ) )
    {
        if( p->background || p->transparent )
        {
            gtk_widget_set_app_paintable( w, TRUE );
            if( GTK_WIDGET_REALIZED(w) )
                gdk_window_set_back_pixmap( w->window, NULL, TRUE );
        }
        else
        {
            gtk_widget_set_app_paintable( w, FALSE );
            if( GTK_WIDGET_REALIZED(w) )
            {
                gdk_window_set_back_pixmap( w->window, NULL, FALSE );

				/* dirty hack to fix the background color of tray icons :-( */
                if( in_tray )
					gdk_window_set_background( w->window, &w->style->bg[ GTK_STATE_NORMAL ] );
			}
        }
        // g_debug("%s has window (%s)", gtk_widget_get_name(w), G_OBJECT_TYPE_NAME(w) );
    }
    /*
    else
        g_debug("%s has NO window (%s)", gtk_widget_get_name(w), G_OBJECT_TYPE_NAME(w) );
    */
    if( GTK_IS_CONTAINER( w ) )
    {
    	if( is_tray )
    		in_tray = TRUE;

        gtk_container_foreach( (GtkContainer*)w, 
                (GtkCallback)plugin_widget_set_background, p );

        if( is_tray )
			in_tray = FALSE;
    }

    /* Dirty hack: Force repaint of tray icons!!
     * Normal gtk+ repaint cannot work across different processes.
     * So, we need some dirty tricks here.
     */
    if( is_tray )
    {
        gtk_widget_set_size_request( w, w->allocation.width, w->allocation.height );
        gtk_widget_hide (gtk_bin_get_child((GtkBin*)w));
        if( gtk_events_pending() )
            gtk_main_iteration();
        gtk_widget_show (gtk_bin_get_child((GtkBin*)w));
        if( gtk_events_pending() )
            gtk_main_iteration();
        gtk_widget_set_size_request( w, -1, -1 );
    }
}

void plugin_set_background( Plugin* pl, Panel* p )
{
    if( G_UNLIKELY( pl->class->invisible || ! pl->pwid ) )
        return;
    plugin_widget_set_background( pl->pwid, p );
}
