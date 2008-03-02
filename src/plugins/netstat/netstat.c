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
#include <sys/types.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <pthread.h>
#include <iwlib.h>
#include "nsconfig.h"
#include "fnetdaemon.h"
#include "devproc.h"
#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "dbg.h"

/* 1 second */
#define NETSTAT_IFACE_POLL_DELAY 1000

typedef struct {
	GtkWidget *mainw;
	FNETD *fnetd;
	char *fixcmd;
	gint ttag;
} netstat;

typedef struct {
	netstat *ns;
	NETDEVLIST_PTR netdev_list;
} netdev_info;

static
statusicon *create_statusicon(GtkWidget *box, const char *filename, const char *tooltips)
{
	statusicon *newicon;

	newicon = malloc(sizeof(statusicon));

	/* main */
	newicon->main = gtk_event_box_new();

	gtk_widget_add_events(newicon->main, GDK_BUTTON_PRESS_MASK);
	gtk_widget_set_size_request(newicon->main, 24, 24);
	gtk_box_pack_start(GTK_BOX(box), newicon->main, TRUE, TRUE, 0);

	/* icon */
	newicon->icon = gtk_image_new_from_file(filename);
	gtk_container_add(GTK_CONTAINER(newicon->main), newicon->icon);
	gtk_widget_show_all(newicon->main);

	/* tooltip */
	newicon->tooltips = gtk_tooltips_new();
#if GLIB_CHECK_VERSION( 2, 10, 0 )
	g_object_ref_sink(newicon->tooltips);
#else
	g_object_ref(newicon->tooltips);
	gtk_object_sink(newicon->tooltips);
#endif
	gtk_tooltips_set_tip(newicon->tooltips, newicon->main, tooltips, NULL);

	return newicon;
}

static void
set_statusicon_image_from_file(statusicon *widget, const char *filename)
{
	gtk_image_set_from_file(widget->icon, filename);
}

static void
set_statusicon_tooltips(statusicon *widget, const char *tooltips)
{
	gtk_tooltips_set_tip(widget->tooltips, widget->main, tooltips, NULL);
}

static void
set_statusicon_visible(statusicon *widget, gboolean b)
{
//	gtk_widget_set_child_visible(widget->main, b);
	if (b)
		gtk_widget_show(widget->main);
	else
		gtk_widget_hide(widget->main);
}

static int
actionProcess(void *arg)
{
	ENTER;
	RET(system((char *)arg));
}

/* menu handlers */
static void
fixconn(GtkWidget *widget, netdev_info *ni)
{
	pthread_t actionThread;
	char *fixcmd;

	fixcmd = g_strdup_printf(ni->ns->fixcmd, ni->netdev_list->info.ifname);

	pthread_create(&actionThread, NULL, actionProcess, fixcmd);
}

static gint
menupopup(GtkWidget *widget, GdkEvent *event, netdev_info *ni)
{
	GdkEventButton *event_button;

	g_return_val_if_fail (event != NULL, FALSE);

	if (event->type == GDK_BUTTON_PRESS) {
		event_button = (GdkEventButton *) event;
		if (event_button->button == 3) {
			GtkWidget *menu;
			GtkWidget *menu_fix;

			/* create menu */
			menu = gtk_menu_new();

			/* Repair */
			menu_fix = gtk_menu_item_new_with_label(N_("Repair"));
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_fix);
			g_signal_connect(G_OBJECT(menu_fix), "activate", G_CALLBACK(fixconn), ni);

			gtk_widget_show_all(menu);

			gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event_button->button, event_button->time);
			return TRUE;
		}
	}

	return FALSE;
}

static
char *select_icon(gboolean plug, gboolean connected, int stat)
{
	if (!plug)
		return ICONS_DISCONNECT;

	switch(stat) {
		case NETDEV_STAT_NORMAL:
			return ICONS_CONNECTED;
			break;
		case NETDEV_STAT_PROBLEM:
			return ICONS_PROBLEM;
			break;
		case NETDEV_STAT_RENEW:
			return ICONS_RENEW;
			break;
		case NETDEV_STAT_BOTHRS:
			return ICONS_BOTHRS;
			break;
		case NETDEV_STAT_SENDDATA:
			return ICONS_SENDDATA;
			break;
		case NETDEV_STAT_RECVDATA:
			return ICONS_RECVDATA;
			break;
	}
}

