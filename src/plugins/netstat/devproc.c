#include <string.h>
#include <glib.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/sockios.h>
#include <linux/types.h>
#include "nsconfig.h"
#include "fnetdaemon.h"
#include "devproc.h"

/* network device list */
int netproc_netdevlist_clear(NETDEVLIST_PTR *netdev_list)
{
	NETDEVLIST_PTR ptr;
	NETDEVLIST_PTR ptr_del;

	if (*netdev_list==NULL)
		return 0;

	ptr = *netdev_list;
	while(ptr->next!=NULL) {
		ptr_del = ptr;
		ptr = ptr->next;
		free(ptr_del);
	}

	*netdev_list = NULL;

	return 0;
}

void netproc_netdevlist_add(NETDEVLIST_PTR *netdev_list,
                                   const char *ifname,
                                   gulong recv_bytes,
                                   gulong recv_packets,
                                   gulong trans_bytes,
                                   gulong trans_packets)
{
	NETDEVLIST_PTR new_dev;

	new_dev = malloc(sizeof(NETDEVLIST));
	new_dev->info.ifname = g_strdup(ifname);
	new_dev->info.ipaddr = NULL;
	new_dev->info.bcast = NULL;
	new_dev->info.mask = NULL;
	new_dev->info.alive = TRUE;
	new_dev->info.enable = TRUE;
	new_dev->info.updated = TRUE;
	new_dev->info.plug = TRUE;
	new_dev->info.connected = TRUE;
	new_dev->info.status = NETDEV_STAT_NORMAL;
	new_dev->info.recv_bytes = recv_bytes;
	new_dev->info.recv_packets = recv_packets;
	new_dev->info.trans_bytes = trans_bytes;
	new_dev->info.trans_packets = trans_packets;
	new_dev->prev = NULL;
	new_dev->next = *netdev_list;
	if (new_dev->next!=NULL) {
		new_dev->next->prev = new_dev;
	}
	*netdev_list = new_dev;
}

NETDEVLIST_PTR netproc_netdevlist_find(NETDEVLIST_PTR netdev_list, const char *ifname)
{
	NETDEVLIST_PTR ptr;

	if (netdev_list==NULL)
		return NULL;

	ptr = netdev_list;
	do {

		if (strcmp(ptr->info.ifname, ifname)==0)
			return ptr;

		ptr = ptr->next;
	} while(ptr!=NULL);

	return NULL;
}

static char *netproc_parse_ifname(const char *buffer)
{
	char *ptr;
	if ((ptr = strchr(buffer, ':'))) {
		*ptr++ = '\0';
	}

	return ptr;
}

static void netproc_parse_stats_header(const char *buffer,
                                       int *prx_idx,
                                       int *ptx_idx,
                                       int *brx_idx,
                                       int *btx_idx)
{
	char *p;
	int i;

	*prx_idx = *ptx_idx = -1;
	*brx_idx = *btx_idx = -1;

	p = strtok (buffer, "| \t\n");
	p = strtok (NULL, "| \t\n");
	for (i = 0; p; i++, p = strtok (NULL, "| \t\n")) {
		if (!strcmp (p, "packets")) {
			if (*prx_idx == -1)
				*prx_idx = i;
			else
				*ptx_idx = i;
		} else if (!strcmp (p, "bytes")) {
			if (*brx_idx == -1)
				*brx_idx = i;
			else
				*btx_idx = i;
		}
	}
}

static gboolean netproc_parse_status(const char *buffer,
                                     int *prx_idx,
                                     int *ptx_idx,
                                     gulong *in_packets,
                                     gulong *out_packets,
                                     int *brx_idx,
                                     int *btx_idx,
                                     gulong *in_bytes,
                                     gulong *out_bytes)
{
	char *ptr;
	int i;
	ptr = strtok(buffer, " \t\n");
	for (i=0;ptr;i++, ptr = strtok(NULL, " \t\n")) {
		if (i==prx_idx)
			*in_packets = g_ascii_strtoull(ptr, NULL, 10);
		else if (i==ptx_idx)
			*out_packets = g_ascii_strtoull(ptr, NULL, 10);
		else if (i==brx_idx)
			*in_bytes = g_ascii_strtoull(ptr, NULL, 10);
		else if (i==btx_idx)
			*out_bytes = g_ascii_strtoull(ptr, NULL, 10);
	}

	if (i <= prx_idx || i <= ptx_idx || i <= brx_idx || i <=btx_idx)
		return FALSE;

	return TRUE;
}

FILE *netproc_open()
{
	return fopen("/proc/net/dev", "r");
}

void netproc_close(FILE *fp)
{
	fclose(fp);
}

