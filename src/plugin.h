#ifndef PLUGIN_H
#define PLUGIN_H

#include <gmodule.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <stdio.h>
#include "panel.h"

struct _plugin *stam;

typedef struct {
    /* common */
    char *fname;
    int count;
    GModule *gmodule;

    int dynamic : 1;
    int invisible : 1;
    /* these fields are pointers to the data within loaded dll */
    char *type;
    char *name;
    char *version;
    char *description;

    int (*constructor)(struct _plugin *this);
    void (*destructor)(struct _plugin *this);
    GtkWidget* (*config)(struct _plugin *this); /* config UI */

} plugin_class;

typedef struct _plugin{
    plugin_class *class;
    panel        *panel;
    FILE         *fp;
    GtkWidget    *pwid;
    int           expand;
    int           padding;
    int           border;
    gpointer      priv;
} plugin;

/* if plugin is external it will load its dll */
plugin * plugin_load(char *type);
void plugin_put(plugin *this);
int plugin_start(plugin *this);
void plugin_stop(plugin *this);

/* FIXME: optional definitions */
#define STATIC_SEPARATOR
#define STATIC_IMAGE
#define STATIC_LAUNCHBAR
#define STATIC_DCLOCK
#define STATIC_WINCMD
#define STATIC_TEST
#define STATIC_TASKBAR
#define STATIC_PAGER
#define STATIC_TRAY
#define STATIC_MENU
#define STATIC_SPACE
#define STATIC_ICONS
#define STATIC_DESKNO

#endif
