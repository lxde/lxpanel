#ifndef HAVE_NS_LXNETDAEMON_H
#define HAVE_NS_LXNETDAEMON_H

#include "fnetdaemon.h"

#define LXNETDAEMON_SOCKET "/var/run/lxnetdaemon.socket"

/* Command */
#define LXND_VERSION                   0
#define LXND_ETHERNET_UP               1
#define LXND_ETHERNET_DOWN             2
#define LXND_ETHERNET_REPAIR           3
#define LXND_WIRELESS_UP               4
#define LXND_WIRELESS_DOWN             5
#define LXND_WIRELESS_REPAIR           6
#define LXND_WIRELESS_CONNECT          7

GIOChannel *lxnetdaemon_socket(void);
void lxnetdaemon_close(GIOChannel *gio);
void lxnetdaemon_send_command(GIOChannel *gio, int command, const char* cmdargs);

#endif
