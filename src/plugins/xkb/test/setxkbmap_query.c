
// gcc setxkbmap_query.c -o setxkbmap_query `pkg-config --cflags --libs gobject-2.0`

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
#include <glib.h>


#define MAX_BUF_LEN  128

int  main(int argc, char *argv[])
{
    FILE *fp;
    char  buf[MAX_BUF_LEN];
    char *layouts=NULL, *variants=NULL, *model=NULL;

    /* Open the command for reading. */
    fp = popen("setxkbmap -query", "r");
    if(fp == NULL)
    {
        printf("Failed to run command\n");
        return -1;
    }

    GRegex *p_regex_model = g_regex_new("(?<=model:).*", 0, 0, NULL);
    GRegex *p_regex_layouts = g_regex_new("(?<=layout:).*", 0, 0, NULL);
    GRegex *p_regex_variants = g_regex_new("(?<=variant:).*", 0, 0, NULL);
    GMatchInfo *p_match_info;

    /* Read the output a line at a time - output it. */
    while(fgets(buf, MAX_BUF_LEN, fp) != NULL)
    {
        // model
        g_regex_match(p_regex_model, buf, 0, &p_match_info);
        if(g_match_info_matches(p_match_info))
        {
            gchar *model = g_strchug(g_match_info_fetch(p_match_info, 0));
            g_print("Found model: '%s'\n", model);
            g_free(model);
            g_match_info_free(p_match_info);
            continue;
        }
        g_match_info_free(p_match_info);

        // layouts
        g_regex_match(p_regex_layouts, buf, 0, &p_match_info);
        if(g_match_info_matches(p_match_info))
        {
            gchar *layouts = g_strchug(g_match_info_fetch(p_match_info, 0));
            g_print("Found layouts: '%s'\n", layouts);
            g_free(layouts);
            g_match_info_free(p_match_info);
            continue;
        }
        g_match_info_free(p_match_info);

        // variants
        g_regex_match(p_regex_variants, buf, 0, &p_match_info);
        if(g_match_info_matches(p_match_info))
        {
            gchar *variants = g_strchug(g_match_info_fetch(p_match_info, 0));
            g_print("Found variants: '%s'\n", variants);
            g_free(variants);
            g_match_info_free(p_match_info);
            continue;
        }
        g_match_info_free(p_match_info);

        //printf("No match: '%s'\n", buf);
    }

    g_regex_unref(p_regex_model);
    g_regex_unref(p_regex_layouts);
    g_regex_unref(p_regex_variants);

    /* close */
    pclose(fp);

    return 0;
}
