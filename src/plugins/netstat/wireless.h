#ifndef HAVE_NS_WIRELESS_H
#define HAVE_NS_WIRELESS_H

#include "netstat.h"

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
