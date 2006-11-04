#include <stdlib.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>

#include "panel.h"
#include "misc.h"
#include "plugin.h"

//#define DEBUG
#include "dbg.h"


typedef struct {
    int size;
    GtkWidget *mainw;

} space;



static void
space_destructor(plugin *p)
{
    space *sp = (space *)p->priv;

    ENTER;
    gtk_widget_destroy(sp->mainw);
    g_free(sp);
    RET();
}




static int
space_constructor(plugin *p, char** fp)
{
    space *sp;
    line s;
    int w, h;

    ENTER;
    s.len = 256;  
    sp = g_new0(space, 1);
    g_return_val_if_fail(sp != NULL, 0);
    p->priv = sp;
    if( fp )
    {
        while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
            if (s.type == LINE_NONE) {
                ERR( "space: illegal token %s\n", s.str);
                goto error;
            }
            if (s.type == LINE_VAR) {
                if (!g_ascii_strcasecmp(s.t[0], "size")) 
                    sp->size = atoi(s.t[1]);
                else {
                    ERR( "space: unknown var %s\n", s.t[0]);
                    goto error;
                }
            } else {
                ERR( "space: illegal in this context %s\n", s.str);
                goto error;
            }
        }
    }
    if (!sp->size)
        sp->size = 1;
    sp->mainw = gtk_vbox_new(TRUE, 0);
    gtk_widget_show(sp->mainw);
    if (p->panel->orientation == ORIENT_HORIZ) {
        h = 2;
        w = sp->size;
    } else {
        w = 2;
        h = sp->size;
    } 
    gtk_widget_set_size_request(sp->mainw, w, h);
    gtk_container_set_border_width(GTK_CONTAINER(sp->mainw), 0);
    //gtk_container_add(GTK_CONTAINER(p->pwid), sp->mainw);
    gtk_widget_set_size_request(p->pwid, w, h);

    RET(1);

 error:
    space_destructor(p);
    RET(0);
}

static void apply_config( plugin* p )
{
    int w, h;
    space *sp = (space *)p->priv;
    /* Apply settings */
    if (p->panel->orientation == ORIENT_HORIZ) {
        h = 2;
        w = sp->size;
    } else {
        w = 2;
        h = sp->size;
    }
    gtk_widget_set_size_request(p->pwid, w, h);
}

static void space_config(plugin* p, GtkWindow* parent )
{
    GtkWidget* dlg;
    space *sp = (space *)p->priv;
    dlg =  create_generic_config_dlg( _(p->class->name),
                                     GTK_WIDGET(parent),
                                     (GSourceFunc) apply_config, (gpointer) p,
                                     _("Size"), &sp->size, G_TYPE_INT,  NULL );
    gtk_window_present( GTK_WINDOW(dlg) );
}

static void save_config( plugin* p, FILE* fp )
{
    space *sp = (space *)p->priv;
    lxpanel_put_int( fp, "Size", sp->size );
}

plugin_class space_plugin_class = {
    fname: NULL,
    count: 0,

    type : "space",
    name : N_("<Space>"),
    version: "1.0",
    description : "just allocate space",

    constructor : space_constructor,
    destructor  : space_destructor,
    config : space_config,
    save : save_config
};
