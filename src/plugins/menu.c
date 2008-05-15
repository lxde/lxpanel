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

#include <stdlib.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "bg.h"

#include "ptk-app-menu.h"

#include "dbg.h"

/*
 * SuxPanel version 0.1
 * Copyright (c) 2003 Leandro Pereira <leandro@linuxmag.com.br>
 */

/*
 * menu style code was taken from suxpanel
 */

typedef struct {
    GtkTooltips *tips;
    GtkWidget *menu, *box, *bg, *label;
    char *fname, *caption;
    gulong handler_id;
    int iconsize, paneliconsize;
    GSList *files;
    gboolean has_system_menu;
    char* config_data;
    int sysmenu_pos;
    char *config_start, *config_end;
} menup;

static guint idle_loader = 0;

static void
menu_destructor(Plugin *p)
{
    menup *m = (menup *)p->priv;

    ENTER;

    if( G_UNLIKELY( idle_loader ) )
    {
        g_source_remove( idle_loader );
        idle_loader = 0;
    }
    if( m->has_system_menu )
        p->panel->system_menus = g_slist_remove( p->panel->system_menus, p );

    g_signal_handler_disconnect(G_OBJECT(m->bg), m->handler_id);
    gtk_widget_destroy(m->menu);
    /* The widget is destroyed in plugin_stop().
    gtk_widget_destroy(m->box);
    */
    g_free(m->fname);
    g_free(m->caption);
    g_free(m);
    RET();
}

static void
spawn_app(GtkWidget *widget, gpointer data)
{
    GError *error = NULL;

    ENTER;
    if (data) {
        if (! g_spawn_command_line_async(data, &error) ) {
            ERR("can't spawn %s\nError is %s\n", (char *)data, error->message);
            g_error_free (error);
        }
    }
    RET();
}


static void
run_command(GtkWidget *widget, void (*cmd)(void))
{
    ENTER;
    cmd();
    RET();
}

static void
menu_pos(GtkMenu *menu, gint *x, gint *y, gboolean *push_in, GtkWidget *widget)
{
    int ox, oy, w, h;
    Plugin *p;

    ENTER;
    p = g_object_get_data(G_OBJECT(widget), "plugin");
    gdk_window_get_origin(widget->window, &ox, &oy);
    w = GTK_WIDGET(menu)->requisition.width;
    h = GTK_WIDGET(menu)->requisition.height;
    if (p->panel->orientation == ORIENT_HORIZ) {
        *x = ox;
        if (*x + w > gdk_screen_width())
            *x = ox + widget->allocation.width - w;
        *y = oy - h;
        if (*y < 0)
            *y = oy + widget->allocation.height;
    } else {
        *x = ox + widget->allocation.width;
        if (*x > gdk_screen_width())
            *x = ox - w;
        *y = oy;
        if (*y + h >  gdk_screen_height())
            *y = oy + widget->allocation.height - h;
    }
    DBG("widget: x,y=%d,%d  w,h=%d,%d\n", ox, oy,
          widget->allocation.width, widget->allocation.height );
    DBG("w-h %d %d\n", w, h);
    *push_in = TRUE;
    RET();
}

static void
reload_system_menu( GtkMenu* menu )
{
    GList *children, *child;
    GtkMenuItem* item;
    GtkWidget* sub_menu;
    gint idx;
    children = gtk_container_get_children( GTK_CONTAINER(menu) );
    for( child = children, idx = 0; child; child = child->next, ++idx ) {
        item = GTK_MENU_ITEM( child->data );
        if( ptk_app_menu_item_has_data( item ) ) {
            do {
                item = GTK_MENU_ITEM( child->data );
                child = child->next;
                gtk_widget_destroy( GTK_WIDGET(item) );
            }while( child && ptk_app_menu_item_has_data( child->data ) );
            ptk_app_menu_insert_items( menu, idx );
            if( ! child )
                break;
        }
        else if( ( sub_menu = gtk_menu_item_get_submenu( item ) ) ) {
            reload_system_menu( GTK_MENU(sub_menu) );
        }
    }
    g_list_free( children );
}

static void show_menu( GtkWidget* widget, Plugin* p, int btn, guint32 time )
{
    menup* m = (menup*)p->priv;
    /* reload system menu items if needed */
    if( m->has_system_menu && ptk_app_menu_need_reload() ) {
        GSList* l;
        /* FIXME: Reload all system menus here.
                  This is dirty, but I don't know any better way. */
        for( l = p->panel->system_menus; l; l = l->next ) {
            Plugin* _p = (Plugin*)l->data;
            menup* _m = (menup*)_p->priv;
            reload_system_menu( GTK_MENU(_m->menu) );
        }
    }
    gtk_menu_popup(GTK_MENU(m->menu),
                   NULL, NULL,
                   (GtkMenuPositionFunc)menu_pos, widget,
                   btn, time);
}