int netproc_scandevice(int sockfd, FILE *fp, NETDEVLIST_PTR *netdev_list)
{
	char buffer[512];
	int count = 0;
	int prx_idx, ptx_idx, brx_idx, btx_idx;
	gulong in_packets, out_packets, in_bytes, out_bytes;

	fgets (buffer, sizeof(buffer), fp);
	fgets (buffer, sizeof(buffer), fp);
	netproc_parse_stats_header(buffer, &prx_idx, &ptx_idx, &brx_idx, &btx_idx);

	while (fgets(buffer, sizeof(buffer), fp)) {
		struct ifreq ifr;
		struct linktest_value edata;
		char *status;
		char *name;
		NETDEVLIST_PTR devptr = NULL;

		/* getting interface name */
		name = buffer;
		while (g_ascii_isspace(name[0])) {
			name++;
		}

		/* reading packet infomation */
		status = netproc_parse_ifname(name);
		netproc_parse_status(status, prx_idx, ptx_idx, &in_packets, &out_packets,
				     brx_idx, btx_idx, &in_bytes, &out_bytes);

		/* check interface hw_type */
		bzero(&ifr, sizeof(ifr));
		strncpy(ifr.ifr_name, name, strlen(name)+1);
  		ifr.ifr_name[strlen(name)+1] = '\0';
		if (ioctl(sockfd, SIOCGIFHWADDR, &ifr)<0)
			continue;

		/* hw_types is not Ethernet and PPP */
		if (ifr.ifr_hwaddr.sa_family!=ARPHRD_ETHER&&ifr.ifr_hwaddr.sa_family!=ARPHRD_PPP)
			continue;

		/* detecting new interface */
		if ((devptr = netproc_netdevlist_find(*netdev_list, name))==NULL) {
			netproc_netdevlist_add(netdev_list, name, in_bytes, in_packets, out_bytes, out_packets);
			devptr = netproc_netdevlist_find(*netdev_list, name);
		} else {
			/* Setting device status and update flags */
			if (devptr->info.recv_packets!=in_packets&&devptr->info.trans_packets!=out_packets) {
				if (devptr->info.status!=NETDEV_STAT_BOTHRS)
					devptr->info.updated = TRUE;
			
				devptr->info.status = NETDEV_STAT_BOTHRS;
			} else if (devptr->info.recv_packets!=in_packets) {
				if (devptr->info.status!=NETDEV_STAT_RECVDATA)
					devptr->info.updated = TRUE;

				devptr->info.status = NETDEV_STAT_RECVDATA;
			} else if (devptr->info.trans_packets!=out_packets) {
				if (devptr->info.status!=NETDEV_STAT_SENDDATA)
					devptr->info.updated = TRUE;

				devptr->info.status = NETDEV_STAT_SENDDATA;
			} else {
				if (devptr->info.status!=NETDEV_STAT_NORMAL)
					devptr->info.updated = TRUE;

				devptr->info.status = NETDEV_STAT_NORMAL;
			}

			/* Recording r/t information */
			devptr->info.recv_bytes = in_bytes;
			devptr->info.recv_packets = in_packets;
			devptr->info.trans_bytes = out_bytes;
			devptr->info.trans_packets = out_packets;

			/* give device a life */
			devptr->info.alive = TRUE;
		}

		/* Enable */
		bzero(&ifr, sizeof(ifr));
		strcpy(ifr.ifr_name, devptr->info.ifname);
		if (ioctl(sockfd, SIOCGIFFLAGS, &ifr)>=0) {
			devptr->info.flags = ifr.ifr_flags;
			if (ifr.ifr_flags & IFF_UP) {
				devptr->info.enable = TRUE;
				devptr->info.updated = TRUE;
			} else {
				devptr->info.enable = FALSE;
				devptr->info.updated = TRUE;
			}

			if (devptr->info.enable) {
				/* plug */
				bzero(&ifr, sizeof(ifr));
				strcpy(ifr.ifr_name, devptr->info.ifname);
				strncpy(ifr.ifr_name, name, strlen(name));

				edata.cmd = 0x0000000a;
				ifr.ifr_data = (caddr_t)&edata;
				if (ioctl(sockfd, SIOCETHTOOL, &ifr)>=0) {
					if (edata.data) {
						if (!devptr->info.plug) {
							devptr->info.updated = TRUE;
							devptr->info.plug = TRUE;
						}
					} else if (devptr->info.plug) {
						devptr->info.updated = TRUE;
						devptr->info.plug = FALSE;
					}
					g_print("%s\n", devptr->info.ifname);

					/* get network information */
					if (devptr->info.enable&&devptr->info.plug) {
						if (devptr->info.flags & IFF_RUNNING) {
							bzero(&ifr, sizeof(ifr));
  							ifr.ifr_addr.sa_family = AF_INET;
							/* IP Address */
							strcpy(ifr.ifr_name, devptr->info.ifname);
							ioctl(sockfd, SIOCGIFADDR, &ifr);
							devptr->info.ipaddr = g_strdup(inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr));

							/* Point-to-Porint Address */
							if (devptr->info.flags & IFF_POINTOPOINT) {
								strcpy(ifr.ifr_name, devptr->info.ifname);
								ioctl(sockfd, SIOCGIFDSTADDR, &ifr);
								devptr->info.ipaddr = g_strdup(inet_ntoa(((struct sockaddr_in*)&ifr.ifr_dstaddr)->sin_addr));
							}

							/* Broadcast */
							if (devptr->info.flags & IFF_BROADCAST) {
								strcpy(ifr.ifr_name, devptr->info.ifname);
								ioctl(sockfd, SIOCGIFBRDADDR, &ifr);
								devptr->info.bcast = g_strdup(inet_ntoa(((struct sockaddr_in*)&ifr.ifr_broadaddr)->sin_addr));
							}

							/* Netmask */
							strcpy(ifr.ifr_name, devptr->info.ifname);
							ioctl(sockfd, SIOCGIFNETMASK, &ifr);
							devptr->info.mask = g_strdup(inet_ntoa(((struct sockaddr_in*)&ifr.ifr_addr)->sin_addr));

							if (strcmp(devptr->info.ipaddr, "0.0.0.0")==0) {
								devptr->info.status = NETDEV_STAT_PROBLEM;
								/* has connection problem  */
								if (devptr->info.connected) {
									devptr->info.connected = FALSE;
									devptr->info.updated = TRUE;
								}
							} else {
								if (!devptr->info.connected) {
									devptr->info.status = NETDEV_STAT_NORMAL;
									devptr->info.connected = TRUE;
									devptr->info.updated = TRUE;
								}
							}
						} else {
							devptr->info.status = NETDEV_STAT_PROBLEM;
							/* has connection problem  */
							if (devptr->info.connected) {
								devptr->info.connected = FALSE;
								devptr->info.updated = TRUE;
							}
						}
					}
				}
			}

		}

		devptr = NULL;
		count++;
	}

	rewind(fp);
	fflush(fp);

	return count;
}