static void
create_systray(netstat *ns, NETDEVLIST_PTR netdev_list)
{
	NETDEVLIST_PTR ptr;
	char *tooltip;

	if (netdev_list==NULL) {
		return;
	}

	ptr = netdev_list;
	do {
		netdev_info *ni;
		ni = malloc(sizeof(netdev_info));
		ni->ns = ns;
		ni->netdev_list = ptr;

		if (!ptr->info.plug)
			tooltip = g_strdup_printf("%s\n  %s", ptr->info.ifname, N_("Network cable is plugged out"));
		else if (!ptr->info.connected)
			tooltip = g_strdup_printf("%s\n  %s", ptr->info.ifname, N_("Connection has limited or no connectivity"));
		else if (ptr->info.flags & IFF_POINTOPOINT)
			tooltip = g_strdup_printf("%s\n  %s\t%s\n  %s\t%s\n  %s\t%s", ptr->info.ifname,
															N_("IP Address: "), ptr->info.ipaddr,
															N_("Remote IP: "), ptr->info.dest,
															N_("Netmask: "), ptr->info.mask);
		else if (ptr->info.wireless)
			tooltip = g_strdup_printf("%s(%s) - %s(%d%%) \n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s",
															ptr->info.ifname, N_("Wireless"),
															ptr->info.essid, ptr->info.quality,
															N_("Protocol: "), ptr->info.protocol,
															N_("IP Address: "), ptr->info.ipaddr,
															N_("Boradcast: "), ptr->info.bcast,
															N_("Netmask: "), ptr->info.mask,
															N_("HW Address: "), ptr->info.mac);
		else
			tooltip = g_strdup_printf("%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s", ptr->info.ifname,
															N_("IP Address: "), ptr->info.ipaddr,
															N_("Boradcast: "), ptr->info.bcast,
															N_("Netmask: "), ptr->info.mask,
															N_("HW Address: "), ptr->info.mac);

		ptr->info.status_icon = create_statusicon(ns->mainw, select_icon(ptr->info.plug, ptr->info.connected, ptr->info.status), tooltip);
		g_signal_connect(ptr->info.status_icon->main, "button_press_event", G_CALLBACK(menupopup), ni);
		g_free(tooltip);
		ptr = ptr->next;
	} while(ptr!=NULL);
}

static void
refresh_systray(netstat *ns, NETDEVLIST_PTR netdev_list)
{
	NETDEVLIST_PTR ptr;
	char *tooltip;

	if (netdev_list==NULL) {
		return;
	}

	ptr = netdev_list;
	do {
		if (!ptr->info.enable) {
			set_statusicon_visible(ptr->info.status_icon, FALSE);
		} else if (ptr->info.updated) {
			if (!ptr->info.plug)
				tooltip = g_strdup_printf("%s\n  %s", ptr->info.ifname, N_("Network cable is plugged out"));
			else if (!ptr->info.connected)
				tooltip = g_strdup_printf("%s\n  %s", ptr->info.ifname, N_("Connection has limited or no connectivity"));
			else if (ptr->info.flags & IFF_POINTOPOINT)
				tooltip = g_strdup_printf("%s\n  %s\t%s\n  %s\t%s\n  %s\t%s", ptr->info.ifname,
																N_("IP Address: "), ptr->info.ipaddr,
																N_("Remote IP: "), ptr->info.dest,
																N_("Netmask: "), ptr->info.mask);
			else if (ptr->info.wireless)
				tooltip = g_strdup_printf("%s(%s) - %s(%d%%) \n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s",
																ptr->info.ifname, N_("Wireless"),
																ptr->info.essid, ptr->info.quality,
																N_("Protocol: "), ptr->info.protocol,
																N_("IP Address: "), ptr->info.ipaddr,
																N_("Boradcast: "), ptr->info.bcast,
																N_("Netmask: "), ptr->info.mask,
																N_("HW Address: "), ptr->info.mac);
			else
				tooltip = g_strdup_printf("%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s", ptr->info.ifname,
																N_("IP Address: "), ptr->info.ipaddr,
																N_("Boradcast: "), ptr->info.bcast,
																N_("Netmask: "), ptr->info.mask,
																N_("HW Address: "), ptr->info.mac);

			set_statusicon_tooltips(ptr->info.status_icon, tooltip);
			set_statusicon_image_from_file(ptr->info.status_icon, select_icon(ptr->info.plug, ptr->info.connected, ptr->info.status));
			set_statusicon_visible(ptr->info.status_icon, TRUE);
			g_free(tooltip);
		}
		ptr = ptr->next;
	} while(ptr!=NULL);
}