static gboolean
my_button_pressed(GtkWidget *widget, GdkEventButton *event, Plugin* plugin)
{
    ENTER;

    if( event->button == 3 )  /* right button */
    {
        GtkMenu* popup = lxpanel_get_panel_menu( plugin->panel, plugin, FALSE );
        gtk_menu_popup( popup, NULL, NULL, NULL, NULL, event->button, event->time );
        return TRUE;
    }

    if ((event->type == GDK_BUTTON_PRESS)
          && (event->x >=0 && event->x < widget->allocation.width)
          && (event->y >=0 && event->y < widget->allocation.height)) {
        show_menu( widget, plugin, event->button, event->time );
    }
    RET(TRUE);
}

gboolean show_system_menu( gpointer system_menu )
{
    Plugin* p = (Plugin*)system_menu;
    menup* m = (menup*)p->priv;
    show_menu( m->bg, p, 0, GDK_CURRENT_TIME );
    return FALSE;
}

static GtkWidget *
make_button(Plugin *p, gchar *fname, gchar *name, GdkColor* tint, GtkWidget *menu)
{
    int w, h;
    char* title = NULL;
    menup *m;

    ENTER;
    m = (menup *)p->priv;
    m->menu = menu;
    if (p->panel->orientation == ORIENT_HORIZ) {
        w = 10000;
        h = p->panel->ah;
    } else {
        w = p->panel->aw;
        h = 10000;
    }

    if( name )
    {
        /* load the name from *.directory file if needed */
        if( g_str_has_suffix( name, ".directory" ) )
        {
            GKeyFile* kf = g_key_file_new();
            char* dir_file = g_build_filename( "desktop-directories", name, NULL ); 
            if( g_key_file_load_from_data_dirs( kf, dir_file, NULL, 0, NULL ) )
            {
                title = g_key_file_get_locale_string( kf, "Desktop Entry", "Name", NULL, NULL );
            }
            g_free( dir_file );
            g_key_file_free( kf );
        }
        else
            title = name;

        /* FIXME: handle orientation problems */
        if (p->panel->usefontcolor)
            m->bg = fb_button_new_from_file_with_colorlabel(fname, w, h, gcolor2rgb24(tint),
                p->panel->fontcolor, TRUE, title);
        else
            m->bg = fb_button_new_from_file_with_label(fname, w, h, gcolor2rgb24(tint), TRUE, title);

        if( title != name )
            g_free( title );
    }
    else
    {
        m->bg = fb_button_new_from_file(fname, w, h, gcolor2rgb24(tint), TRUE );
    }

    gtk_widget_show(m->bg);
    gtk_box_pack_start(GTK_BOX(m->box), m->bg, FALSE, FALSE, 0);

    m->handler_id = g_signal_connect (G_OBJECT (m->bg), "button-press-event",
          G_CALLBACK (my_button_pressed), p);
    g_object_set_data(G_OBJECT(m->bg), "plugin", p);

    RET(m->bg);
}


static GtkWidget *
read_item(Plugin *p, char** fp)
{
    line s;
    gchar *name, *fname, *action;
    GtkWidget *item;
    menup *m = (menup *)p->priv;
    Command *cmd_entry = NULL;

    ENTER;
    s.len = 256;
    name = fname = action = NULL;

    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "image"))
                    fname = expand_tilda(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "name"))
                    name = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "action"))
                    action = g_strdup(s.t[1]);
                else if (!g_ascii_strcasecmp(s.t[0], "command")) {
                    Command *tmp;

                    for (tmp = commands; tmp->name; tmp++) {
                        if (!g_ascii_strcasecmp(s.t[1], tmp->name)) {
                            cmd_entry = tmp;
                            break;
                        }
                    }
                } else {
                    ERR( "menu/item: unknown var %s\n", s.t[0]);
                    goto error;
                }
            }
        }
    }
    /* menu button */
    if( cmd_entry ) /* built-in commands */
    {
        item = gtk_image_menu_item_new_with_label( _(cmd_entry->disp_name) );
        g_signal_connect(G_OBJECT(item), "activate", (GCallback)run_command, cmd_entry->cmd);
    }
    else
    {
        item = gtk_image_menu_item_new_with_label(name ? name : "");
        if (action) {
            g_signal_connect(G_OBJECT(item), "activate", (GCallback)spawn_app, action);
        }
    }
    gtk_container_set_border_width(GTK_CONTAINER(item), 0);
    g_free(name);
    if (fname) {
        GtkWidget *img;

        img = gtk_image_new_from_file_scaled(fname, m->iconsize, m->iconsize, TRUE);
        gtk_widget_show(img);
        gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item), img);
        g_free(fname);
    }
    RET(item);

 error:
    g_free(fname);
    g_free(name);
    g_free(action);
    RET(NULL);
}

