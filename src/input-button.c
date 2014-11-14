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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>

#include "plugin.h"
#include "gtk-compat.h"

#define PANEL_TYPE_CFG_INPUT_BUTTON     (config_input_button_get_type())

typedef struct _PanelCfgInputButton      PanelCfgInputButton;
typedef struct _PanelCfgInputButtonClass PanelCfgInputButtonClass;

struct _PanelCfgInputButton
{
    GtkFrame parent;
    GtkToggleButton *none;
    GtkToggleButton *custom;
    GtkButton *btn;
    gboolean do_key;
    gboolean do_click;
    guint key;
    GdkModifierType mods;
    gboolean has_focus;
};

struct _PanelCfgInputButtonClass
{
    GtkFrameClass parent_class;
    void (*changed)(PanelCfgInputButton *btn, char *accel);
};

enum
{
    CHANGED,
    N_SIGNALS
};

static guint signals[N_SIGNALS];


/* ---- Events on test button ---- */

static void on_focus_in_event(GtkButton *test, GdkEvent *event,
                              PanelCfgInputButton *btn)
{
    /* toggle radiobuttons */
    gtk_toggle_button_set_active(btn->custom, TRUE);
    btn->has_focus = TRUE;
    if (btn->do_key)
        gdk_keyboard_grab(gtk_widget_get_window(GTK_WIDGET(test)),
                          TRUE, GDK_CURRENT_TIME);
}

static void on_focus_out_event(GtkButton *test, GdkEvent *event,
                               PanelCfgInputButton *btn)
{
    /* stop accepting mouse clicks */
    btn->has_focus = FALSE;
    if (btn->do_key)
        gdk_keyboard_ungrab(GDK_CURRENT_TIME);
}

static gboolean on_key_event(GtkButton *test, GdkEventKey *event,
                             PanelCfgInputButton *btn)
{
    GdkModifierType state;
    char *text;

    /* ignore Tab completely so user can leave focus */
    if (event->keyval == GDK_KEY_Tab)
        return FALSE;
    /* request mods directly, event->state isn't updated yet */
    gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(test)),
                           NULL, NULL, &state);
    /* special support for Win key, it doesn't work sometimes */
    if ((state & GDK_SUPER_MASK) == 0 && (state & GDK_MOD4_MASK) != 0)
        state |= GDK_SUPER_MASK;
    state &= gtk_accelerator_get_default_mod_mask();
    /* if mod key event then update test label and go */
    if (event->is_modifier)
    {
        text = gtk_accelerator_get_label(0, state);
        gtk_button_set_label(test, text);
        g_free(text);
        return FALSE;
    }
    /* if not keypress query then ignore key press */
    if (event->type != GDK_KEY_PRESS || !btn->do_key)
        return FALSE;
    /* if keypress is equal to previous then nothing to do */
    if (state == btn->mods && event->keyval == btn->key)
    {
        text = gtk_accelerator_get_label(event->keyval, state);
        gtk_button_set_label(test, text);
        g_free(text);
        return FALSE;
    }
    /* drop single printable and printable with single Shift, Ctrl, Alt */
    if (event->length != 0 && (state == 0 || state == GDK_SHIFT_MASK ||
                               state == GDK_CONTROL_MASK || state == GDK_MOD1_MASK))
    {
        GtkWidget* dlg;
        text = gtk_accelerator_get_label(event->keyval, state);
        dlg = gtk_message_dialog_new(NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
                                     _("Key combination '%s' cannot be used as"
                                       " a global hotkey, sorry."), text);
        g_free(text);
        gtk_window_set_title(GTK_WINDOW(dlg), _("Error"));
        gtk_window_set_keep_above(GTK_WINDOW(dlg), TRUE);
        gtk_dialog_run(GTK_DIALOG(dlg));
        gtk_widget_destroy(dlg);
        return FALSE;
    }
    /* send a signal that it's changed */
    btn->mods = state;
    btn->key = event->keyval;
    text = gtk_accelerator_name(btn->key, state);
    g_signal_emit(btn, signals[CHANGED], 0, text);
    g_free(text);
    text = gtk_accelerator_get_label(event->keyval, state);
    gtk_button_set_label(test, text);
    g_free(text);
    return FALSE;
}

static gboolean on_button_press_event(GtkButton *test, GdkEventButton *event,
                                      PanelCfgInputButton *btn)
{
    GdkModifierType state;
    char *text;
    char digit[4];
    guint keyval;

    if (!btn->do_click)
        return FALSE;
    /* if not focused yet then take facus and skip event */
    if (!btn->has_focus)
    {
        btn->has_focus = TRUE;
        return FALSE;
    }
    /* if simple right-click then just ignore it */
    state = event->state & gtk_accelerator_get_default_mod_mask();
    if (event->button == 3 && state == 0)
        return FALSE;
    /* FIXME: how else to represent buttons? */
    snprintf(digit, sizeof(digit), "%d", event->button);
    keyval = gdk_keyval_from_name(digit);
    /* if click is equal to previous then nothing to do */
    if (state == btn->mods && keyval == btn->key)
    {
        text = gtk_accelerator_get_label(keyval, state);
        gtk_button_set_label(test, text);
        g_free(text);
        return FALSE;
    }
    /* send a signal that it's changed */
    text = gtk_accelerator_get_label(keyval, state);
    btn->mods = state;
    btn->key = keyval;
    gtk_button_set_label(test, text);
    g_free(text);
    text = gtk_accelerator_name(keyval, state);
    g_signal_emit(btn, signals[CHANGED], 0, text);
    g_free(text);
    return FALSE;
}

