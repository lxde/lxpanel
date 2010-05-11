/**
 * Copyright (c) 2010 LxDE Developers, see the file AUTHORS for details.
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

#include "xkb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/XKBlib.h>

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

/* The X Keyboard Extension: Library Specification
 * http://www.xfree86.org/current/XKBlib.pdf */

static void xkb_enter_locale_by_process(XkbPlugin * xkb);
static void refresh_group_xkb(XkbPlugin * xkb);
static int initialize_keyboard_description(XkbPlugin * xkb);
static GdkFilterReturn xkb_event_filter(GdkXEvent * xevent, GdkEvent * event, XkbPlugin * xkb);

/* Insert a process and its layout into the hash table. */
static void xkb_enter_locale_by_process(XkbPlugin * xkb)
{
    if ((xkb->group_hash_table != NULL) && (fb_ev_active_window(fbev) != None))
    {
        Window * win = fb_ev_active_window(fbev);
        if (*win != None)
            g_hash_table_insert(xkb->group_hash_table, GINT_TO_POINTER(*win), GINT_TO_POINTER(xkb->current_group_xkb_no));
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
    XkbGetState(GDK_DISPLAY(), XkbUseCoreKbd, &xkb_state);
    xkb->current_group_xkb_no = xkb_state.group & (XkbNumKbdGroups - 1);
}

/* Initialize the keyboard description initially or after a NewKeyboard event. */
static int initialize_keyboard_description(XkbPlugin * xkb)
{
    /* Allocate a keyboard description. */
    XkbDescRec * xkb_desc = XkbAllocKeyboard();
    if (xkb_desc == NULL)
        g_warning("XkbAllocKeyboard failed\n");
    else
    {
        /* Read necessary values into the keyboard description. */
        XkbGetControls(GDK_DISPLAY(), XkbAllControlsMask, xkb_desc);
        XkbGetNames(GDK_DISPLAY(), XkbSymbolsNameMask | XkbGroupNamesMask, xkb_desc);
        if ((xkb_desc->names == NULL) || (xkb_desc->ctrls == NULL) || (xkb_desc->names->groups == NULL))
            g_warning("XkbGetControls/XkbGetNames failed\n");
        else
        {
            /* Get the group name of each keyboard layout.  Infer the group count from the highest available. */
            Atom * group_source = xkb_desc->names->groups;
            int i;
            for (i = 0; i < XkbNumKbdGroups; i += 1)
            {
                g_free(xkb->group_names[i]);
                xkb->group_names[i] = NULL;
                if (group_source[i] != None)
                {
                    xkb->group_count = i + 1;
                    char * p = XGetAtomName(GDK_DISPLAY(), group_source[i]);
                    xkb->group_names[i] = g_strdup(p);
                    XFree(p);
                }
            }

            /* Reinitialize the symbol name storage. */
            for (i = 0; i < XkbNumKbdGroups; i += 1)
            {
                g_free(xkb->symbol_names[i]);
                xkb->symbol_names[i] = NULL;
            }

            /* Get the symbol name of all keyboard layouts.
             * This is a plus-sign separated string. */
            if (xkb_desc->names->symbols != None)
            {
                char * symbol_string = XGetAtomName(GDK_DISPLAY(), xkb_desc->names->symbols);
                if (symbol_string != NULL)
                {
                    char * p = symbol_string;
                    char * q = p;
                    int symbol_group_number = 0;
                    for ( ; symbol_group_number < XkbNumKbdGroups; p += 1)
                    {
                        char c = *p;
                        if ((c == '\0') || (c == '+'))
                        {
                            /* End of a symbol.  Ignore the symbols "pc" and "inet" and "group". */
                            *p = '\0';
                            if ((strcmp(q, "pc") != 0) && (strcmp(q, "inet") != 0) && (strcmp(q, "group") != 0))
                            {
                                xkb->symbol_names[symbol_group_number] = g_ascii_strup(q, -1);
                                symbol_group_number += 1;
                            }
                            if (c == '\0')
                                break;
                            q = p + 1;
                        }
                        else if ((c == ':') && (p[1] >= '1') && (p[1] < ('1' + XkbNumKbdGroups)))
                        {
                            /* Construction ":n" at the end of a symbol.  The digit is a one-based index of the symbol.
                             * If not present, we will default to "next index". */
                            *p = '\0';
                            symbol_group_number = p[1] - '1';
                            xkb->symbol_names[symbol_group_number] = g_ascii_strup(q, -1);
                            symbol_group_number += 1;
                            p += 2;
                            if (*p == '\0')
                                break;
                            q = p + 1;
                        }
                        else if ((*p >= 'A') && (*p <= 'Z'))
                            *p |= 'a' - 'A';
                        else if ((*p < 'a') || (*p > 'z'))
                            *p = '\0';
                    }
		    
                    /* Crosscheck the group count determined from the "ctrls" structure,
                     * that determined from the "groups" vector, and that determined from the "symbols" string.
                     * The "ctrls" structure is considered less reliable because it has been observed to be incorrect. */
                    if ((xkb->group_count != symbol_group_number)
                    || (xkb->group_count != xkb_desc->ctrls->num_groups))
                    {
                        g_warning("Group count mismatch, ctrls = %d, groups = %d, symbols = %d\n",
                            xkb_desc->ctrls->num_groups, xkb->group_count, symbol_group_number);

                        /* Maximize the "groups" and "symbols" value. */
                        if (xkb->group_count < symbol_group_number)
                            xkb->group_count = symbol_group_number;
                    }
                    XFree(symbol_string);
                }
            }
        }
        XkbFreeKeyboard(xkb_desc, 0, True);
    }

    /* Ensure that all elements within the name vectors are initialized. */
    int i;
    for (i = 0; i < XkbNumKbdGroups; i += 1)
    {
        if (xkb->group_names[i] == NULL)
            xkb->group_names[i] = g_strdup("Unknown");
        if (xkb->symbol_names[i] == NULL)
            xkb->symbol_names[i] = g_strdup("None");
    }

    /* Create or recreate hash table.
     * The layout that was associated to the windows may or may not be at the same group number,
     * and worse, may no longer exist, which there is no meaningful way to deal with. */
    if (xkb->group_hash_table != NULL)
        g_hash_table_destroy(xkb->group_hash_table);
    xkb->group_hash_table = g_hash_table_new(g_direct_hash, NULL);
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
            initialize_keyboard_description(xkb);
            refresh_group_xkb(xkb);
            xkb_redraw(xkb);
            xkb_enter_locale_by_process(xkb);
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
    && (XkbQueryExtension(GDK_DISPLAY(), &opcode, &xkb->base_event_code, &xkb->base_error_code, &maj, &min)))
    {
        /* Read the keyboard description. */
        initialize_keyboard_description(xkb);

        /* Establish GDK event filter. */
        gdk_window_add_filter(NULL, (GdkFilterFunc) xkb_event_filter, (gpointer) xkb);

        /* Specify events we will receive. */
        XkbSelectEvents(GDK_DISPLAY(), XkbUseCoreKbd, XkbNewKeyboardNotifyMask, XkbNewKeyboardNotifyMask);
        XkbSelectEventDetails(GDK_DISPLAY(), XkbUseCoreKbd, XkbStateNotify, XkbAllStateComponentsMask, XkbGroupStateMask);

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
    g_hash_table_destroy(xkb->group_hash_table);
    xkb->group_hash_table = NULL;
}

/* Set the layout to the next layout. */
int xkb_change_group(XkbPlugin * xkb, int increment) 
{
    /* Apply the increment and wrap the result. */
    int next_group = xkb->current_group_xkb_no + increment;
    if (next_group < 0) next_group = xkb->group_count - 1;
    if (next_group >= xkb->group_count) next_group = 0;

    /* Execute the change. */
    XkbLockGroup(GDK_DISPLAY(), XkbUseCoreKbd, next_group);
    refresh_group_xkb(xkb);
    xkb_redraw(xkb);
    xkb_enter_locale_by_process(xkb);
    return 1;
}

/* React to change of focus by switching to the application's layout or the default layout. */
void xkb_active_window_changed(XkbPlugin * xkb, Window window)
{
    gint new_group_xkb_no = xkb->default_group;

    gpointer pKey = 0, pVal = 0;
    if ((xkb->group_hash_table != NULL) && (g_hash_table_lookup_extended(xkb->group_hash_table, GINT_TO_POINTER(window), &pKey, &pVal)))
        new_group_xkb_no = GPOINTER_TO_INT(pVal);

    if (new_group_xkb_no < xkb->group_count)
    {
        XkbLockGroup(GDK_DISPLAY(), XkbUseCoreKbd, new_group_xkb_no);
        refresh_group_xkb(xkb);
    }
}


