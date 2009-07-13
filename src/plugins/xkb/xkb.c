/*
// ====================================================================
//  xfce4-xkb-plugin - XFCE4 Xkb Layout Indicator panel plugin
// -------------------------------------------------------------------
//  Alexander Iliev <sasoiliev@mamul.org>
//  20-Feb-04
// -------------------------------------------------------------------
//  Parts of this code belong to Michael Glickman <wmalms@yahooo.com>
//  and his program wmxkb.
//  WARNING: DO NOT BOTHER Michael Glickman WITH QUESTIONS ABOUT THIS
//           PROGRAM!!! SEND INSTEAD EMAILS TO <sasoiliev@mamul.org>
//====================================================================
*/

/* Modified by Hong Jen Yee (PCMan) <pcman.tw@gmail.com> on 2008-04-06 for lxpanel */

#include "xkb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/XKBlib.h>

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib.h>

static void xkb_enter_locale_by_process(XkbPlugin * xkb);
static void refresh_group_xkb(XkbPlugin * xkb);
static int do_init_xkb(XkbPlugin * xkb);

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

/* Convert a group number to a symbol name. */
const char * xkb_get_symbol_name_by_res_no(XkbPlugin * xkb, int n) 
{
    return xkb->symbol_names[n];
}

/* Get the current group name. */
const char * xkb_get_current_group_name(XkbPlugin * xkb) 
{
    return xkb_get_symbol_name_by_res_no(xkb, xkb->current_group_xkb_no);
}

/* Get the current group name converted to lowercase. */
const char * xkb_get_current_group_name_lowercase(XkbPlugin * xkb) 
{
    const char * tmp = xkb_get_current_group_name(xkb);
    return g_utf8_strdown(tmp, -1);
}

/* Refresh current group number from Xkb state. */
static void refresh_group_xkb(XkbPlugin * xkb) 
{
    XkbStateRec xkb_state;
    XkbGetState(xkb->dsp, xkb->device_id, &xkb_state);
    xkb->current_group_xkb_no = xkb_state.group;
}

/* Initialize the Xkb structures. */
static int do_init_xkb(XkbPlugin * xkb) 
{
    /* Create hash table. */
    xkb->group_hash_table = g_hash_table_new(g_direct_hash, NULL);

    /* Initialize the Xkb extension. */
    int major, minor, opcode;
    Bool status = XkbQueryExtension(xkb->dsp, &opcode,
        &xkb->base_event_code, &xkb->base_error_code, &major, &minor);

    /* Use the core keyboard. */
    xkb->device_id = XkbUseCoreKbd;

    /* Allocate a keyboard description structure. */
    XkbDescRec * kbd_desc_ptr = XkbAllocKeyboard();
    if (kbd_desc_ptr == NULL)
    {
        ERR("Failed to get keyboard description\n");
        goto HastaLaVista;
    }
    kbd_desc_ptr->dpy = xkb->dsp;

    /* Fetch information into the keyboard description. */
    XkbGetControls(xkb->dsp, XkbAllControlsMask, kbd_desc_ptr);
    XkbGetNames(xkb->dsp, XkbSymbolsNameMask, kbd_desc_ptr);
    XkbGetNames(xkb->dsp, XkbGroupNamesMask, kbd_desc_ptr);

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
    int i;
    for (i = 0; i < xkb->group_count; i++)
    {
        if (tmp_group_source[i] != None)
        {
            char * ptr = XGetAtomName(xkb->dsp, tmp_group_source[i]);
            xkb->group_names[i] = ptr;
            if ((ptr != NULL) && ((ptr = strchr(ptr, '('))) != NULL)
                *ptr = '\0';
        }
    }

    /* Fetch the symbol names. */
    Atom sym_name_atom = kbd_desc_ptr->names->symbols;
    char * sym_name;
    if ((sym_name_atom == None)
    || ((sym_name = XGetAtomName(xkb->dsp, sym_name_atom)) == NULL))
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
        xkb->symbol_names[0] = strdup("us");
        xkb->group_names[0] = strdup("US/ASCII");
        xkb->group_names[1] = strdup("Japanese");
    }
    else if (count < xkb->group_count)
    {
        /* Ensure that the names are fully initialized. */
        int j = count, k = xkb->group_count;
        while(--j >= 0) xkb->symbol_names[--k] = xkb->symbol_names[j];
        while(--k >= 0) xkb->symbol_names[k] = strdup("en_US");
    }

    /* Enxure that the names are fully initialized. */
    for (i = 0; i < xkb->group_count; i++)
    {
        if (xkb->symbol_names[i] == NULL)
        {
            ERR("\nGroup Symbol %i is undefined, set to 'U/A' !\n", i+1);
            xkb->symbol_names[i] = strdup("U/A");
        }
    }

    status = True;

