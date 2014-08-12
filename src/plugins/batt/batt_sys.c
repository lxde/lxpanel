/*
 *      batt_sys.h
 *
 *      Copyright 2009 Juergen Hötzel <juergen@archlinux.org>
 *
 * 	Parts shameless stolen and glibified from acpi package
 * 	Copyright (C) 2001  Grahame Bowland <grahame@angrygoats.net>
 *	(C) 2008-2009  Michael Meskes  <meskes@debian.org>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */


#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include "batt_sys.h"
#include <glib/gstdio.h>

/* shrug: get rid of this */
#include <stdlib.h>
#include <string.h>

battery* battery_new() {
    static int battery_num = 1;
    battery * b = g_new0 ( battery, 1 );
    b->type_battery = TRUE;
    b->capacity_unit = "mAh";
    b->energy_full = -1;
    b->charge_full = -1;
    b->voltage_now = -1;
    b->energy_full_design = -1;
    b->charge_full_design = -1;
    b->energy_now = -1;
    b->charge_now = -1;
    b->current_now = -1;
    b->power_now = -1;
    b->state = NULL;
    b->battery_num = battery_num;
    b->seconds = -1;
    b->percentage = -1;
    b->poststr = NULL;
    battery_num++;
    return b;
}


static gchar* parse_info_file(battery *b, char *sys_file)
{
    char *buf = NULL;
    gchar *value = NULL;
    GString *filename = g_string_new(ACPI_PATH_SYS_POWER_SUPPY);

    g_string_append_printf (filename, "/%s/%s", b->path, sys_file);

    if (g_file_get_contents(filename->str, &buf, NULL, NULL) == TRUE) {
        value = g_strdup( buf );
        value = g_strstrip( value );
        g_free( buf );
    }

    g_string_free(filename, TRUE);

    return value;
}

/* get_gint_from_infofile():
 *         If the sys_file exists, then its value is converted to an int,
 *         divided by 1000, and returned.
 *         Failure is indicated by returning -1. */
static gint get_gint_from_infofile(battery *b, gchar *sys_file)
{
    gchar *file_content = parse_info_file(b, sys_file);

    if (file_content != NULL)
        return atoi(file_content) / 1000;

    return -1;
}

static gchar* get_gchar_from_infofile(battery *b, gchar *sys_file)
{
    return parse_info_file(b, sys_file);
}

void battery_print(battery *b, int show_capacity)
{
    if ( b->type_battery )
    {
        if (b->state) {

            printf("%s %d: %s, %d%%", BATTERY_DESC, b->battery_num - 1, b->state, b->percentage);

            if (b->seconds > 0) {
                int hours = b->seconds / 3600;
                int seconds = b->seconds - 3600 * hours;
                int minutes = seconds / 60;
                seconds -= 60 * minutes;
                printf(", %02d:%02d:%02d%s", hours, minutes, seconds,
                        b->poststr);
            } else if (b->poststr != NULL) {
                printf(", %s", b->poststr);
            }

            printf("\n");

            if (show_capacity && b->charge_full_design > 0) {
                int percentage = -1;
                int charge_full = -1;
                if (b->charge_full <= 100) {
                    /* some broken systems just give a percentage here */
                    percentage = b->charge_full;
                    charge_full = percentage * b->charge_full_design / 100;
                } else {
                    percentage = b->charge_full * 100 / b->charge_full_design;
                    charge_full = b->charge_full;
                }
                if (percentage > 100)
                    percentage = 100;

                printf ("%s %d: design capacity %d %s, "
                        "last full capacity %d %s = %d%%\n",
                        BATTERY_DESC, b->battery_num - 1, b->charge_full_design,
                        b->capacity_unit, charge_full, b->capacity_unit,
                        percentage);
            }
        }
    }
}


static gboolean battery_inserted(gchar* path)
{
    if (path == NULL)
        return FALSE;

    GString *dirname = g_string_new(ACPI_PATH_SYS_POWER_SUPPY);
    GDir *dir;

    g_string_append_printf (dirname, "/%s/", path);
    dir = g_dir_open(dirname->str, 0, NULL);
    if (dir)
        g_dir_close(dir);
    g_string_free(dirname, TRUE);

    return dir ? TRUE : FALSE;
}


