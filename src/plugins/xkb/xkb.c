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
        {
            GPid pid = get_net_wm_pid(*fb_ev_active_window(fbev));
            g_hash_table_insert(xkb->group_hash_table, GINT_TO_POINTER(pid), GINT_TO_POINTER(xkb->current_group_xkb_no));
        }
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
    XkbStateRec xkb_state;
    XkbGetState(GDK_DISPLAY(), XkbUseCoreKbd, &xkb_state);
    xkb->current_group_xkb_no = xkb_state.group;
}

/* Initialize the keyboard description initially or after a NewKeyboard event. */
static int initialize_keyboard_description(XkbPlugin * xkb)
{
    /* Free the strings. */
    int i;
    for (i = 0; i < XkbNumKbdGroups; i += 1)
    {
        g_free(xkb->group_names[i]);
        g_free(xkb->symbol_names[i]);
        xkb->group_names[i] = NULL;
        xkb->symbol_names[i] = NULL;
    }

    /* Allocate a keyboard description structure. */
    int status = False;
    XkbDescRec * kbd_desc_ptr = XkbAllocKeyboard();
    if (kbd_desc_ptr == NULL)
    {
        ERR("Failed to get keyboard description\n");
        goto HastaLaVista;
    }

    /* Fetch information into the keyboard description. */
    XkbGetControls(GDK_DISPLAY(), XkbAllControlsMask, kbd_desc_ptr);
    XkbGetNames(GDK_DISPLAY(), XkbSymbolsNameMask, kbd_desc_ptr);
    XkbGetNames(GDK_DISPLAY(), XkbGroupNamesMask, kbd_desc_ptr);

    if (kbd_desc_ptr->names == NULL)
    {
        ERR("Failed to get keyboard description\n");
        goto HastaLaVista;
    }

    /* Determine the group count either from the "ctrls" structure
     * or by enumerating the "groups" structure.  Normally the
     * "ctrls" structure is valid. */
    const Atom * group_source = kbd_desc_ptr->names->groups;
    if (kbd_desc_ptr->ctrls != NULL)
        xkb->group_count = kbd_desc_ptr->ctrls->num_groups;
    else
    {
        for (
          xkb->group_count = 0;
          ((xkb->group_count < XkbNumKbdGroups) && (group_source[xkb->group_count] != None));
          xkb->group_count++) ;
    }
    if (xkb->group_count == 0)
        xkb->group_count = 1;
    if (xkb->group_count > XkbNumKbdGroups)
        xkb->group_count = XkbNumKbdGroups;

    /* Determine the group names.  Trim off text beginning at a '('. */
    const Atom * tmp_group_source = kbd_desc_ptr->names->groups;
    for (i = 0; i < xkb->group_count; i++)
    {
        if (tmp_group_source[i] != None)
        {
            char * ptr = XGetAtomName(GDK_DISPLAY(), tmp_group_source[i]);
            xkb->group_names[i] = ptr;
            if ((ptr != NULL) && ((ptr = strchr(ptr, '('))) != NULL)
                *ptr = '\0';
        }
    }

    /* Fetch the symbol names. */
    Atom sym_name_atom = kbd_desc_ptr->names->symbols;
    char * sym_name;
    if ((sym_name_atom == None)
    || ((sym_name = XGetAtomName(GDK_DISPLAY(), sym_name_atom)) == NULL))
        goto HastaLaVista;

    /* Parse and store symbol names. */
    int count = 0;
    char * ptr;
    for (ptr = strtok(sym_name, "+"); ptr != NULL; ptr = strtok(NULL, "+"))
    {
        char * ptr1 = strchr(ptr, '(');
        if (ptr1 != NULL) *ptr1 = '\0';
        ptr1 = strchr(ptr, '_');
        if ((ptr1 != NULL) && ( ! g_ascii_isupper(ptr1[1]))) *ptr1 = '\0';
        ptr1 = strchr(ptr, ':');
        if (ptr1 != NULL) *ptr1 = '\0';

        ptr1 = strrchr(ptr, '/');
        if (ptr1 != NULL)
        {
            /* Filter out cases like pc/pc */
            if (memcmp(ptr, ptr1 + 1, ptr1 - ptr) == 0) continue;
            ptr = ptr1 + 1;
        }

        if (strncmp(ptr, "group", 5) == 0) continue;
        if (strncmp(ptr, "inet", 4) == 0) continue;
        /* Filter cases like pc(pc105) (Xorg 7.0 update) */
        if (strncmp(ptr, "pc", 2) == 0) continue;
        
        xkb->symbol_names[count] = g_utf8_strup(ptr, -1);
        count += 1;
    }

    /* Special cases. */
    if ((count == 1) && (xkb->group_names[0] == NULL) && (strcmp(xkb->symbol_names[0], "jp") == 0))
    {
        xkb->group_count = 2;
        xkb->symbol_names[1] = xkb->symbol_names[0];
        xkb->symbol_names[0] = g_strdup("us");
        xkb->group_names[0] = g_strdup("US/ASCII");
        xkb->group_names[1] = g_strdup("Japanese");
    }
    else if (count < xkb->group_count)
    {
        /* Ensure that the names are fully initialized. */
        int j = count, k = xkb->group_count;
        while(--j >= 0) xkb->symbol_names[--k] = xkb->symbol_names[j];
        while(--k >= 0) xkb->symbol_names[k] = g_strdup("en_US");
    }

    /* Ensure that the names are fully initialized. */
    for (i = 0; i < xkb->group_count; i++)
    {
        if (xkb->symbol_names[i] == NULL)
        {
            ERR("\nGroup Symbol %i is undefined, set to 'U/A' !\n", i+1);
            xkb->symbol_names[i] = g_strdup("U/A");
        }
    }

    /* Create or recreate hash table.
     * The layout that was associated to the windows may or may not be at the same group number,
     * and worse, may no longer exist, which there is no meaningful way to deal with. */
    if (xkb->group_hash_table != NULL)
        g_hash_table_destroy(xkb->group_hash_table);
    xkb->group_hash_table = g_hash_table_new(g_direct_hash, NULL);

    status = True;

HastaLaVista:
    if (kbd_desc_ptr != NULL)
        XkbFreeKeyboard(kbd_desc_ptr, 0, True);

    return status;
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
                /* Switch to the new group and redraw the display. */
                xkb->current_group_xkb_no = xkbev->state.group;
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
    for (i = 0; i < xkb->group_count; i++)
    {
        if (xkb->group_names[i] != NULL)
        {
            free(xkb->group_names[i]);
            xkb->group_names[i] = NULL;
        }
        if (xkb->symbol_names[i] != NULL)
        {
            free(xkb->symbol_names[i]);
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
void xkb_active_window_changed(XkbPlugin * xkb, gint pid)
{
    gint new_group_xkb_no = xkb->default_group;

    gpointer pKey = 0, pVal = 0;
    if ((xkb->group_hash_table != NULL) && (g_hash_table_lookup_extended(xkb->group_hash_table, GINT_TO_POINTER(pid), &pKey, &pVal)))
        new_group_xkb_no = GPOINTER_TO_INT(pVal);

    if (new_group_xkb_no < xkb->group_count)
    {
        XkbLockGroup(GDK_DISPLAY(), XkbUseCoreKbd, new_group_xkb_no);
        refresh_group_xkb(xkb);
    }
}