HastaLaVista:
    if (kbd_desc_ptr != NULL)
        XkbFreeKeyboard(kbd_desc_ptr, 0, True);
    return status;
}

/* Initialize the Xkb interface. */
void xkb_mechanism_constructor(XkbPlugin * xkb)
{
    /* Enable the Xkb extension on all clients. */
    XkbIgnoreExtension(False);

    /* Open the display. */
    int major = XkbMajorVersion;
    int minor = XkbMinorVersion;
    char * display_name = "";
    int event_code;
    int error_rtrn;
    int reason_rtrn;
    xkb->dsp = XkbOpenDisplay(display_name, &event_code, &error_rtrn, &major, &minor, &reason_rtrn);

    switch (reason_rtrn)
    {
        case XkbOD_BadLibraryVersion:
            ERR("Bad XKB library version.\n");
            return;
        case XkbOD_ConnectionRefused:
            ERR("Connection to X server refused.\n");
            return;
        case XkbOD_BadServerVersion:
            ERR("Bad X server version.\n");
            return;
        case XkbOD_NonXkbServer:
            ERR("XKB not present.\n");
            return;
        case XkbOD_Success:
            break;
    }

    /* Initialize our mechanism. */
    if (do_init_xkb(xkb) != True)
        return;

    /* Specify events we will receive. */
    XkbSelectEventDetails(xkb->dsp, xkb->device_id, XkbStateNotify, XkbAllStateComponentsMask, XkbGroupStateMask);

    /* Get current state. */
    refresh_group_xkb(xkb);
}

/* Deallocate resources associated with Xkb interface. */
void xkb_mechanism_destructor(XkbPlugin * xkb) 
{
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

    /* Close the display. */
    XCloseDisplay(xkb->dsp);
    xkb->dsp = NULL;

    /* Destroy the hash table. */
    g_hash_table_destroy(xkb->group_hash_table);
    xkb->group_hash_table = NULL;
}

/* Return the connection number for the display. */
int xkb_get_connection_number(XkbPlugin * xkb)
{
    return ConnectionNumber(xkb->dsp);
}

/* Set the layout to the next layout. */
int xkb_change_group(XkbPlugin * xkb, int increment) 
{
    /* Apply the increment and wrap the result. */
    int next_group = xkb->current_group_xkb_no + increment;
    if (next_group < 0) next_group = xkb->group_count - 1;
    if (next_group >= xkb->group_count) next_group = 0;

    /* Execute the change. */
    XkbLockGroup(xkb->dsp, xkb->device_id, next_group);
    refresh_group_xkb(xkb);
    xkb_redraw(xkb);
    xkb_enter_locale_by_process(xkb);
    return 1;
}

/* Callback when activity detected on the Xkb channel. */
gboolean xkb_gio_callback(GIOChannel * source, GIOCondition condition, gpointer data) 
{
    XkbPlugin * xkb = (XkbPlugin *) data;

    XkbEvent evnt;
    XNextEvent(xkb->dsp, &evnt.core);
    if ((evnt.type == xkb->base_event_code)
    && (evnt.any.xkb_type == XkbStateNotify)
    && (evnt.state.group != xkb->current_group_xkb_no))
    {
        /* Switch to the new group and redraw the display. */
        xkb->current_group_xkb_no = evnt.state.group;
        refresh_group_xkb(xkb);
        xkb_redraw(xkb);
        xkb_enter_locale_by_process(xkb);
    }
    return TRUE;
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
        XkbLockGroup(xkb->dsp, xkb->device_id, new_group_xkb_no);
        refresh_group_xkb(xkb);
    }
}


