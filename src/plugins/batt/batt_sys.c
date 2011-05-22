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
    battery_reset(b);
    b->battery_num = battery_num;
    battery_num++;
    return b;
}

void battery_reset( battery * b) {
    b->type_battery = TRUE;
    b->capacity_unit = "mAh";
    b->last_capacity_unit = -1;
    b->last_capacity = -1;
    b->voltage = -1;
    b->design_capacity_unit = -1;
    b->design_capacity = -1;
    b->remaining_energy = -1;
    b->remaining_capacity = -1;
    b->present_rate = -1;
    b->state = NULL;
}

static gchar* parse_info_file(char *filename)
{
    char *buf = NULL;

    if ( g_file_get_contents( filename, &buf, NULL, NULL) == TRUE ) 
    {
	gchar *value = g_strdup( buf );
	value = g_strstrip( value );
	g_free( buf );
	return value;
    }
    return NULL;
}

static int get_unit_value(char *value)
{
    int n = -1;
    sscanf(value, "%d", &n);
    return n;
}

void battery_print(battery *b, int show_capacity)
{
    if ( b->type_battery )
    {
	if (b->state) {
	    
	    printf("%s %d: %s, %d%%", BATTERY_DESC, b->battery_num - 1, b->state, b->percentage);
	    

	    if (b->seconds > 0) {
		b->hours = b->seconds / 3600;
		b->seconds -= 3600 * b->hours;
		b->minutes = b->seconds / 60;
		b->seconds -= 60 * b->minutes;
		printf(", %02d:%02d:%02d%s", b->hours, b->minutes, b->seconds, b->poststr);
	    } else if (b->poststr != NULL) {
		printf(", %s", b->poststr);
	    }


	    printf("\n");
	    
	    if (show_capacity && b->design_capacity > 0) {
		if (b->last_capacity <= 100) {
		    /* some broken systems just give a percentage here */
		    b->percentage = b->last_capacity;
		    b->last_capacity = b->percentage * b->design_capacity / 100;
		} else {
		    b->percentage = b->last_capacity * 100 / b->design_capacity;
		}
		if (b->percentage > 100)
		    b->percentage = 100;

		printf ("%s %d: design capacity %d %s, last full capacity %d %s = %d%%\n",
			BATTERY_DESC, b->battery_num - 1, b->design_capacity, b->capacity_unit, b->last_capacity, b->capacity_unit, b->percentage);
	    }
	}
    }
}    


