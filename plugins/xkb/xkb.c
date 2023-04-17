/*
 * Copyright (C) 2008 Hong Jen Yee (PCMan) <pcman.tw@gmail.com>
 *               2009-2010 Marty Jack <martyj19@comcast.net>
 *               2012-2013 Giuseppe Penone <giuspen@gmail.com>
 *               2017 Max Kirillov <max@max630.net>
 *
 * This file is a part of LXPanel project.
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

/* Originally derived from xfce4-xkb-plugin, Copyright 2004 Alexander Iliev,
 * which credits Michael Glickman. */

/* Modified by Giuseppe Penone <giuspen@gmail.com> starting from 2012-07 and lxpanel 0.5.10 */

#include "xkb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/XKBlib.h>
#include <X11/extensions/XKBrules.h>

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

/* The X Keyboard Extension: Library Specification
 * http://www.xfree86.org/current/XKBlib.pdf */

typedef enum
{
    NEW_KBD_STATE_NOTIFY_IGNORE_NO,
    NEW_KBD_STATE_NOTIFY_IGNORE_YES_SET,
    NEW_KBD_STATE_NOTIFY_IGNORE_YES_ALL,

} t_new_kbd_notify_ignore;

static void             xkb_enter_locale_by_process(XkbPlugin * xkb);
static void             refresh_group_xkb(XkbPlugin * xkb);
static int              initialize_keyboard_description(XkbPlugin * xkb);
static GdkFilterReturn  xkb_event_filter(GdkXEvent * xevent, GdkEvent * event, XkbPlugin * xkb);

static t_new_kbd_notify_ignore  xkb_new_kbd_notify_ignore = NEW_KBD_STATE_NOTIFY_IGNORE_NO;


static gboolean xkb_new_kbd_notify_ignore_slot(gpointer p_data)
{
    xkb_new_kbd_notify_ignore = NEW_KBD_STATE_NOTIFY_IGNORE_NO;
    return FALSE; // remove source
}

/* Insert a process and its layout into the hash table. */
static void xkb_enter_locale_by_process(XkbPlugin * xkb)
{
    if ((xkb->p_hash_table_group != NULL) && (fb_ev_active_window(fbev) != None))
    {
        Window * win = fb_ev_active_window(fbev);
        if (*win != None)
            g_hash_table_insert(xkb->p_hash_table_group, GINT_TO_POINTER(*win), GINT_TO_POINTER(xkb->current_group_xkb_no));
    }
}

/* Return the current group Xkb ID. */
int xkb_get_current_group_xkb_no(XkbPlugin * xkb)
{
    return xkb->current_group_xkb_no;
}

/* Return the count of members in the current group. */
int xkb_get_group_count(XkbPlugin * xkb)
{
  return xkb->group_count;
}

/* Get the current group name. */
const char * xkb_get_current_group_name(XkbPlugin * xkb)
{
    return xkb->group_names[xkb->current_group_xkb_no];
}

/* Convert a group number to a symbol name. */
const char * xkb_get_symbol_name_by_res_no(XkbPlugin * xkb, int n)
{
    return xkb->symbol_names[n];
}

/* Get the current symbol name. */
const char * xkb_get_current_symbol_name(XkbPlugin * xkb)
{
    return xkb_get_symbol_name_by_res_no(xkb, xkb->current_group_xkb_no);
}

/* Get the current symbol name converted to lowercase. */
const char * xkb_get_current_symbol_name_lowercase(XkbPlugin * xkb)
{
    const char * tmp = xkb_get_current_symbol_name(xkb);
    return ((tmp != NULL) ? g_utf8_strdown(tmp, -1) : NULL);
}

/* Refresh current group number from Xkb state. */
static void refresh_group_xkb(XkbPlugin * xkb)
{
    /* Get the current group number.
     * This shouldn't be necessary, but mask the group number down for safety. */
    XkbStateRec xkb_state;
    XkbGetState(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), XkbUseCoreKbd, &xkb_state);
    xkb->current_group_xkb_no = xkb_state.group & (XkbNumKbdGroups - 1);
}

