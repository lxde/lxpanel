#ifndef HAVE_NS_WIRELESS_H
#define HAVE_NS_WIRELESS_H

#include "netstat.h"

#define NS_WIRELESS_AUTH_WEP       0
#define NS_WIRELESS_AUTH_WPA       1
#define NS_WIRELESS_AUTH_WPA_PSK   2

typedef struct {
	char *essid;
	char *apaddr;
	gboolean haskey;
	int quality;
} ap_info;

typedef struct ap_info_node {
	ap_info              info;
	struct ap_info_node *next;
} APINFOLIST;

APINFOLIST *wireless_ap_scanning(int iwsockfd, const char *ifname);

#endif
