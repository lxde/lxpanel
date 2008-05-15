/**
 * Copyright (c) 2008 LxDE Developers, see the file AUTHORS for details.
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
#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <iwlib.h>
#include "netstat.h"
#include "lxnm_client.h"

char*
asc2hex(char *src)
{
    char *buf, *tmp;
    char c[3];

    buf = malloc(sizeof(char)+strlen(src)*2);
    tmp = buf;

    for (;*src!='\0';src++) {
        sprintf(c, "%X", *src);
        *tmp = c[0];
        *(tmp+1) = c[1];
        tmp += 2;
    }

    *tmp = '\0';
    return buf;
}

static gboolean
lxnm_read_channel(GIOChannel *gio, GIOCondition condition, gpointer data)
{
/*
    GIOStatus ret;
    GError *err = NULL;
    gchar *msg;
    gsize len;

    if (condition & G_IO_HUP)
        g_error ("Read end of pipe died!\n");

    ret = g_io_channel_read_line (gio, &msg, &len, NULL, &err);
    if (ret == G_IO_STATUS_ERROR)
        g_error ("Error reading: %s\n", err->message);

    printf ("Read %u bytes: %s\n", len, msg);

    g_free (msg);
*/
    return TRUE;
}

GIOChannel *
lxnm_socket(void)
{
    GIOChannel *gio;
    int sockfd;
    struct sockaddr_un sa_un;

    /* create socket */
    sockfd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (sockfd < 0) {
        return NULL;
    }

    /* Initiate socket */
    bzero(&sa_un, sizeof(sa_un));

    /* setting UNIX socket */
    sa_un.sun_family = AF_UNIX;
    snprintf(sa_un.sun_path, sizeof(sa_un.sun_path), LXNM_SOCKET);

    if (connect(sockfd, (struct sockaddr *) &sa_un, sizeof (sa_un)) < 0) {
        return NULL;
    }

    gio = g_io_channel_unix_new(sockfd);
    g_io_channel_set_encoding(gio, NULL, NULL);
    g_io_add_watch(gio, G_IO_IN | G_IO_HUP, lxnm_read_channel, NULL);

    return gio;
}

void lxnm_close(GIOChannel *gio)
{
    if (gio)
        close(g_io_channel_unix_get_fd(gio));
}

void
lxnm_send_command(GIOChannel *gio, int command, const char* cmdargs)
{
    char *msg;
    gsize len;

    if (gio==NULL)
        return;

    msg = g_strdup_printf("%d %s\n", command, cmdargs);
    g_io_channel_write_chars(gio, msg, -1, &len, NULL);
    g_io_channel_flush(gio, NULL);
}

char *lxnm_wireless_command_make(const char *ifname, const char *essid,
								 const char *apaddr, const char *key,
								 int protocol, int key_mgmt,
								 int group, int pairwise)
{
    char *cmd_essid;
    char *cmd_key;

    if (essid==NULL) {
        cmd_essid = g_strdup("NULL");
    } else if (strlen(essid)==0) {
        cmd_essid = g_strdup("NULL");
    } else {
        cmd_essid = essid;
    }

    if (strlen(key)==0) {
        cmd_key = g_strdup("OFF");
    } else {
        cmd_key = key;
    }

    return g_strdup_printf("%s %s %s %s %d %d %d %d", ifname, asc2hex(cmd_essid),
                                                   apaddr, asc2hex(cmd_key),
                                                   protocol, key_mgmt,
                                                   group, pairwise);
}
