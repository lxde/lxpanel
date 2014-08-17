
// gcc layouts_n_variants.c -o layouts_n_variants `pkg-config --cflags --libs gobject-2.0`

/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>


#define MAX_BUF_LEN  128

int  main(int argc, char *argv[])
{
    FILE *fp;
    char  buf[MAX_BUF_LEN];
    char *x11_xkb_dir=NULL;

    /* Open the command for reading. */
    fp = popen("pkg-config --variable=xkb_base xkeyboard-config", "r");
    if(fp == NULL)
    {
        printf("Failed to run command\n");
        return -1;
    }

    /* Read the output a line at a time - output it. */
    while(fgets(buf, MAX_BUF_LEN, fp) != NULL)
    {
        if(buf[0] && buf[0] == '/')
        {
            char *p_char = strchr(buf, '\n');
            if(p_char != NULL) *p_char = '\0';
            x11_xkb_dir = g_strdup(buf);
            break;
        }
    }
    /* close */
    pclose(fp);

    printf("Found: '%s'\n", x11_xkb_dir != NULL ? x11_xkb_dir : "none");
    g_free(x11_xkb_dir);

    return 0;
}
