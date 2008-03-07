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
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <sys/time.h>
#include <iwlib.h>
#include "fnetdaemon.h"
#include "wireless.h"

APINFOLIST *wireless_ap_scanning(int iwsockfd, const char *ifname)
{
	struct timeval tv;
	struct iw_range iwrange;
	struct iwreq wrq;
	int timeout = 15000000;
	int iwbufsize = IW_SCAN_MAX_DATA;
	unsigned char * buffer = NULL;
	APINFOLIST *aplist = NULL;
	struct ap_info_node *info;

	/* Initializing Wireless */
	if (iw_get_range_info(iwsockfd, ifname, &iwrange)<0)
		return NULL;

	/* check scanning support */
	if (iwrange.we_version_compiled < 14)
		return NULL;

	/* Init timeout value -> 250ms */
	tv.tv_sec = 0;
	tv.tv_usec = 250000;

	wrq.u.data.pointer = NULL;
	wrq.u.data.flags = 0;
	wrq.u.data.length = 0;

	/* Initiate Scanning */
	if (iw_set_ext(iwsockfd, ifname, SIOCSIWSCAN, &wrq)<0) {
		if (errno!=EPERM)
			return NULL;

		tv.tv_usec = 0;
	}

	timeout -= tv.tv_usec;
	while(1) {
		fd_set rfds; /* File descriptors for select */
		int last_fd; /* Last fd */
		int ret;

		FD_ZERO(&rfds);
		last_fd = -1;

		ret = select(last_fd + 1, &rfds, NULL, NULL, &tv);
		if (ret < 0) {
			if(errno == EAGAIN || errno == EINTR)
				continue;

			return NULL;

		}

		/* Check if there was a timeout */
		if (ret == 0) {
			unsigned char *newbuf;
		realloc:
			/* (Re)allocate the buffer - realloc(NULL, len) == malloc(len) */
			newbuf = realloc(buffer, iwbufsize);
			if (newbuf == NULL) {
				if (buffer)
					free(buffer);

				return NULL;
			}

			buffer = newbuf;

			wrq.u.data.pointer = buffer;
			wrq.u.data.flags = 0;
			wrq.u.data.length = iwbufsize;
			if (iw_get_ext(iwsockfd, ifname, SIOCGIWSCAN, &wrq) < 0) {
				if((errno == E2BIG) && (iwrange.we_version_compiled > 16)) {
					if (wrq.u.data.length > iwbufsize)
						iwbufsize = wrq.u.data.length;
					else
						iwbufsize *= 2;

					goto realloc;
				}

				if (errno == EAGAIN) {
					tv.tv_sec = 0;
					tv.tv_usec = 100000;
					timeout -= tv.tv_usec;
					if (timeout > 0)
						continue; /* Try again later */
				}

				free(buffer);
				return NULL;
			} else
				break;
		}
	}

	if (wrq.u.data.length) {
		char buf[128];
		struct iw_event iwe;
		struct stream_descr stream;
		int ret;

		iw_init_event_stream(&stream, buffer, wrq.u.data.length);
		while(iw_extract_event_stream(&stream, &iwe, iwrange.we_version_compiled) > 0) {
			switch(iwe.cmd) {
				case SIOCGIWAP:
					/* found a new AP */
					if (aplist==NULL) {
						info = malloc(sizeof(struct ap_info_node));
						info->next = NULL;
						aplist = info;
					} else {
						info->next = malloc(sizeof(struct ap_info_node));
						info->next->next = NULL;
						info = info->next;
					}

					info->info.apaddr = g_strdup(iw_saether_ntop(&iwe.u.ap_addr, buf));
					break;
				case SIOCGIWESSID: /* ESSID */
					if (iwe.u.essid.flags)
						info->info.essid = g_strndup(iwe.u.essid.pointer, iwe.u.essid.length);
					break;
				case SIOCGIWENCODE: /* Encryption Key */
						if (!iwe.u.data.pointer)
							iwe.u.data.flags |= IW_ENCODE_NOKEY;

						/* open AP */
                        if (iwe.u.data.flags & IW_ENCODE_DISABLED)
							info->info.haskey = FALSE;
						else
							info->info.haskey = TRUE;
					break;
				case IWEVQUAL: /* Signal Quality */
					info->info.quality = (int)rint((log (iwe.u.qual.qual) / log (92)) * 100.0);
					break;
			}
		}
	} 

	free(buffer);
	return aplist;
}