void netproc_alive(NETDEVLIST_PTR netdev_list)
{
	NETDEVLIST_PTR ptr;

	if (netdev_list==NULL) {
		return;
	}

	ptr = netdev_list;
	do {
		if (strcmp(ptr->info.ifname, "lo")!=0) {
			ptr->info.alive = FALSE;
		}
		ptr = ptr->next;
	} while(ptr!=NULL);
}

void netproc_devicelist_clear(NETDEVLIST_PTR *netdev_list)
{
	NETDEVLIST_PTR ptr;
	NETDEVLIST_PTR prev_ptr;
	NETDEVLIST_PTR del_ptr;

	if (netdev_list==NULL) {
		return;
	}

	prev_ptr = NULL;
	ptr = *netdev_list;
	do {
		if (strcmp(ptr->info.ifname, "lo")!=0) {
			if (!ptr->info.alive) {
				if (prev_ptr!=NULL) {
					ptr->prev->next = ptr->next;
					ptr->next->prev = ptr->prev;
				} else {
					ptr->next->prev = NULL;
					*netdev_list = ptr->next;
				}

				del_ptr = ptr;
				ptr = ptr->next;
				free(del_ptr);
				printf("%s\n", ptr->info.ifname);
			} else {
				prev_ptr = ptr;
				ptr = ptr->next;
			}
		} else {
			prev_ptr = ptr;
			ptr = ptr->next;
		}
	} while(ptr!=NULL);
}

void netproc_listener(FNETD *fnetd)
{
	if (fnetd->sockfd) {
		netproc_alive(fnetd->netdevlist);
		fnetd->netdev_fp = netproc_open();
		netproc_scandevice(fnetd->sockfd, fnetd->netdev_fp, &fnetd->netdevlist);
		netproc_close(fnetd->netdev_fp);
	}
}


void netproc_print(NETDEVLIST_PTR netdev_list)
{
	NETDEVLIST_PTR ptr;

	if (netdev_list==NULL) {
		return;
	}

	ptr = netdev_list;
	do {
		if (strcmp(ptr->info.ifname, "lo")!=0) {
			printf("%s: %d\n", ptr->info.ifname, ptr->info.status);
/*
			printf("%s:%d:%d:%d:%d\n", ptr->info.ifname,
	                	                   ptr->info.recv_bytes,
	                        	           ptr->info.recv_packets,
	                                	   ptr->info.trans_bytes,
	                                   	   ptr->info.trans_packets);
*/
		}
		ptr = ptr->next;
	} while(ptr!=NULL);
}