static
gboolean refresh_devstat(netstat *ns)
{
	netproc_listener(ns->fnetd);
	//netproc_print(fnetd->netdevlist);
	refresh_systray(ns, ns->fnetd->netdevlist);
	netproc_devicelist_clear(&ns->fnetd->netdevlist);
	return TRUE;
}

/* Plugin constructor */
static void
netstat_destructor(plugin *p)
{
	netstat *ns = (netstat *) p->priv;

	ENTER;
	g_source_remove(ns->ttag);
	netproc_netdevlist_clear(&ns->fnetd->netdevlist);
	gtk_widget_destroy(ns->mainw);
	close(ns->fnetd->sockfd);
	close(ns->fnetd->iwsockfd);
	g_free(ns->fnetd);
	g_free(ns->fixcmd);
	g_free(ns);
	RET();
}

static int
netstat_constructor(plugin *p, char **fp)
{
	netstat *ns;
	line s;

	ENTER;
	s.len = 256;  
	ns = g_new0(netstat, 1);
	g_return_val_if_fail(ns != NULL, 0);
	p->priv = ns;

	if( fp ) {
		while (lxpanel_get_line(fp, &s) != LINE_BLOCK_END) {
			if (s.type == LINE_NONE) {
				ERR( "netstat: illegal token %s\n", s.str);
				goto error;
			}
			if (s.type == LINE_VAR) {
				if (!g_ascii_strcasecmp(s.t[0], "FixCommand"))
				    ns->fixcmd = g_strdup(s.t[1]);
				else {
				    ERR( "netstat: unknown var %s\n", s.t[0]);
				    goto error;
				}
			} else {
				ERR( "netstat: illegal in this context %s\n", s.str);
				goto error;
			}
		}
	}

	/* initializing */
	ns->fnetd = malloc(sizeof(FNETD));
	ns->fnetd->netdevlist = NULL;
	ns->fnetd->sockfd = socket(AF_INET, SOCK_DGRAM, 0);
	ns->fnetd->iwsockfd = iw_sockets_open();

	/* main */
	ns->mainw = p->panel->my_box_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(p->pwid), ns->mainw);
	gtk_widget_show_all(ns->mainw);

	/* Initializing network device list*/
	ns->fnetd->netdev_fp = netproc_open();
	ns->fnetd->dev_count = netproc_netdevlist_clear(&ns->fnetd->netdevlist);
	ns->fnetd->dev_count = netproc_scandevice(ns->fnetd->sockfd, ns->fnetd->iwsockfd, ns->fnetd->netdev_fp, &ns->fnetd->netdevlist);
	netproc_close(ns->fnetd->netdev_fp);
	create_systray(ns, ns->fnetd->netdevlist);

	ns->ttag = g_timeout_add(NETSTAT_IFACE_POLL_DELAY, (GSourceFunc)refresh_devstat, ns);

	RET(1);
error:
	g_free(ns->fnetd);
	g_free(ns->fixcmd);
	g_free(ns);
	RET(0);
}

plugin_class netstat_plugin_class = {
	fname: NULL,
	count: 0,

	type : "netstat",
	name : N_("Net Status Monitor"),
	version: "1.0",
	description : N_("Monitor network status"),

	constructor : netstat_constructor,
	destructor  : netstat_destructor,
	config : NULL,
	save : NULL
};
