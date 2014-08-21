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
 * signal on the plugin instance. This callback would never receive any
 * right-clicks without modifier keys because panel itself will handle it
 * showing context menu.
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
 * are handled by lxpanel: "DEL" will remove plugin from panel, "ADD"
 * will create new instance if there is no instance yet. (TODO)
 */
typedef struct {
    /*< public >*/
    void (*init)(void);         /* optional startup */
    void (*finalize)(void);     /* optional finalize */
    char *name;                 /* name to represent in lists */
    char *description;          /* tooltip text */
    GtkWidget *(*new_instance)(LXPanel *panel, config_setting_t *settings);
    GtkWidget *(*config)(LXPanel *panel, GtkWidget *instance);
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

/*
 * This descriptor instance should be defined in each plugin code as main
 * entry point for plugin creation. Primitive plugin example follows:
 *
 * #include <lxpanel/plugin.h>
 *
 * GtkWidget *test_new_instance(LXPanel *panel, config_setting_t *settings)
 * {
 *      return gtk_image_new_from_stock(GTK_STOCK_OK, panel_get_icon_size(panel));
 * }
 *
 * FM_DEFINE_MODULE(lxpanel_gtk, test)
 *
 * LXPanelPluginInit fm_module_init_lxpanel_gtk = {
 *      .name = "Test plugin",
 *      .description = "An image with OK icon",
 *      .new_instance = test_new_instance
 * }
 */
extern LXPanelPluginInit fm_module_init_lxpanel_gtk;

extern GQuark lxpanel_plugin_qdata; /* access to plugin private data */
/**
 * lxpanel_plugin_get_data
 * @_i: instance widget
 *
 * Retrieves instance data attached to the widget.
 *
 * Returns: (transfer none): pointer to the data.
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

/**
 * lxpanel_register_plugin_type
 * @name: name of new plugin type
 * @init: plugin-specific type descriptor
 *
 * Register new plugin type. Can be called from plugins init() callback too.
 *
 * Returns: %TRUE in case of success.
 */
extern gboolean lxpanel_register_plugin_type(const char *name, const LXPanelPluginInit *init);

/**
 * lxpanel_get_plugin_menu
 * @panel: panel instance pointer
 * @plugin: plugin instance pointer
 * @use_sub_menu: %TRUE if panel management options should be in separated submenu
 *
 * Creates context menu for given @panel and @plugin.
 *
 * Returns: (transfer full): new menu widget.
 */
extern GtkMenu* lxpanel_get_plugin_menu(LXPanel* panel, GtkWidget* plugin, gboolean use_sub_menu);

/**
 * lxpanel_plugin_adjust_popup_position
 * @popup: a widget to adjust position
 * @plugin: a plugin instance
 *
 * Adjusts the position of a @popup window to ensure that it is not hidden
 * by the panel and moves @popup near @plugin if possible.
 */
extern void lxpanel_plugin_adjust_popup_position(GtkWidget * popup, GtkWidget * plugin);

/**
 * lxpanel_plugin_popup_set_position_helper
 * @p: a panel instance
 * @near: a widget to position the popup
 * @popup: a widget to calculate position
 * @px: (out): pointer to receive X coordinate
 * @py: (out): pointer to receive Y coordinate
 *
 * Calculates desired position of @popup to be placed right to the widget
 * @near accordingly to position of panel @p and returns its coordinates.
 * Can be used in position-calculation callback for popup menus.
 */
extern void lxpanel_plugin_popup_set_position_helper(LXPanel * p, GtkWidget * near, GtkWidget * popup, gint * px, gint * py);

/**
 * plugin_widget_set_background
 * @widget: a widget
 * @p: a panel instance
 *
 * Recursively set the background of @widget and its children. Can be
 * used on a panel background configuration change.
 */
extern void plugin_widget_set_background(GtkWidget * widget, LXPanel * p);

/**
 * lxpanel_launch_path
 * @panel: a panel instance
 * @path: a path to launch
 *
 * Launches the @path either itself, or using default application.
 *
 * Returns: %TRUE if launch was successful.
 */
extern gboolean lxpanel_launch_path(LXPanel *panel, FmPath *path);

/**
 * lxpanel_plugin_show_config_dialog
 * @plugin: a plugin instance
 *
 * Calls config() callback and shows configuration window.
 */
extern void lxpanel_plugin_show_config_dialog(GtkWidget* plugin);

/**
 * PluginConfType:
 *
 * Type of variable passed to lxpanel_generic_config_dlg().
 */
typedef enum {
    CONF_TYPE_STR,
    CONF_TYPE_INT,
    CONF_TYPE_BOOL,
    CONF_TYPE_FILE,
    CONF_TYPE_FILE_ENTRY,
    CONF_TYPE_DIRECTORY_ENTRY,
    CONF_TYPE_TRIM
} PluginConfType;

/**
 * lxpanel_generic_config_dlg
 * @title: (allow-none): optional title of dialog window
 * @panel: a panel instance
 * @apply_func: (allow-none): function to apply changes to the plugin
 * @plugin: (allow-none): an argument for @apply_func
 * @name: variable-size list of options to configure, terminated with %NULL
 *
 * Creates a generic dialog widget to configure the plugin parameters.
 * The dialog widget may be used for plugin's callback config() then.
 * Variable-size list of options consists of three arguments for each
 * option:
 *   - const char* name: text representing the option in dialog
 *   - gpointer ret_value: (out): pointer to the option value
 *   - PluginConfType type: type of the option
 *
 * Returns: (tranfer full): new created dialog widget.
 */
/* Parameters: const char* name, gpointer ret_value, PluginConfType type, ....NULL */
extern GtkWidget *lxpanel_generic_config_dlg(const char *title, LXPanel *panel,
                                             GSourceFunc apply_func,
                                             GtkWidget *plugin,
                                             const char *name, ...);

G_END_DECLS

#endif /* __PLUGIN_H__ */