battery* battery_update(battery *b)
{
    gchar *gctmp;

    if (b == NULL)
        return NULL;

    if (!battery_inserted(b->path))
        return NULL;

    /* read from sysfs */
    b->charge_now = get_gint_from_infofile(b, "charge_now");
    b->energy_now = get_gint_from_infofile(b, "energy_now");

    b->current_now = get_gint_from_infofile(b, "current_now");
    b->power_now   = get_gint_from_infofile(b, "power_now");
    /* FIXME: Some battery drivers report -1000 when the discharge rate is
     * unavailable. Others use negative values when discharging. Best we can do
     * is to treat -1 as an error, and take the absolute value otherwise.
     * Ideally the kernel would not export the sysfs file when the value is not
     * available. */
    if (b->current_now < -1)
            b->current_now = - b->current_now;

    b->charge_full = get_gint_from_infofile(b, "charge_full");
    b->energy_full = get_gint_from_infofile(b, "energy_full");

    b->charge_full_design = get_gint_from_infofile(b, "charge_full_design");
    b->energy_full_design = get_gint_from_infofile(b, "energy_full_design");

    b->voltage_now = get_gint_from_infofile(b, "voltage_now");

    gctmp = get_gchar_from_infofile(b, "type");
    b->type_battery = gctmp ? (strcasecmp(gctmp, "battery") == 0) : TRUE;

    b->state = get_gchar_from_infofile(b, "status");
    if (!b->state)
        b->state = get_gchar_from_infofile(b, "state");
    if (!b->state) {
        if (b->charge_now != -1 || b->energy_now != -1
                || b->charge_full != -1 || b->energy_full != -1)
            b->state = "available";
        else
            b->state = "unavailable";
    }


    /* convert energy values (in mWh) to charge values (in mAh) if needed and possible */

    if (b->energy_full != -1 && b->charge_full == -1) {
        if (b->voltage_now != -1 && b->voltage_now != 0) {
            b->charge_full = b->energy_full * 1000 / b->voltage_now;
        } else {
            b->charge_full = b->energy_full;
            b->capacity_unit = "mWh";
        }
    }

    if (b->energy_full_design != -1 && b->charge_full_design == -1) {
        if (b->voltage_now != -1 && b->voltage_now != 0) {
            b->charge_full_design = b->energy_full_design * 1000 / b->voltage_now;
        } else {
            b->charge_full_design = b->energy_full_design;
            b->capacity_unit = "mWh";
        }
    }

    if (b->energy_now != -1 && b->charge_now == -1) {
        if (b->voltage_now != -1 && b->voltage_now != 0) {
            b->charge_now = b->energy_now * 1000 / b->voltage_now;
            if (b->current_now != -1)
                b->current_now = b->current_now * 1000 / b->voltage_now;
        } else {
            b->charge_now = b->energy_now;
        }
    }

    if (b->power_now < -1)
        b->power_now = - b->power_now;
    else if (b->power_now == -1 && b->voltage_now != -1 && b->current_now != -1)
        b->power_now = b->voltage_now * b->current_now / 1000; // P = U*I
    if (b->power_now != -1 && b->current_now == -1) {
        if (b->voltage_now != -1 && b->voltage_now != 0)
            b->current_now = b->power_now * 1000 / b->voltage_now;
    }


    if (b->charge_full < MIN_CAPACITY)
        b->percentage = 0;
    else {
        int promille = (b->charge_now * 1000) / b->charge_full;
        b->percentage = (promille + 5) / 10; /* round properly */
    }
    if (b->percentage > 100)
        b->percentage = 100;


    if (b->current_now == -1) {
        b->poststr = "rate information unavailable";
        b->seconds = -1;
    } else if (!strcasecmp(b->state, "charging")) {
        if (b->current_now > MIN_PRESENT_RATE) {
            b->seconds = 3600 * (b->charge_full - b->charge_now) / b->current_now;
            b->poststr = " until charged";
        } else {
            b->poststr = "charging at zero rate - will never fully charge.";
            b->seconds = -1;
        }
    } else if (!strcasecmp(b->state, "discharging")) {
        if (b->current_now > MIN_PRESENT_RATE) {
            b->seconds = 3600 * b->charge_now / b->current_now;
            b->poststr = " remaining";
        } else {
            b->poststr = "discharging at zero rate - will never fully discharge.";
            b->seconds = -1;
        }
    } else {
        b->poststr = NULL;
        b->seconds = -1;
    }

    return b;
}


battery *battery_get() {
    GError * error = NULL;
    const gchar *entry;
    GDir * dir = g_dir_open( ACPI_PATH_SYS_POWER_SUPPY, 0, &error );
    battery *b = NULL;
    if ( dir == NULL )
    {
        g_warning( "NO ACPI/sysfs support in kernel: %s", error->message );
        return NULL;
    }
    while ( ( entry = g_dir_read_name (dir) ) != NULL )
    {
        b = battery_new();
        b->path = g_strdup( entry );
        battery_update ( b );
        if ( b->type_battery == TRUE )
            break;
        /* ignore non-batteries */
        else {
            g_free(b);
            b = NULL;
        }
    }
    g_dir_close( dir );
    return b;
}

void battery_free(battery* bat)
{
    if (bat) {
        g_free(bat->path);
        g_free(bat);
    }
}

gboolean battery_is_charging( battery *b )
{
    if (!b->state)
        return TRUE; // Same as "Unkown"
    return ( strcasecmp( b->state, "Unknown" ) == 0 ||
             strcasecmp( b->state, "Full" ) == 0
             || strcasecmp( b->state, "Charging" ) == 0 );
}

gint battery_get_remaining( battery *b )
{
    return b->seconds;
}


/* vim: set sw=4 et sts=4 : */