static GtkWidget *
read_separator(Plugin *p, char **fp)
{
    line s;

    ENTER;
    s.len = 256;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            ERR("menu: error - separator can not have paramteres\n");
            RET(NULL);
        }
    }
    RET(gtk_separator_menu_item_new());
}

static gboolean on_idle( Panel* p )
{
    GSList* l;
    /* Reload all system menus here.
        This is dirty, but I don't know any better way. */
    for( l = p->system_menus; l; l = l->next ) {
        Plugin* _p = (Plugin*)l->data;
        menup* _m = (menup*)_p->priv;
        reload_system_menu( GTK_MENU(_m->menu) );
    }
    idle_loader = 0;
    return FALSE;   /* remove the handler */
}

static void
read_system_menu(GtkMenu* menu, Plugin *p, char** fp)
{
   line s;
   menup *m = (menup *)p->priv;
    GtkWidget* fake;

   ENTER;
   s.len = 256;
   if( fp )
   {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            ERR("menu: error - system can not have paramteres\n");
            RET();
        }
   }

   /* ptk_app_menu_insert_items( menu, -1 ); */
   /* Don't load the real system menu here to speed up startup.
    * Let's add a fake item to cheat PtkAppMenu as a place holder,
    * and we utilize reload_system_menu() to load the real menu later. */
    fake = gtk_separator_menu_item_new();
    PTK_APP_MENU_ITEM_ID = g_quark_from_static_string( "PtkAppMenuItem" );
    g_object_set_qdata( fake, PTK_APP_MENU_ITEM_ID, GUINT_TO_POINTER(TRUE) );
   gtk_menu_shell_append( menu, fake);

   m->has_system_menu = TRUE;

   p->panel->system_menus = g_slist_append( p->panel->system_menus, p );

    if( idle_loader == 0 )  /* delay the loading, and do it in idle handler */
        idle_loader = g_idle_add( (GSourceFunc)on_idle, p->panel );

   RET();
}

static void
read_include(Plugin *p, char **fp)
{
    ENTER;
#if 0
    gchar *name;
    line s;
    menup *m = (menup *)p->priv;
    /* FIXME: this is disabled */
    ENTER;
    s.len = 256;
    name = NULL;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "name"))
                    name = expand_tilda(s.t[1]);
                else  {
                    ERR( "menu/include: unknown var %s\n", s.t[0]);
                    RET();
                }
            }
        }
    }
    if ((fp = fopen(name, "r"))) {
        LOG(LOG_INFO, "Including %s\n", name);
        m->files = g_slist_prepend(m->files, fp);
        p->fp = fp;
    } else {
        ERR("Can't include %s\n", name);
    }
    if (name) g_free(name);
#endif
    RET();
}

static GtkWidget *
read_submenu(Plugin *p, char** fp, gboolean as_item)
{
    line s;
    GtkWidget *mi, *menu;
    gchar name[256], *fname;
    menup *m = (menup *)p->priv;
    GdkColor color={0, 0, 36 * 0xffff / 0xff, 96 * 0xffff / 0xff};

    ENTER;


    s.len = 256;
    menu = gtk_menu_new ();
    gtk_container_set_border_width(GTK_CONTAINER(menu), 0);

    fname = 0;
    name[0] = 0;
    while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_BLOCK_START) {
            mi = NULL;
            if (!g_ascii_strcasecmp(s.t[0], "item")) {
                mi = read_item(p, fp);
            } else if (!g_ascii_strcasecmp(s.t[0], "separator")) {
                mi = read_separator(p, fp);
            } else if (!g_ascii_strcasecmp(s.t[0], "system")) {
                read_system_menu(GTK_MENU(menu), p, fp); /* add system menu items */
                continue;
            } else if (!g_ascii_strcasecmp(s.t[0], "menu")) {
                mi = read_submenu(p, fp, TRUE);
            } else if (!g_ascii_strcasecmp(s.t[0], "include")) {
                read_include(p, fp);
                continue;
            } else {
                ERR("menu: unknown block %s\n", s.t[0]);
                goto error;
            }
            if (!mi) {
                ERR("menu: can't create menu item\n");
                goto error;
            }
            gtk_widget_show(mi);
            gtk_menu_shell_append (GTK_MENU_SHELL (menu), mi);
        } else if (s.type == LINE_VAR) {
            m->config_start = *fp;
            if (!g_ascii_strcasecmp(s.t[0], "image"))
                fname = expand_tilda(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "name"))
                strcpy(name, s.t[1]);
	    /* FIXME: tintcolor will not be saved.  */
            else if (!g_ascii_strcasecmp(s.t[0], "tintcolor"))
                gdk_color_parse( s.t[1], &color);
            else {
                ERR("menu: unknown var %s\n", s.t[0]);
                goto error;
            }
        } else if (s.type == LINE_NONE) {
            if (m->files) {
                /*
                  fclose(p->fp);
                  p->fp = m->files->data;
                */
                m->files = g_slist_delete_link(m->files, m->files);
            }
        }  else {
            ERR("menu: illegal in this context %s\n", s.str);
            goto error;
        }
    }
    if (as_item) {
        mi = gtk_image_menu_item_new_with_label(name ? name : "");
        if (fname) {
            GtkWidget *img;
            img = gtk_image_new_from_file_scaled(fname, m->iconsize, m->iconsize, TRUE);
            gtk_widget_show(img);
            gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), img);
            g_free(fname);
        }
        gtk_menu_item_set_submenu (GTK_MENU_ITEM (mi), menu);
        RET(mi);
    } else {
        m->fname = g_strdup(fname);
        m->caption = g_strdup(name);
        mi = make_button(p, fname, name, &color, menu);
        if (fname)
            g_free(fname);
        RET(mi);
    }

 error:
    // FIXME: we need to recursivly destroy all child menus and their items
    gtk_widget_destroy(menu);
    g_free(fname);
    g_free(name);
    RET(NULL);
}

