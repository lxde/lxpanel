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
#include "gtkbgbox.h"


//#define DEBUG
#include "dbg.h"

static GList *pcl = NULL;


/* counter for static (built-in) plugins must be greater then zero
 * so lxpanel will not try to unload them */

#define REGISTER_PLUGIN_CLASS(pc, dynamic) \
do {\
    extern plugin_class pc;\
    register_plugin_class(&pc, dynamic);\
} while (0)


static void
register_plugin_class(plugin_class *pc, int dynamic)
{
    pcl = g_list_append(pcl, pc);
    pc->dynamic = dynamic;
    if (!pc->dynamic)
        pc->count++;
    /* reloading tray results in segfault due to registering static type in dll.
     * so keep it always onboard until bug fix */
    if (!strcmp(pc->type, "tray"))
        pc->count++;
}

static void
init_plugin_class_list()
{
    ENTER;
#ifdef STATIC_SEPARATOR
    REGISTER_PLUGIN_CLASS(separator_plugin_class, 0);
#endif

#ifdef STATIC_IMAGE
    REGISTER_PLUGIN_CLASS(image_plugin_class, 0);
#endif

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

#ifdef STATIC_MENU
    REGISTER_PLUGIN_CLASS(menu_plugin_class, 0);
#endif

#ifdef STATIC_SPACE
    REGISTER_PLUGIN_CLASS(space_plugin_class, 0);
#endif

#ifdef STATIC_ICONS
    REGISTER_PLUGIN_CLASS(icons_plugin_class, 0);
#endif

#ifdef STATIC_DESKNO
    REGISTER_PLUGIN_CLASS(deskno_plugin_class, 0);
#endif

    RET();
}





plugin *
plugin_load(char *type)
{
    GList *tmp;
    plugin_class *pc = NULL;
    plugin *plug = NULL;

    ENTER;
    if (!pcl)
        init_plugin_class_list();

    LOG(LOG_INFO, "loading %s plugin\n", type);
    for (tmp = pcl; tmp; tmp = g_list_next(tmp)) {
        pc = (plugin_class *) tmp->data;
        if (!g_ascii_strcasecmp(type, pc->type)) {
            LOG(LOG_INFO, "   already have it\n");
            break;
        }
    }

#ifndef DISABLE_PLUGINS_LOADING
    if (!tmp && g_module_supported()) {
        GModule *m;
        static GString *str = NULL;
        gpointer tmpsym;
        if (!str)
            str = g_string_sized_new(PATH_MAX);
        g_string_printf(str, "%s/.lxpanel/plugins/%s.so", getenv("HOME"), type);
        m = g_module_open(str->str, G_MODULE_BIND_LAZY);
        LOG(LOG_INFO, "   %s ... %s\n", str->str, m ? "ok" : "no");
        if (!m) {
            DBG("error is %s\n", g_module_error());
            g_string_printf(str, PACKAGE_LIB_DIR "/lxpanel/plugins/%s.so", type);
            m = g_module_open(str->str, G_MODULE_BIND_LAZY);
            LOG(LOG_INFO, "   %s ... %s\n", str->str, m ? "ok" : "no");
            if (!m) {
                ERR("error is %s\n", g_module_error());
                RET(NULL);
            }
        }
        g_string_printf(str, "%s_plugin_class", type);
        if (!g_module_symbol(m, str->str, &tmpsym) || (pc = tmpsym) == NULL
              || strcmp(type, pc->type)) {
            g_module_close(m);
            ERR("%s.so is not a lxpanel plugin\n", type);
            RET(NULL);
        }
        DBG("3\n");
        pc->gmodule = m;
        register_plugin_class(pc, 1);
        DBG("4\n");
    }
#endif 	/* DISABLE_PLUGINS_LOADING */

    /* nothing was found */
    if (!pc)
        RET(NULL);

    plug = g_new0(plugin, 1);
    g_return_val_if_fail (plug != NULL, NULL);
    plug->class = pc;
    pc->count++;
    RET(plug);
}


void plugin_put(plugin *this)
{
    plugin_class *pc = this->class;
    GModule *tmp;

    ENTER;
    pc->count--;
    if (pc->count == 0 && pc->dynamic) {
        pcl = g_list_remove(pcl, pc);
        /* pc points now somewhere inside loaded lib, so if g_module_close
         * will touch it after dlclose (and 2.6 does) it will result in segfault */
        tmp = pc->gmodule;
        g_module_close(tmp);
    }
    g_free(this);
    RET();
}

int
plugin_start(plugin *this, char** fp)
{
    ENTER;

    DBG("%s\n", this->class->type);
    if (!this->class->invisible) {
        this->pwid = gtk_bgbox_new();
        gtk_widget_set_name(this->pwid, this->class->type);
        gtk_box_pack_start(GTK_BOX(this->panel->box), this->pwid, this->expand, TRUE,
              this->padding);
        gtk_container_set_border_width(GTK_CONTAINER(this->pwid), this->border);
        if (this->panel->transparent) {
            gtk_bgbox_set_background(this->pwid, BG_ROOT, this->panel->tintcolor, this->panel->alpha);
        }
        gtk_widget_show(this->pwid);
    }
    if (!this->class->constructor(this, fp)) {
        if (!this->class->invisible)
            gtk_widget_destroy(this->pwid);
        RET(0);
    }
    RET(1);
}


void plugin_stop(plugin *this)
{
    ENTER;
    DBG("%s\n", this->class->type);
    this->class->destructor(this);
    this->panel->plug_num--;
    if (!this->class->invisible)
        gtk_widget_destroy(this->pwid);
    RET();
}

