
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
#include "gtkbgbox.h"

//#define DEBUG
#include "dbg.h"


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

static const GtkTargetEntry target_table[] = {
    { "text/uri-list", 0, TARGET_URILIST},
    { "UTF8_STRING", 0, TARGET_UTF8_STRING },
    { "COMPOUND_TEXT", 0, 0 },
    { "TEXT",          0, 0 },
    { "STRING",        0, 0 }
};

static const char desktop_ent[] = "Desktop Entry";

typedef struct btn {
    //GtkWidget *button, *pixmap;
    gchar *action;
} btn;

#define MAXBUTTONS 20
typedef struct launchbar {
    GtkWidget *box;
    GtkTooltips *tips;
    btn btns[MAXBUTTONS];
    int btn_num;
    int iconsize;
} launchbar;



static gboolean
my_button_pressed(GtkWidget *widget, GdkEventButton *event, btn *b )
{
    GtkWidget *image;

    ENTER;
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
    RET(TRUE);
}

static void
launchbar_destructor(plugin *p)
{
    launchbar *lb = (launchbar *)p->priv;
    int i;

    ENTER;
    gtk_widget_destroy(lb->box);
    for (i = 0; i < lb->btn_num; i++) {
        g_free(lb->btns[i].action);
    }
    g_free(lb);
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
      btn              *b)
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
read_button(plugin *p)
{
    launchbar *lb = (launchbar *)p->priv;
    gchar *fname, *tooltip, *action, *desktop_id;
    //GdkPixbuf *gp, *gps;
    GtkWidget *button;
    line s;
    //GError *err = NULL;
    int w, h;
    
    ENTER;
    s.len = 256;
    if (lb->btn_num >= MAXBUTTONS) {
        ERR("launchbar: max number of buttons (%d) was reached. skipping the rest\n",
              lb->btn_num );
        RET(0);
    }

    tooltip = fname = action = desktop_id = NULL;
    while (lxpanel_get_line(p->fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_NONE) {
            ERR( "launchbar: illegal token %s\n", s.str);
            RET(0);
        }
        if (s.type == LINE_VAR) {
            if( !g_ascii_strcasecmp(s.t[0], "id") )
                desktop_id = g_strdup(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "image"))
                fname = expand_tilda(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "tooltip"))
                tooltip = g_strdup(s.t[1]);
            else if (!g_ascii_strcasecmp(s.t[0], "action"))
                action = g_strdup(s.t[1]);
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

    if( desktop_id ) {
        gchar *desktop_file = NULL;
        gchar *full_id = NULL;
        GKeyFile* desktop = g_key_file_new();
        full_id = g_strconcat( "applications/", desktop_id, NULL );
        g_free( desktop_id );
        if( g_key_file_load_from_data_dirs( desktop, full_id, &desktop_file,
                                            G_KEY_FILE_NONE, NULL ) )
        {
            gchar *icon = NULL, *title = NULL;
            icon = g_key_file_get_string( desktop, desktop_ent, "Icon", NULL);
            title = g_key_file_get_locale_string( desktop, desktop_ent,
                                                "Name", NULL, NULL);
            if( !fname && icon ){
                gchar* sep = strchr( icon, '.' );
                if( sep )
                    fname = g_strndup( icon, (sep - icon) );
                else
                    fname = icon;
            }
            if( ! action ) {
                gchar* exec;
                exec = g_key_file_get_string( desktop, desktop_ent, "Exec", NULL);
                action = translate_exec_to_cmd( exec, icon, title, desktop_file );
                g_free( exec );
            }
            if( ! tooltip )
                tooltip = title;
            if( fname != icon )
                g_free( icon );
            if( tooltip != title )
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
    button = fb_button_new_from_file(fname, w, h, 0x202020, TRUE);
    //gtk_container_set_border_width(GTK_CONTAINER(button), 0);
    g_signal_connect (G_OBJECT (button), "button-release-event",
          G_CALLBACK (my_button_pressed), (gpointer) &lb->btns[lb->btn_num]);
    g_signal_connect (G_OBJECT (button), "button-press-event",
          G_CALLBACK (my_button_pressed), (gpointer) &lb->btns[lb->btn_num]);


    GTK_WIDGET_UNSET_FLAGS (button, GTK_CAN_FOCUS);
    // DnD support
    gtk_drag_dest_set (GTK_WIDGET(button),
          GTK_DEST_DEFAULT_ALL, //GTK_DEST_DEFAULT_HIGHLIGHT,
          target_table, G_N_ELEMENTS (target_table),
          GDK_ACTION_COPY);
    g_signal_connect (G_OBJECT(button), "drag_data_received",
          G_CALLBACK (drag_data_received_cb),  (gpointer) &lb->btns[lb->btn_num]);

    gtk_box_pack_start(GTK_BOX(lb->box), button, FALSE, FALSE, 0);
    gtk_widget_show(button);
    //gtk_bgbox_set_background(button, BG_ROOT, 0xFFFFFF, 20);

    if (p->panel->transparent)
        gtk_bgbox_set_background(button, BG_ROOT, p->panel->tintcolor, p->panel->alpha);

    g_free(fname);
    // tooltip
    if (tooltip) {
        gtk_tooltips_set_tip(GTK_TOOLTIPS (lb->tips), button, tooltip, NULL);
        g_free(tooltip);
    }
 
    //gtk_container_add(GTK_CONTAINER(eb), button);
    lb->btns[lb->btn_num].action = action;
    lb->btn_num++;
    
    RET(1);

 error:
    g_free(fname);
    g_free(tooltip);
    g_free(action);
    RET(0);
}

static int
launchbar_constructor(plugin *p)
{
    launchbar *lb;
    line s;
    GtkRequisition req;
    static gchar *launchbar_rc = "style 'launchbar-style'\n"
        "{\n"
        "GtkWidget::focus-line-width = 0\n"
        "GtkWidget::focus-padding = 0\n"
        "GtkButton::default-border = { 0, 0, 0, 0 }\n"
        "GtkButton::default-outside-border = { 0, 0, 0, 0 }\n"
        "}\n"
        "widget '*' style 'launchbar-style'";
   
    ENTER;
    gtk_widget_set_name(p->pwid, "launchbar");
    gtk_rc_parse_string(launchbar_rc);
    get_button_spacing(&req, GTK_CONTAINER(p->pwid), "");
    
    lb = g_new0(launchbar, 1);
    g_return_val_if_fail(lb != NULL, 0);
    p->priv = lb;
    lb->box = p->panel->my_box_new(FALSE, 0);
    gtk_container_add(GTK_CONTAINER(p->pwid), lb->box);
    gtk_container_set_border_width (GTK_CONTAINER (lb->box), 0);
    gtk_widget_show(lb->box);
    lb->tips = gtk_tooltips_new();
    
    if  (p->panel->orientation == ORIENT_HORIZ)
        lb->iconsize = GTK_WIDGET(p->panel->box)->allocation.height;
    else
        lb->iconsize = GTK_WIDGET(p->panel->box)->allocation.width;
    DBG("button: req width=%d height=%d\n", req.width, req.height);
    DBG("iconsize=%d\n", lb->iconsize);
    
    s.len = 256;
    while (lxpanel_get_line(p->fp, &s) != LINE_BLOCK_END) {
        if (s.type == LINE_NONE) {
            ERR( "launchbar: illegal token %s\n", s.str);
            goto error;
        }
        if (s.type == LINE_BLOCK_START) {
            if (!g_ascii_strcasecmp(s.t[0], "button")) {
                if (!read_button(p)) {
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



plugin_class launchbar_plugin_class = {
    fname: NULL,
    count: 0,

    type : "launchbar",
    name : N_("Application Launch Bar"),
    version: "1.0",
    description : N_("Bar with buttons to launch application"),

    constructor : launchbar_constructor,
    destructor  : launchbar_destructor,
};