static int
menu_constructor(Plugin *p, char **fp)
{
    menup *m;
    static char default_config[] =
        "image=" PACKAGE_DATA_DIR "/lxpanel/images/my-computer.png\n"
        "system {\n"
        "}\n"
        "separator {\n"
        "}\n"
        "item {\n"
            "command=run\n"
        "}\n"
        "separator {\n"
        "}\n"
        "item {\n"
            "image=gnome-logout\n"
            "command=logout\n"
        "}\n"
        "}\n";
    char *config_start, *config_end, *config_default = default_config;

    ENTER;
    m = g_new0(menup, 1);
    g_return_val_if_fail(m != NULL, 0);
    m->fname = NULL;
    m->caption = NULL;
    p->priv = m;

    //gtk_rc_parse_string(menu_rc);
    if  (p->panel->orientation == ORIENT_HORIZ)
        m->paneliconsize = p->panel->ah
            - 2* GTK_WIDGET(p->panel->box)->style->ythickness;
    else
        m->paneliconsize = p->panel->aw
            - 2* GTK_WIDGET(p->panel->box)->style->xthickness;
    m->iconsize = 22;

    m->box = gtk_hbox_new(FALSE, 0);
    gtk_container_set_border_width(GTK_CONTAINER(m->box), 0);

    if( ! fp )
        fp = &config_default;

    m->config_start = *fp;
    if (!read_submenu(p, fp, FALSE)) {
        ERR("menu: plugin init failed\n");
        goto error;
    }
    m->config_end = *fp - 1;
    while( *m->config_end != '}' && m->config_end > m->config_start ) {
        --m->config_end;
    }
    if( *m->config_end == '}' )
        --m->config_end;

    m->config_data = g_strndup( m->config_start,
                                (m->config_end-m->config_start) );

    p->pwid = m->box;

    RET(1);

 error:
    menu_destructor(p);
    RET(0);
}

static void save_config( Plugin* p, FILE* fp )
{
    menup* menu = (menup*)p->priv;
    lxpanel_put_str( fp, "name", menu->caption );
    lxpanel_put_str( fp, "image", menu->fname );
    if( menu->config_data ) {
        char** lines = g_strsplit( menu->config_data, "\n", 0 );
        char** line;
        for( line = lines; *line; ++line ) {
            g_strstrip( *line );
            if( **line )
                lxpanel_put_line( fp, *line );
        }
        g_strfreev( lines );
    }
}

static void apply_config(Plugin* p)
{
    /* FIXME: update menu for new setting */
}

static void menu_config( Plugin *p, GtkWindow* parent )
{
    GtkWidget* dlg;
    menup* menu = (menup*)p->priv;
    dlg = create_generic_config_dlg( _(p->class->name),
                                     GTK_WIDGET(parent),
                                    (GSourceFunc) apply_config, (gpointer) p,
                                     _("Icon"), &menu->fname, G_TYPE_STRING,
                                     _("Caption"), &menu->caption, G_TYPE_STRING,
                                     NULL );
    gtk_window_present( GTK_WINDOW(dlg) );
}

PluginClass menu_plugin_class = {
    fname: NULL,
    count: 0,

    type : "menu",
    name : N_("Menu"),
    version: "1.0",
    description : N_("Provide Menu"),

    constructor : menu_constructor,
    destructor  : menu_destructor,
    config : menu_config,
    save : save_config
};

