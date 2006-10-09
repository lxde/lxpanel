#include <stdlib.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "bg.h"
#include "gtkbgbox.h"

#include "ptk-app-menu.h"

//#define DEBUG
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
    gulong handler_id;
    int iconsize, paneliconsize;
    GSList *files;
    gboolean has_system_menu;
} menup;

static void
menu_destructor(plugin *p)
{
    menup *m = (menup *)p->priv;

    ENTER;
    g_signal_handler_disconnect(G_OBJECT(m->bg), m->handler_id);
    gtk_widget_destroy(m->menu);
    gtk_widget_destroy(m->box);
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
    plugin *p;

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

static void show_menu( GtkWidget* widget, menup* m, int btn, guint32 time )
{
    /* reload system menu items if needed */
    if( m->has_system_menu && ptk_app_menu_need_reload() ) {
        reload_system_menu( GTK_MENU(m->menu) );
    }
    gtk_menu_popup(GTK_MENU(m->menu),
                   NULL, NULL,
                   (GtkMenuPositionFunc)menu_pos, widget,
                   btn, time);
}

static gboolean
my_button_pressed(GtkWidget *widget, GdkEventButton *event, menup* m)
{
    ENTER;
    if ((event->type == GDK_BUTTON_PRESS)
          && (event->x >=0 && event->x < widget->allocation.width)
          && (event->y >=0 && event->y < widget->allocation.height)) {
        show_menu( widget, m, event->button, event->time );
    }
    RET(TRUE);
}

gboolean show_system_menu( gpointer system_menu )
{
    menup* m = (menup*)system_menu;
    show_menu( m->bg, m, 0, GDK_CURRENT_TIME );
    return FALSE;
}

static GtkWidget *
make_button(plugin *p, gchar *fname, gchar *name, GtkWidget *menu)
{
    int w, h;
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
    m->bg = fb_button_new_from_file_with_label(fname, w, h, 0xFF0000, TRUE,
          (p->panel->orientation == ORIENT_HORIZ ? name : NULL));
    gtk_widget_show(m->bg);
    gtk_box_pack_start(GTK_BOX(m->box), m->bg, FALSE, FALSE, 0);
    if (p->panel->transparent)
        gtk_bgbox_set_background(m->bg, BG_ROOT, p->panel->tintcolor, p->panel->alpha);


    m->handler_id = g_signal_connect (G_OBJECT (m->bg), "button-press-event",
          G_CALLBACK (my_button_pressed), m);
    g_object_set_data(G_OBJECT(m->bg), "plugin", p);

    RET(m->bg);
}


static GtkWidget *
read_item(plugin *p)
{
    line s;
    gchar *name, *fname, *action;
    GtkWidget *item;
    menup *m = (menup *)p->priv;
    command *cmd_entry = NULL;

    ENTER;
    s.len = 256;
    name = fname = action = NULL;

    while (get_line(p->fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_VAR) {
            if (!g_ascii_strcasecmp(s.t[0], "image"))
                fname = expand_tilda(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "name"))
                name = g_strdup(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "action"))
                action = g_strdup(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "command")) {
                command *tmp;

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
read_separator(plugin *p)
{
    line s;

    ENTER;
    s.len = 256;
    while (get_line(p->fp, &s) != LINE_BLOCK_END) {
        ERR("menu: error - separator can not have paramteres\n");
        RET(NULL);
    }
    RET(gtk_separator_menu_item_new());
}

static void
read_system_menu(GtkMenu* menu, plugin *p)
{
   line s;
   menup *m = (menup *)p->priv;

   ENTER;
   s.len = 256;
   while (get_line(p->fp, &s) != LINE_BLOCK_END) {
      ERR("menu: error - system can not have paramteres\n");
      RET();
   }
   if( p->panel->system_menu ) {
      ERR("menu: error - only one system is allowed");
   }
   ptk_app_menu_insert_items( menu, -1 );
   m->has_system_menu = TRUE;

   p->panel->system_menu = m;

   RET();
}

static void
read_include(plugin *p)
{
    gchar *name;
    line s;
    menup *m = (menup *)p->priv;
    FILE *fp;

    ENTER;
    s.len = 256;
    name = NULL;
    while (get_line(p->fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_VAR) {
            if (!g_ascii_strcasecmp(s.t[0], "name"))
                name = expand_tilda(s.t[1]);
            else  {
                ERR( "menu/include: unknown var %s\n", s.t[0]);
                RET();
            }
        }
    }
    if ((fp = fopen(name, "r"))) {
        LOG(LOG_INFO, "Including %s\n", name);
        m->files = g_slist_prepend(m->files, p->fp);
        p->fp = fp;
    } else {
        ERR("Can't include %s\n", name);
    }
    if (name) g_free(name);
    RET();
}

static GtkWidget *
read_submenu(plugin *p, gboolean as_item)
{
    line s;
    GtkWidget *mi, *menu;
    gchar name[256], *fname;
    menup *m = (menup *)p->priv;


    ENTER;
    s.len = 256;
    menu = gtk_menu_new ();
    gtk_container_set_border_width(GTK_CONTAINER(menu), 0);

    fname = 0;
    name[0] = 0;
    while (get_line(p->fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_BLOCK_START) {
            mi = NULL;
            if (!g_ascii_strcasecmp(s.t[0], "item")) {
                mi = read_item(p);
            } else if (!g_ascii_strcasecmp(s.t[0], "separator")) {
                mi = read_separator(p);
            } else if (!g_ascii_strcasecmp(s.t[0], "system")) {
                read_system_menu(GTK_MENU(menu), p); /* add system menu items */
                continue;
            } else if (!g_ascii_strcasecmp(s.t[0], "menu")) {
                mi = read_submenu(p, TRUE);
            } else if (!g_ascii_strcasecmp(s.t[0], "include")) {
                read_include(p);
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
            if (!g_ascii_strcasecmp(s.t[0], "image"))
                fname = expand_tilda(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "name"))
                strcpy(name, s.t[1]);
            else {
                ERR("menu: unknown var %s\n", s.t[0]);
                goto error;
            }
        } else if (s.type == LINE_NONE) {
            if (m->files) {
                fclose(p->fp);
                p->fp = m->files->data;
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
        mi = make_button(p, fname, name, menu);
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
menu_constructor(plugin *p)
{
    menup *m;

    ENTER;
    m = g_new0(menup, 1);
    g_return_val_if_fail(m != NULL, 0);
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
    gtk_container_add(GTK_CONTAINER(p->pwid), m->box);

    if (!read_submenu(p, FALSE)) {
        ERR("menu: plugin init failed\n");
        goto error;
    }
    RET(1);

 error:
    menu_destructor(p);
    RET(0);
}


plugin_class menu_plugin_class = {
    fname: NULL,
    count: 0,

    type : "menu",
    name : "menu",
    version: "1.0",
    description : "Provide Menu",

    constructor : menu_constructor,
    destructor  : menu_destructor,
};


/*
if (level == 0) {

        } else
*/
