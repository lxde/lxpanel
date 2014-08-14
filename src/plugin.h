/*
 * Copyright (c) 2014 LxDE Developers, see the file AUTHORS for details.
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

#ifndef __PLUGIN_H__
#define __PLUGIN_H__ 1

#include <libfm/fm.h>

#include "panel.h"
#include "conf.h"

G_BEGIN_DECLS

/* New plugins style which uses FmModule loader, our module type is "lxpanel_gtk" */

#define FM_MODULE_lxpanel_gtk_VERSION 1 /* version of this API */

/**
 * LXPanelPluginInit:
 * @init: (allow-none): callback on lxpanel start
 * @finalize: (allow-none): callback on lxpanel exit
 * @name: name to represent plugin in lists
 * @description: tooltip on the plugin in lists
 * @new_instance: callback to create new plugin instance in panel
 * @config: (allow-none): callback to show configuration dialog
 * @reconfigure: (allow-none): callback to apply panel configuration change
 * @button_press_event: (allow-none): callback on "button-press-event" signal
 * @show_system_menu: (allow-none): callback to queue show system menu
 * @update_context_menu: (allow-none): callback to update context menu
 * @control: (allow-none): callback to pass messages from lxpanelctl
 *
 * Callback @init is called on module loading, only once per application
 * lifetime.
 *
 * Callback @new_instance is called when panel tries to add some plugin.
 * It should initialize instance data, prepare widget, and return the
 * widget or %NULL if something went wrong. The @panel and @settings data
 * are guaranteed to be valid until gtk_widget_destroy() is called on the
 * instance. Instance own data should be attached to the instance using
 * lxpanel_plugin_set_data().
 *
 * Callback @config is called when user tries to configure the instance
 * (either via context menu or from plugins selection dialog). It should
 * create dialog window with instance configuration options. Returned
 * dialog will be destroyed on responce or on instance destroy and any
 * changed settings will be saved to the panel configuration file. See
 * also lxpanel_generic_config_dlg().
 *
 * Callback @reconfigure is called when panel configuration was changed
 * in the panel configuration dialog so the instance may change layout of
 * own subwidgets appropriately to new geometry.
 *
 * Callback @button_press_event is a handler for "button-press-event"
 * signal on the plugin instance. If this callback is not set then
 * lxpanel_plugin_button_press_event() will be used.
 *
 * Callback @show_system_menu is called when lxpanel received a message
 * by 'lxpanelctl menu' command. It will be sent to each instance if more
 * than one with this callback available exists.
 *
 * Callback @update_context_menu is called when panel context menu being
 * composed. The @menu contains only item for plugin instance config. The
 * callback can append or prepend own items to the menu. The callback
 * should return %TRUE if panel's common menu should be moved into the
 * submenu 'Panel' (therefore context menu will contain 'Settings' item,
 * any added ones, and 'Panel') and %FALSE if panel's common menu items
 * should be in this menu after separator.
 *
 * Callback @control is called when command was sent via the lxpanelctl.
 * The message will be sent to only one instance of plugin. Some messages
 * are handled by lxpanel: "STOP" will remove plugin from panel, "START"
 * will create new instance if there is no instance yet. (TODO)
 */
typedef struct {
    /*< public >*/
    void (*init)(void);         /* optional startup */
    void (*finalize)(void);     /* optional finalize */
    char *name;                 /* name to represent in lists */
    char *description;          /* tooltip text */
    GtkWidget *(*new_instance)(LXPanel *panel, config_setting_t *settings);
    GtkWidget *(*config)(LXPanel *panel, GtkWidget *instance, GtkWindow *parent);
    void (*reconfigure)(LXPanel *panel, GtkWidget *instance);
    gboolean (*button_press_event)(GtkWidget *widget, GdkEventButton *event, LXPanel *panel);
    void (*show_system_menu)(GtkWidget *widget);
    gboolean (*update_context_menu)(GtkWidget *plugin, GtkMenu *menu);
    gboolean (*control)(GtkWidget *plugin, const char *cmd); /* not implemented */
    /*< private >*/
    gpointer _reserved1;
    gpointer _reserved2;
    gpointer _reserved3;
    /*< public >*/
    int one_per_system : 1;     /* True to disable more than one instance */
    int expand_available : 1;   /* True if "stretch" option is available */
    int expand_default : 1;     /* True if "stretch" option is default */
    int superseded : 1;         /* True if plugin was superseded by another */
} LXPanelPluginInit; /* constant data */

extern LXPanelPluginInit fm_module_init_lxpanel_gtk;

extern GQuark lxpanel_plugin_qdata; /* access to plugin private data */
/**
 * lxpanel_plugin_get_data
 * @_i: instance widget
 *
 * Retrieves instance data attached to the widget.
 */
#define lxpanel_plugin_get_data(_i) g_object_get_qdata(G_OBJECT(_i),lxpanel_plugin_qdata)
/**
 * lxpanel_plugin_set_data
 * @_i: instance widget
 * @_data: instance data
 * @_destructor: destructor for @_data
 *
 * Attaches data to the widget instance. The @_destructor callback will
 * be called on @_data when @_i is destroyed and therefore it should free
 * any allocated data. The instance widget at that moment is already not
 * available to use in any way so not rely on it or its children.
 */
#define lxpanel_plugin_set_data(_i,_data,_destructor) g_object_set_qdata_full(G_OBJECT(_i),lxpanel_plugin_qdata,_data,_destructor)

/* register new plugin type - can be called from plugins init() too */
extern gboolean lxpanel_register_plugin_type(const char *name, LXPanelPluginInit *init);

/* few helper functions */
extern GtkMenu* lxpanel_get_plugin_menu(LXPanel* panel, GtkWidget* plugin, gboolean use_sub_menu);
extern gboolean lxpanel_plugin_button_press_event(GtkWidget *plugin, GdkEventButton *event, LXPanel *panel);
			/* Handler for "button_press_event" signal with Plugin as parameter */
extern void lxpanel_plugin_adjust_popup_position(GtkWidget * popup, GtkWidget * plugin);
			/* Helper to move popup windows away from the panel */
extern void lxpanel_plugin_popup_set_position_helper(LXPanel * p, GtkWidget * near, GtkWidget * popup, gint * px, gint * py);
			/* Helper for position-calculation callback for popup menus */
extern void plugin_widget_set_background(GtkWidget * widget, LXPanel * p);
			/* Recursively set the background of all widgets on a panel background configuration change */
extern gboolean lxpanel_launch_path(LXPanel *panel, FmPath *path);
extern void lxpanel_plugin_show_config_dialog(GtkWidget* plugin);
			/* Calls config() callback and shows configuration window */

G_END_DECLS

#endif /* __PLUGIN_H__ */
