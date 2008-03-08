#ifndef HAVE_FNETDAEMON_H
#define HAVE_FNETDAEMON_H

#include <stdio.h>
#include <netinet/in.h>

#define NETDEV_STAT_NORMAL	0
#define NETDEV_STAT_PROBLEM	1
#define NETDEV_STAT_RENEW	2
#define NETDEV_STAT_BOTHRS	3
#define NETDEV_STAT_SENDDATA	4
#define NETDEV_STAT_RECVDATA	5

/* forward declaration for UI interaction. */
struct statusicon;

typedef struct {
	char *ifname;
	char *mac;
	char *ipaddr;
	char *dest;
	char *bcast;
	char *mask;
	int flags;
	gboolean alive;
	gboolean enable;
	gboolean updated;
	gboolean plug;
	gboolean connected;

	/* wireless */
	gboolean wireless;
	char *protocol;
	char *essid;
	int *quality;

	int status;
	gulong recv_bytes;
	gulong recv_packets;
	gulong trans_bytes;
	gulong trans_packets;

	/* systray */
	struct statusicon *status_icon;
} netdevice;

typedef struct netdevice_node {
	netdevice             info;
	struct netdevice_node *prev;
	struct netdevice_node *next;
} NETDEVLIST;
typedef NETDEVLIST *NETDEVLIST_PTR;

typedef struct {
	int dev_count;
	int sockfd;
	int iwsockfd;
	GIOChannel *lxndsockfd;
	FILE *netdev_fp;
	NETDEVLIST_PTR netdevlist;
} FNETD;

#endif
