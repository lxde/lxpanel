#ifndef HAVE_NS_LXNETDAEMON_H
#define HAVE_NS_LXNETDAEMON_H

#include "fnetdaemon.h"

#define LXNETDAEMON_SOCKET "/var/run/lxnetdaemon.socket"

GIOChannel *lxnetdaemon_socket(void);
void lxnetdaemon_close(GIOChannel *gio);
void lxnetdaemon_send_command(GIOChannel *gio, int command, const char* cmdargs);

#endif