/* Initialize the keyboard description initially or after a NewKeyboard event. */
static int initialize_keyboard_description(XkbPlugin * xkb)
{
    /* Allocate a keyboard description. */
    XkbDescRec * xkb_desc = XkbAllocKeyboard();
    if (xkb_desc == NULL)
    {
        g_warning("XkbAllocKeyboard failed\n");
        goto ikd_clean;
    }

    /* Read necessary values into the keyboard description. */
    Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());
    XkbGetNames(xdisplay, XkbGroupNamesMask, xkb_desc);
    if ((xkb_desc->names == NULL) || (xkb_desc->names->groups == NULL))
    {
        g_warning("XkbGetNames failed\n");
        goto ikd_clean;
    }

    /* Get the group name of each keyboard layout. Infer the group count from the highest available. */
    Atom * group_source = xkb_desc->names->groups;
    int i;
    for (i = 0; i < XkbNumKbdGroups; ++i)
    {
        g_free(xkb->group_names[i]);
        xkb->group_names[i] = NULL;
        if (group_source[i] != None)
        {
            xkb->group_count = i + 1;
            char * p = XGetAtomName(xdisplay, group_source[i]);
            xkb->group_names[i] = g_strdup(p);
            XFree(p);
        }
    }
    ikd_clean:
    XkbFreeKeyboard(xkb_desc, 0, True);

    /* Reinitialize the symbol name storage. */
    XkbRF_VarDefsRec vd;
    XkbRF_GetNamesProp(xdisplay, NULL, &vd);
    for (i = 0; i < XkbNumKbdGroups; ++i)
    {
        g_free(xkb->symbol_names[i]);
        xkb->symbol_names[i] = NULL;
    }
    char **symbol_source = g_strsplit(vd.layout, ",", 4);
    for (i = 0; symbol_source[i]; ++i)
    {
        xkb->symbol_names[i] = g_strdup(symbol_source[i]);
    }
    g_strfreev(symbol_source);

    /* Ensure that all elements within the name vectors are initialized. */
    for (i = 0; i < XkbNumKbdGroups; i += 1)
    {
        if (xkb->group_names[i] == NULL)
            xkb->group_names[i] = g_strdup("Unknown");
        if (xkb->symbol_names[i] == NULL)
            xkb->symbol_names[i] = g_strdup("None");
    }

    /* Create or recreate hash table */
    if (xkb->p_hash_table_group != NULL)
        g_hash_table_destroy(xkb->p_hash_table_group);
    xkb->p_hash_table_group = g_hash_table_new(g_direct_hash, NULL);

    return TRUE;
}

/* GDK event filter that receives events from all windows and the Xkb extension. */
static GdkFilterReturn xkb_event_filter(GdkXEvent * xevent, GdkEvent * event, XkbPlugin * xkb)
{
    XEvent * ev = (XEvent *) xevent;

    if (ev->xany.type == xkb->base_event_code + XkbEventCode)
    {
        /* Xkb event. */
        XkbEvent * xkbev = (XkbEvent *) ev;
        if (xkbev->any.xkb_type == XkbNewKeyboardNotify)
        {
            if(xkb_new_kbd_notify_ignore == NEW_KBD_STATE_NOTIFY_IGNORE_NO)
            {
                //g_print("xkb_new_kbd_notify_ignore == NEW_KBD_STATE_NOTIFY_IGNORE_NO\n");
                xkb_new_kbd_notify_ignore = NEW_KBD_STATE_NOTIFY_IGNORE_YES_SET;
                (void)g_timeout_add(1000/*msec*/, xkb_new_kbd_notify_ignore_slot, NULL);
                xkb_setxkbmap(xkb);
            }
            else if(xkb_new_kbd_notify_ignore == NEW_KBD_STATE_NOTIFY_IGNORE_YES_SET)
            {
                //g_print("xkb_new_kbd_notify_ignore == NEW_KBD_STATE_NOTIFY_IGNORE_YES_SET\n");
                xkb_new_kbd_notify_ignore = NEW_KBD_STATE_NOTIFY_IGNORE_YES_ALL;
                initialize_keyboard_description(xkb);
                refresh_group_xkb(xkb);
                xkb_redraw(xkb);
                xkb_enter_locale_by_process(xkb);
            }
        }
        else if (xkbev->any.xkb_type == XkbStateNotify)
        {
            if (xkbev->state.group != xkb->current_group_xkb_no)
            {
                /* Switch to the new group and redraw the display.
                 * This shouldn't be necessary, but mask the group number down for safety. */
                xkb->current_group_xkb_no = xkbev->state.group & (XkbNumKbdGroups - 1);
                refresh_group_xkb(xkb);
                xkb_redraw(xkb);
                xkb_enter_locale_by_process(xkb);
            }
        }
    }
    return GDK_FILTER_CONTINUE;
}