static void on_reset(GtkRadioButton *rb, PanelCfgInputButton *btn)
{
    if (!gtk_toggle_button_get_active(btn->none))
        return;
    btn->mods = 0;
    btn->key = 0;
    gtk_button_set_label(btn->btn, "");
    g_signal_emit(btn, signals[CHANGED], 0, NULL);
}

/* ---- Class implementation ---- */

G_DEFINE_TYPE(PanelCfgInputButton, config_input_button, GTK_TYPE_FRAME)

static void config_input_button_class_init(PanelCfgInputButtonClass *klass)
{
    signals[CHANGED] =
        g_signal_new("changed",
                     G_TYPE_FROM_CLASS(klass),
                     G_SIGNAL_RUN_FIRST,
                     G_STRUCT_OFFSET(PanelCfgInputButtonClass, changed),
                     NULL, NULL,
                     g_cclosure_marshal_VOID__STRING,
                     G_TYPE_NONE, 1, G_TYPE_STRING);
}

static void config_input_button_init(PanelCfgInputButton *self)
{
    GtkWidget *w = gtk_hbox_new(FALSE, 6);
    GtkBox *box = GTK_BOX(w);

    /* GtkRadioButton "None" */
    w = gtk_radio_button_new_with_label(NULL, _("None"));
    gtk_box_pack_start(box, w, FALSE, FALSE, 6);
    self->none = GTK_TOGGLE_BUTTON(w);
    gtk_toggle_button_set_active(self->none, TRUE);
    g_signal_connect(w, "toggled", G_CALLBACK(on_reset), self);
    /* GtkRadioButton "Custom:" */
    w = gtk_radio_button_new_with_label_from_widget(GTK_RADIO_BUTTON(w),
                                                    _("Custom:"));
    gtk_box_pack_start(box, w, FALSE, FALSE, 0);
    gtk_widget_set_can_focus(w, FALSE);
    self->custom = GTK_TOGGLE_BUTTON(w);
    /* test GtkButton */
    w = gtk_button_new_with_label(NULL);
    gtk_box_pack_start(box, w, TRUE, TRUE, 0);
    self->btn = GTK_BUTTON(w);
    gtk_button_set_label(self->btn, "        "); /* set some minimum size */
    g_signal_connect(w, "focus-in-event", G_CALLBACK(on_focus_in_event), self);
    g_signal_connect(w, "focus-out-event", G_CALLBACK(on_focus_out_event), self);
    g_signal_connect(w, "key-press-event", G_CALLBACK(on_key_event), self);
    g_signal_connect(w, "key-release-event", G_CALLBACK(on_key_event), self);
    g_signal_connect(w, "button-press-event", G_CALLBACK(on_button_press_event), self);
    /* HBox */
    w = (GtkWidget *)box;
    gtk_widget_show_all(w);
    gtk_container_add(GTK_CONTAINER(self), w);
}

static PanelCfgInputButton *_config_input_button_new(const char *label)
{
    return g_object_new(PANEL_TYPE_CFG_INPUT_BUTTON,
                        "label", label, NULL);
}

GtkWidget *panel_config_hotkey_button_new(const char *label, const char *hotkey)
{
    PanelCfgInputButton *btn = _config_input_button_new(label);
    char *text;

    btn->do_key = TRUE;
    if (hotkey && *hotkey)
    {
        gtk_accelerator_parse(hotkey, &btn->key, &btn->mods);
        text = gtk_accelerator_get_label(btn->key, btn->mods);
        gtk_button_set_label(btn->btn, text);
        g_free(text);
        gtk_toggle_button_set_active(btn->custom, TRUE);
    }
    return GTK_WIDGET(btn);
}

GtkWidget *panel_config_click_button_new(const char *label, const char *click)
{
    PanelCfgInputButton *btn = _config_input_button_new(label);
    char *text;

    btn->do_click = TRUE;
    if (click && *click)
    {
        gtk_accelerator_parse(click, &btn->key, &btn->mods);
        text = gtk_accelerator_get_label(btn->key, btn->mods);
        gtk_button_set_label(btn->btn, text);
        g_free(text);
        gtk_toggle_button_set_active(btn->custom, TRUE);
    }
    return GTK_WIDGET(btn);
}
#if 0
// test code, can be used as an example until erased. :)
#include <keybinder.h>
static void handler(const char *keystring, void *user_data)
{
}

static char *hotkey = NULL;

static void cb(PanelCfgInputButton *btn, char *text, gpointer unused)
{
    g_print("got keystring \"%s\"\n", text);

    if (!btn->do_key)
        return;
    if (text == NULL || keybinder_bind(text, handler, NULL))
    {
        if (hotkey)
            keybinder_unbind(hotkey, handler);
        g_free(hotkey);
        hotkey = g_strdup(text);
    }
}

int main(int argc, char **argv)
{
    GtkWidget *dialog;
    GtkWidget *btn;

    gtk_init(&argc, &argv);
    dialog = gtk_dialog_new();
    hotkey = g_strdup("<Super>z");
    btn = panel_config_hotkey_button_new("test", hotkey);
//    btn = panel_config_click_button_new("test", NULL);
    gtk_widget_show(btn);
    g_signal_connect(btn, "changed", G_CALLBACK(cb), NULL);
    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), btn);
    gtk_dialog_run(GTK_DIALOG(dialog));
    return 0;
}
#endif
