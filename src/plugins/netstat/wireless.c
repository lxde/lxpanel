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
#include "netstat.h"
#include "wireless.h"

void wireless_aplist_free(APLIST *aplist)
{
	APLIST *ptr;
	APLIST *delptr;

    if (aplist!=NULL) {
        ptr = aplist;
        do {
			g_free(aplist->info->essid);
			g_free(aplist->info->apaddr);
			g_free(aplist->info);

            delptr = ptr;
            ptr = ptr->next;
			g_free(delptr);
        } while(ptr!=NULL);
    }
}

ap_info *
wireless_parse_scanning_event(struct iw_event *event, ap_info *oldinfo)
{
	ap_info *info;

	/* found a new AP */
	if (event->cmd==SIOCGIWAP) {
		char buf[128];
		info = g_new0(ap_info, 1);
		info->apaddr = g_strdup(iw_saether_ntop(&event->u.ap_addr, buf));
		info->en_method = NS_WIRELESS_AUTH_OFF;
		info->haskey = FALSE;
	} else {
		info = oldinfo;
	}

    switch (event->cmd) {
        case SIOCGIWESSID: /* ESSID */
			if (!event->u.essid.flags||event->u.essid.length==0) {
				info->essid = NULL;
			} else {
				info->essid = g_strndup(event->u.essid.pointer, event->u.essid.length);
			}
            break;
		case IWEVQUAL: /* Signal Quality */
				info->quality = (int)rint((log (event->u.qual.qual) / log (92)) * 100.0);
            break;
        case SIOCGIWENCODE: /* Encryption */
			if (!event->u.data.pointer)
				event->u.data.flags |= IW_ENCODE_NOKEY;

			if (!(event->u.data.flags & IW_ENCODE_DISABLED)) {
				info->haskey = TRUE;
			}
            break;
		case IWEVGENIE: /* Extra information */
		{
			int offset = 0;
			int ielen = event->u.data.length;
			unsigned char *iebuf;

			while(offset <= (ielen - 2)) {
				iebuf = (event->u.data.pointer + offset);
				/* check IE type */
				switch(iebuf[offset]) {
					case 0xdd: /* WPA or else */
					{
						unsigned char wpa1_oui[3] = {0x00, 0x50, 0xf2};
						/* Not all IEs that start with 0xdd are WPA. 
						* So check that the OUI is valid. Note : offset==2 */
						if((ielen < 8) || (memcmp(&iebuf[offset], wpa1_oui, 3) != 0)
							|| (iebuf[offset + 3] != 0x01)) {
								/* WEP or else */
								info->en_method = NS_WIRELESS_AUTH_WEP;
							break;
						}
							offset += 4;
					}
					case 0x30: /* IEEE 802.11i/WPA2 */ 
						offset += 2;
						/* fix me */
						if(ielen<(offset + 4)) {
							/* IEEE 802.11i/WPA2 */
							info->en_method = NS_WIRELESS_AUTH_WPA_PSK;
						} else {
							/* WPA-PSK */
							info->en_method = NS_WIRELESS_AUTH_WPA_PSK;
						}
							
						break;
				}
				offset += iebuf[offset+1] + 2;
			}
		}
			break;
	}

    return info;
}

gboolean wireless_refresh(int iwsockfd, const char *ifname)
{
	struct iwreq wrq;
	struct iw_range range;
	struct timeval tv;
	fd_set rfds; /* File descriptors for select */
	int selfd;
	char buffer[IW_SCAN_MAX_DATA];

	/* setting interfaces name */
	strncpy(wrq.ifr_name, ifname, IFNAMSIZ);

	/* Getting range */
	iw_get_range_info(iwsockfd, ifname, &range);

	/* check scanning support */
	if (range.we_version_compiled < 14)
		return FALSE;

	/* Initiate Scanning */
	wrq.u.data.pointer = buffer;
	wrq.u.data.length = IW_SCAN_MAX_DATA;
	wrq.u.data.flags = 0;

	if (ioctl(iwsockfd, SIOCSIWSCAN, &wrq) < 0) {
		if (errno!=EPERM)
			return FALSE;
	}

	/* Init timeout value -> 250ms */
	tv.tv_sec = 0;
	tv.tv_usec = 250000;

	/* Scanning APs */
	while(1) {
		if (ioctl(iwsockfd, SIOCGIWSCAN, &wrq) < 0) {
			if (errno == EAGAIN) { /* not yet ready */
				FD_ZERO(&rfds);
				selfd = -1;

				if (select(selfd + 1, &rfds, NULL, NULL, &tv)==0)
					continue; /* timeout */
			} else {
				break;
			}
		}

		if (wrq.u.data.length <= 0)
			break;
	}

	return TRUE;
}

APLIST *wireless_scanning(int iwsockfd, const char *ifname)
{
	APLIST *ap = NULL;
	APLIST *newap;
	struct iwreq wrq;
	struct iw_range range;
	struct iw_event event;
	struct stream_descr stream;
	struct timeval tv;
	fd_set rfds; /* File descriptors for select */
	int selfd;
	int ret;
	char buffer[IW_SCAN_MAX_DATA];

	strncpy(wrq.ifr_name, ifname, IFNAMSIZ);

	/* Getting range */
	iw_get_range_info(iwsockfd, ifname, &range);

	/* check scanning support */
	if (range.we_version_compiled < 14)
		return NULL;

	/* Initiate Scanning */
	wrq.u.data.pointer = buffer;
	wrq.u.data.length = IW_SCAN_MAX_DATA;
	wrq.u.data.flags = 0;

	if (ioctl(iwsockfd, SIOCSIWSCAN, &wrq) < 0) {
		if (errno!=EPERM)
			return NULL;
	}

	/* Init timeout value -> 250ms */
	tv.tv_sec = 0;
	tv.tv_usec = 250000;

	/* Scanning APs */
	while(1) {
		if (ioctl(iwsockfd, SIOCGIWSCAN, &wrq) < 0) {
			if (errno == EAGAIN) { /* not yet ready */
				FD_ZERO(&rfds);
				selfd = -1;

				if (select(selfd + 1, &rfds, NULL, NULL, &tv)==0)
					continue; /* timeout */
			} else {
				break;
			}
		}

		if (wrq.u.data.length <= 0)
			break;

		/* Initializing event */
		iw_init_event_stream(&stream, buffer, wrq.u.data.length); 
		do {
			ret = iw_extract_event_stream(&stream, &event, range.we_version_compiled);
			if (ret > 0) {
				/* found a new AP */
				if (event.cmd==SIOCGIWAP) {
					newap = g_new0(APLIST, 1);
					newap->info = NULL;
					newap->next = ap;
					ap = newap;
				}

				/* Scanning Event */
				ap->info = wireless_parse_scanning_event(&event, ap->info);
			}
		} while (ret > 0);
	}

	return ap;
}