/* Initialize the Xkb interface. */
void xkb_mechanism_constructor(XkbPlugin * xkb)
{
    /* Initialize Xkb extension. */
    int opcode;
    int maj = XkbMajorVersion;
    int min = XkbMinorVersion;
    if ((XkbLibraryVersion(&maj, &min))
    && (XkbQueryExtension(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                          &opcode, &xkb->base_event_code, &xkb->base_error_code, &maj, &min)))
    {
        Display *xdisplay = GDK_DISPLAY_XDISPLAY(gdk_display_get_default());

        /* Read the keyboard description. */
        initialize_keyboard_description(xkb);

        /* Establish GDK event filter. */
        gdk_window_add_filter(NULL, (GdkFilterFunc) xkb_event_filter, (gpointer) xkb);

        /* Specify events we will receive. */
        XkbSelectEvents(xdisplay, XkbUseCoreKbd, XkbNewKeyboardNotifyMask, XkbNewKeyboardNotifyMask);
        XkbSelectEventDetails(xdisplay, XkbUseCoreKbd, XkbStateNotify, XkbAllStateComponentsMask, XkbGroupStateMask);

        /* Get current state. */
        refresh_group_xkb(xkb);
    }
}

/* Deallocate resources associated with Xkb interface. */
void xkb_mechanism_destructor(XkbPlugin * xkb)
{
    /* Remove event filter. */
    gdk_window_remove_filter(NULL, (GdkFilterFunc) xkb_event_filter, xkb);

    /* Free group and symbol name memory. */
    int i;
    for (i = 0; i < XkbNumKbdGroups; i++)
    {
        if (xkb->group_names[i] != NULL)
        {
            g_free(xkb->group_names[i]);
            xkb->group_names[i] = NULL;
        }
        if (xkb->symbol_names[i] != NULL)
        {
            g_free(xkb->symbol_names[i]);
            xkb->symbol_names[i] = NULL;
        }
    }

    /* Destroy the hash table. */
    g_hash_table_destroy(xkb->p_hash_table_group);
    xkb->p_hash_table_group = NULL;
}

/* Set the layout to the next layout. */
int xkb_change_group(XkbPlugin * xkb, int increment)
{
    /* Apply the increment and wrap the result. */
    int next_group = xkb->current_group_xkb_no + increment;
    if (next_group < 0) next_group = xkb->group_count - 1;
    if (next_group >= xkb->group_count) next_group = 0;

    /* Execute the change. */
    XkbLockGroup(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()), XkbUseCoreKbd, next_group);
    refresh_group_xkb(xkb);
    xkb_redraw(xkb);
    xkb_enter_locale_by_process(xkb);
    return 1;
}

/* React to change of focus by switching to the application's layout or the default layout. */
void xkb_active_window_changed(XkbPlugin * xkb, Window window)
{
    gint  new_group_xkb_no = 0;

    gpointer pKey = 0, pVal = 0;
    if ((xkb->p_hash_table_group != NULL) && (g_hash_table_lookup_extended(xkb->p_hash_table_group, GINT_TO_POINTER(window), &pKey, &pVal)))
        new_group_xkb_no = GPOINTER_TO_INT(pVal);

    if (new_group_xkb_no < xkb->group_count)
    {
        XkbLockGroup(GDK_DISPLAY_XDISPLAY(gdk_display_get_default()),
                     XkbUseCoreKbd, new_group_xkb_no);
        refresh_group_xkb(xkb);
    }
}