void battery_update( battery *b ) {
    int i = 0;
    const gchar *sys_list[] = {
	"current_now",
	"charge_now",
	"energy_now",
	"voltage_now",
	"voltage_min_design",
	"charge_full",
	"energy_full",
	"charge_full_design",
	"energy_full_design",
	"online",
	"status",
	"type",
	NULL
    };
    const gchar *sys_file;

    battery_reset(b);

    while ( (sys_file = sys_list[i]) != NULL ) {
    
	gchar *file_content;
	GString *filename = g_string_new( ACPI_PATH_SYS_POWER_SUPPY );
	g_string_append_printf ( filename, "/%s/%s", b->path, 
				 sys_file );
	if ((file_content = parse_info_file(filename->str)) != NULL) {
	    
	    if ( strcmp("charge_now", sys_file ) == 0 ) {
		b->remaining_capacity = get_unit_value((gchar*) file_content) / 1000;
		if (!b->state)
		    b->state = "available";
	    }
	    else if ( strcmp("energy_now", sys_file ) == 0 ) {
		b->remaining_energy = get_unit_value((gchar*) file_content) / 1000;
		if (!b->state)
		    b->state = "available";
	    }
	    else if ( strcmp("current_now", sys_file ) == 0 ) {
		b->present_rate = get_unit_value((gchar*) file_content) / 1000;
	    }
	    else if ( strcmp("charge_full", sys_file ) == 0 ) {
		b->last_capacity = get_unit_value((gchar*) file_content) / 1000;
		if (!b->state)
		    b->state = ("available");
	    }
	    else if ( strcmp("energy_full", sys_file ) == 0 ) {
		b->last_capacity_unit = get_unit_value((gchar*) file_content) / 1000;
		if (!b->state)
		    b->state = ("available");
	    }
	    else if ( strcmp("charge_full_design", sys_file ) == 0 ) {
		b->design_capacity = get_unit_value((gchar*) file_content) / 1000;
	    }
	    else if ( strcmp("energy_full_design", sys_file ) == 0 ) {
		b->design_capacity_unit = get_unit_value((gchar*) file_content) / 1000;
	    }
	    else if ( strcmp("type", sys_file ) == 0 ) {
		b->type_battery = (strcasecmp(file_content, "battery") == 0 );
	    }
	    else if ( ( strcmp("status", sys_file ) == 0 ) || strcmp("state", sys_file ) == 0 ) 
		b->state = file_content;
	    else if ( strcmp("voltage_now", sys_file ) == 0 ) {
		b->voltage = get_unit_value((gchar*) file_content) / 1000;
	    }

	    g_string_free( filename, TRUE );
	}
	i++;
    }
    
    /* convert energy values (in mWh) to charge values (in mAh) if needed and possible */
    if (b->last_capacity_unit != -1 && b->last_capacity == -1) {
	if (b->voltage != -1) {
	    b->last_capacity = b->last_capacity_unit * 1000 / b->voltage;
	} else {
	    b->last_capacity = b->last_capacity_unit;
	    b->capacity_unit = "mWh";
	}
    }
    if (b->design_capacity_unit != -1 && b->design_capacity == -1) {
	if (b->voltage != -1) {
	    b->design_capacity = b->design_capacity_unit * 1000 / b->voltage;
	} else {
	    b->design_capacity = b->design_capacity_unit;
	    b->capacity_unit = "mWh";
	}
    }
    if (b->remaining_energy != -1 && b->remaining_capacity == -1) {
	if (b->voltage != -1) {
	    b->remaining_capacity = b->remaining_energy * 1000 / b->voltage;
	    b->present_rate = b->present_rate * 1000 / b->voltage;
	} else {
	    b->remaining_capacity = b->remaining_energy;
	}
    }
    if (b->last_capacity < MIN_CAPACITY)
	b->percentage = 0;
    else
	b->percentage = ((float) b->remaining_energy * 100.0) / (float) b->last_capacity_unit;
	    
    if (b->percentage > 100)
	b->percentage = 100;


	    
    if (b->present_rate == -1) {
	b->poststr = "rate information unavailable";
	b->seconds = -1;
    } else if (!strcasecmp(b->state, "charging")) {
	if (b->present_rate > MIN_PRESENT_RATE) {
	    b->seconds = 3600 * (b->last_capacity - b->remaining_capacity) / b->present_rate;
	    b->poststr = " until charged";
	} else {
	    b->poststr = "charging at zero rate - will never fully charge.";
	    b->seconds = -1;
	}
    } else if (!strcasecmp(b->state, "discharging")) {
	if (b->present_rate > MIN_PRESENT_RATE) {
	    b->seconds = 3600 * b->remaining_capacity / b->present_rate;
	    b->poststr = " remaining";
	} else {
	    b->poststr = "discharging at zero rate - will never fully discharge.";
	    b->seconds = -1;
	}
    } else {
	b->poststr = NULL;
	b->seconds = -1;
    }
	    
}

static battery* acpi_sys_get_battery_from_dir (const gchar *device_name ) {
    battery *b = battery_new();
    b->path = g_strdup( device_name );    
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
	b = acpi_sys_get_battery_from_dir(entry);
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

gboolean battery_is_charging( battery *b )
{
    return ( strcasecmp( b->state, "Unknown" ) == 0 ||
	     strcasecmp( b->state, "Full" ) == 0
	     || strcasecmp( b->state, "Charging" ) == 0 );
}

gint battery_get_remaining( battery *b )
{
    return b->seconds;
}


