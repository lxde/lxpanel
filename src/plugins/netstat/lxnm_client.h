#ifndef HAVE_NS_LXNM_H
#define HAVE_NS_LXNM_H

#include "netstat.h"

#define LXNM_SOCKET "/var/run/lxnm.socket"

/* Command */
#define LXNM_VERSION                   0
#define LXNM_ETHERNET_UP               1
#define LXNM_ETHERNET_DOWN             2
#define LXNM_ETHERNET_REPAIR           3
#define LXNM_WIRELESS_UP               4
#define LXNM_WIRELESS_DOWN             5
#define LXNM_WIRELESS_REPAIR           6
#define LXNM_WIRELESS_CONNECT          7

char *asc2hex(char *src);
GIOChannel *lxnm_socket(void);
void lxnm_close(GIOChannel *gio);
void lxnm_send_command(GIOChannel *gio, int command, const char* cmdargs);

#endif
