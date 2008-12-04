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

#if HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <pthread.h>
#include <iwlib.h>
#include "nsconfig.h"
#include "netstat.h"
#include "lxnm_client.h"
#include "statusicon.h"
#include "passwd_gui.h"
#include "devproc.h"
#include "wireless.h"
#include "panel.h"
#include "misc.h"
#include "plugin.h"
#include "dbg.h"

/* 1 second */
#define NETSTAT_IFACE_POLL_DELAY 3000

static int actionProcess(void *arg)
{
    ENTER;
    RET(system((char *)arg));
}

/* menu handlers */
static void ethernet_repair(GtkWidget *widget, netdev_info *ni)
{
    if (ni->ns->fixcmd) {
        pthread_t actionThread;
        char *fixcmd;

        fixcmd = g_strdup_printf(ni->ns->fixcmd, ni->netdev_list->info.ifname);

        pthread_create(&actionThread, NULL, actionProcess, fixcmd);
    } else {
        lxnm_send_command(ni->ns->fnetd->lxnmchannel, LXNM_ETHERNET_REPAIR, ni->netdev_list->info.ifname);
    }
}

static void ethernet_down(GtkWidget *widget, netdev_info *ni)
{
    lxnm_send_command(ni->ns->fnetd->lxnmchannel, LXNM_ETHERNET_DOWN, ni->netdev_list->info.ifname);
}

static void wireless_connect(GtkWidget *widget, ap_setting *aps)
{
    char *cmdargs;

    /* without encryption */
    if (!aps->apinfo->haskey) {
        cmdargs = lxnm_wireless_command_make(aps->ifname, aps->apinfo->essid, aps->apinfo->apaddr, "",
                                   aps->apinfo->en_method, aps->apinfo->key_mgmt,
                                   aps->apinfo->group, aps->apinfo->pairwise);
        lxnm_send_command(aps->gio, LXNM_WIRELESS_CONNECT, cmdargs);
        g_free(cmdargs);
    } else {
        /* with encryption */
        if (aps->ni->netdev_list->info.pg!=NULL)
            passwd_gui_destroy(aps->ni->netdev_list->info.pg);

        /* record information for password dialog */
        ap_setting *aps_new;
        ap_info *apinfo;
	apinfo = malloc(sizeof(ap_info));
	if (aps->apinfo->essid==NULL)
		apinfo->essid = NULL;
	else
		apinfo->essid = g_strdup(aps->apinfo->essid);

	apinfo->apaddr = g_strdup(aps->apinfo->apaddr);
	apinfo->quality = aps->apinfo->quality;
	apinfo->en_method = aps->apinfo->en_method;
	apinfo->pairwise = aps->apinfo->pairwise;
	apinfo->group = aps->apinfo->group;
	apinfo->key_mgmt = aps->apinfo->key_mgmt;
	apinfo->haskey = aps->apinfo->haskey;

        aps_new = g_new0(ap_setting, 1);
        aps_new->ni = aps->ni;
        aps_new->gio = aps->gio;
        aps_new->ifname = g_strdup(aps->ifname);
        aps_new->apinfo = apinfo;
        /* create dialog window for typing password */
        aps->ni->netdev_list->info.pg = passwd_gui_new(aps_new);
        //passwd_gui_set_style(aps->ni->netdev_list->info.pg, gtk_style_new());
    }
}

static GtkWidget *
wireless_menu(netdev_info *ni)
{
    APLIST *aplist;
    APLIST *ptr;
    GtkWidget *menu;
    GtkWidget *menu_item;
    GtkWidget *wireless_label;

    /* AP status widget */
    GtkWidget *item_box;
    GtkWidget *essid_label;
    GtkWidget *lockicon;
    GtkWidget *signal_quality;
    gdouble quality_per;
    ap_setting *aps;

    /* create menu */
    menu = gtk_menu_new();
	g_signal_connect(menu, "selection-done", gtk_widget_destroy, NULL);

    /* Scanning AP */
    aplist = wireless_scanning(ni->ns->fnetd->iwsockfd, ni->netdev_list->info.ifname);
    if (aplist!=NULL) {
        /* release AP list after menu */
        g_object_weak_ref(menu, wireless_aplist_free, aplist);
        ptr = aplist;
        do {
            /* skip hidden AP with Encryption */
            if (ptr->info->haskey&&ptr->info->essid==NULL) {
                /* handle next AP */
                ptr = ptr->next;
				continue;
            }

            aps = g_new0(ap_setting, 1);
            aps->ni = ni;
            aps->gio = ni->ns->fnetd->lxnmchannel;
            aps->ifname = ni->netdev_list->info.ifname;
			aps->apinfo = ptr->info;

            /* create a new item */
            menu_item = gtk_menu_item_new();
            item_box = gtk_hbox_new(FALSE, 0);

            /* Encryption */
            if (aps->apinfo->haskey) {
                lockicon = gtk_image_new_from_file(ICONS_WL_LOCK);
                gtk_box_pack_start(GTK_BOX(item_box), lockicon, FALSE, FALSE, 0);
            }

            /* ESSID */
            if (aps->apinfo->essid==NULL)
                essid_label = gtk_label_new(_("<Hidden Access Point>"));
            else
                essid_label = gtk_label_new(aps->apinfo->essid);

            gtk_label_set_justify(essid_label, GTK_JUSTIFY_LEFT);
            gtk_misc_set_padding(GTK_MISC(essid_label), 2, 0);
            gtk_box_pack_start(GTK_BOX(item_box), essid_label, TRUE, FALSE, 0);

            /* Quality */
            quality_per = (gdouble)((double)aps->apinfo->quality/100);
            if (quality_per>1)
                quality_per = 1;
            else if (quality_per<0)
                quality_per = 0;

            signal_quality = gtk_progress_bar_new();
            gtk_widget_set_size_request(signal_quality, 100, -1);
            gtk_progress_bar_set_orientation(signal_quality, GTK_PROGRESS_LEFT_TO_RIGHT);
            gtk_progress_bar_set_fraction(signal_quality, quality_per);
            gtk_box_pack_start(GTK_BOX(item_box), signal_quality, FALSE, FALSE, 0);

            /* add this item to menu */
            gtk_container_add(GTK_CONTAINER(menu_item), item_box);
            gtk_menu_append(GTK_MENU(menu), menu_item);
            g_signal_connect(G_OBJECT(menu_item), "activate", G_CALLBACK(wireless_connect), aps);
            g_object_weak_ref(menu_item, g_free, aps);

			/* handle next AP */
            ptr = ptr->next;
        } while(ptr!=NULL);
    } else {
        /* we do not found any wireless networks */
        menu_item = gtk_menu_item_new();
        wireless_label = gtk_label_new(_("Wireless Networks not found in range"));
        gtk_label_set_justify(wireless_label, GTK_JUSTIFY_LEFT);
        gtk_widget_set_sensitive(GTK_WIDGET(wireless_label), FALSE);
        gtk_container_add(GTK_CONTAINER(menu_item), wireless_label);
        gtk_menu_append(GTK_MENU(menu), menu_item);
    }

    gtk_widget_show_all(menu);

    return menu;
}

static gint menupopup(GtkWidget *widget, GdkEvent *event, netdev_info *ni)
{
    GdkEventButton *event_button;

    g_return_val_if_fail (event != NULL, FALSE);

    if (event->type == GDK_BUTTON_PRESS) {
        event_button = (GdkEventButton *) event;
        if (event_button->button == 1) {
            /* wireless device */
            if (ni->netdev_list->info.wireless) {
                gtk_menu_popup(wireless_menu(ni), NULL, NULL, NULL, NULL, event_button->button, event_button->time);
            }
            return TRUE;
        } else if (event_button->button == 3) {
            GtkWidget *menu;
            GtkWidget *menu_item;

            /* create menu */
            menu = gtk_menu_new();

            /* Repair */
            menu_item = gtk_menu_item_new_with_label(_("Repair"));
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
            g_signal_connect(G_OBJECT(menu_item), "activate", G_CALLBACK(ethernet_repair), ni);

            /* interface down */
            menu_item = gtk_menu_item_new_with_label(_("Disable"));
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), menu_item);
            g_signal_connect(G_OBJECT(menu_item), "activate", G_CALLBACK(ethernet_down), ni);

            gtk_widget_show_all(menu);

            gtk_menu_popup(menu, NULL, NULL, NULL, NULL, event_button->button, event_button->time);
            return TRUE;
        }
    }

    return FALSE;
}

static char *select_icon(gboolean plug, gboolean connected, int stat)
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

static void refresh_systray(netstat *ns, NETDEVLIST_PTR netdev_list)
{
    NETDEVLIST_PTR ptr;
    char *tooltip;

    if (netdev_list==NULL) {
        return;
    }

    ptr = netdev_list;
    do {
        if (!ptr->info.enable) {
            if (ptr->info.status_icon!=NULL) {
                set_statusicon_visible(ptr->info.status_icon, FALSE);
            }
        } else if (ptr->info.updated) {
            if (!ptr->info.plug) {
                if (ptr->info.wireless)
                    tooltip = g_strdup_printf("%s\n  %s", ptr->info.ifname, _("Wireless Connection has no connectivity"));
				else
                    tooltip = g_strdup_printf("%s\n  %s", ptr->info.ifname, _("Network cable is plugged out"));
            } else if (!ptr->info.connected)
                tooltip = g_strdup_printf("%s\n  %s", ptr->info.ifname, _("Connection has limited or no connectivity"));
            else if (ptr->info.flags & IFF_POINTOPOINT)
                tooltip = g_strdup_printf("%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n\n %s(%s/%s)\n   %lu/%lu %s\n   %lu/%lu %s",
                                                                 ptr->info.ifname,
                                                                _("IP Address:"), ptr->info.ipaddr,
                                                                _("Remote IP:"), ptr->info.dest,
                                                                _("Netmask:"), ptr->info.mask,
                                                                _("Activity"), _("Sent"),_("Received"),
                                                                ptr->info.trans_bytes, ptr->info.recv_bytes, _("bytes"),
                                                                ptr->info.trans_packets, ptr->info.recv_packets, _("packets"));
            else if (ptr->info.wireless)
                tooltip = g_strdup_printf("%s(%s) - %s(%d%%)\n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n\n %s(%s/%s)\n   %lu/%lu %s\n   %lu/%lu %s",
                                                                ptr->info.ifname, _("Wireless"),
                                                                ptr->info.essid, ptr->info.quality,
                                                                _("Protocol:"), ptr->info.protocol,
                                                                _("IP Address:"), ptr->info.ipaddr,
                                                                _("Broadcast:"), ptr->info.bcast,
                                                                _("Netmask:"), ptr->info.mask,
                                                                _("HW Address:"), ptr->info.mac,
                                                                _("Activity"), _("Sent"), _("Received"),
                                                                ptr->info.trans_bytes, ptr->info.recv_bytes, _("bytes"),
                                                                ptr->info.trans_packets, ptr->info.recv_packets, _("packets"));

            else
                tooltip = g_strdup_printf("%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n  %s\t%s\n\n %s(%s/%s)\n   %lu/%lu %s\n   %lu/%lu %s",
                                                                ptr->info.ifname,
                                                                _("IP Address:"), ptr->info.ipaddr,
                                                                _("Broadcast:"), ptr->info.bcast,
                                                                _("Netmask:"), ptr->info.mask,
                                                                _("HW Address:"), ptr->info.mac,
                                                                _("Activity"), _("Sent"), _("Received"),
                                                                ptr->info.trans_bytes, ptr->info.recv_bytes, _("bytes"),
                                                                ptr->info.trans_packets, ptr->info.recv_packets, _("packets"));

            /* status icon doesn't exist  */
            if (ptr->info.status_icon==NULL) {
                netdev_info *ni;
                ni = malloc(sizeof(netdev_info));
                ni->ns = ns;
                ni->netdev_list = ptr;

                ptr->info.status_icon = create_statusicon(ns->mainw, select_icon(ptr->info.plug, ptr->info.connected, ptr->info.status), tooltip);
                g_signal_connect(ptr->info.status_icon->main, "button_press_event", G_CALLBACK(menupopup), ni);
                g_object_weak_ref(ptr->info.status_icon->main, g_free, ni);
            } else {
                set_statusicon_tooltips(ptr->info.status_icon, tooltip);
                set_statusicon_image_from_file(ptr->info.status_icon, select_icon(ptr->info.plug, ptr->info.connected, ptr->info.status));
                set_statusicon_visible(ptr->info.status_icon, TRUE);
            }
            g_free(tooltip);
        }
        ptr = ptr->next;
    } while(ptr!=NULL);
}

static gboolean refresh_devstat(netstat *ns)
{
    netproc_listener(ns->fnetd);
#ifdef DEBUG
    netproc_print(ns->fnetd->netdevlist);
#endif
    refresh_systray(ns, ns->fnetd->netdevlist);
    netproc_devicelist_clear(&ns->fnetd->netdevlist);
    return TRUE;
}

/* Plugin constructor */
static void netstat_destructor(Plugin *p)
{
    netstat *ns = (netstat *) p->priv;

    ENTER;
    g_source_remove(ns->ttag);
    netproc_netdevlist_clear(&ns->fnetd->netdevlist);
    /* The widget is destroyed in plugin_stop().
    gtk_widget_destroy(ns->mainw);
    */
    lxnm_close(ns->fnetd->lxnmchannel);
    close(ns->fnetd->sockfd);
    close(ns->fnetd->iwsockfd);
    g_free(ns->fnetd);
    g_free(ns->fixcmd);
    g_free(ns);
    RET();
}

static int netstat_constructor(Plugin *p, char **fp)
{
    netstat *ns;
    line s;

    ENTER;
    s.len = 256;
    ns = g_new0(netstat, 1);
    g_return_val_if_fail(ns != NULL, 0);
    p->priv = ns;
	ns->fixcmd = NULL;

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
    ns->fnetd->lxnmchannel = lxnm_socket();

    /* main */
    ns->mainw = p->panel->my_box_new(FALSE, 1);
    gtk_widget_show_all(ns->mainw);

    /* Initializing network device list*/
    ns->fnetd->netdev_fp = netproc_open();
    ns->fnetd->dev_count = netproc_netdevlist_clear(&ns->fnetd->netdevlist);
    ns->fnetd->dev_count = netproc_scandevice(ns->fnetd->sockfd, ns->fnetd->iwsockfd, ns->fnetd->netdev_fp, &ns->fnetd->netdevlist);
    netproc_close(ns->fnetd->netdev_fp);
    refresh_systray(ns, ns->fnetd->netdevlist);

    ns->ttag = g_timeout_add(NETSTAT_IFACE_POLL_DELAY, (GSourceFunc)refresh_devstat, ns);

    p->pwid = gtk_event_box_new();
    GTK_WIDGET_SET_FLAGS(p->pwid, GTK_NO_WINDOW);
    gtk_container_add((GtkContainer*)p->pwid, ns->mainw);

    RET(1);
error:
    g_free(ns->fnetd);
    g_free(ns->fixcmd);
    g_free(ns);
    RET(0);
}

static void orientation_changed(Plugin* p)
{
    netstat *ns = (netstat *)p->priv;
    GtkBox* newbox;
    newbox = GTK_BOX(recreate_box(GTK_BOX(ns->mainw), p->panel->orientation));
    if( GTK_WIDGET(newbox) != ns->mainw ) {
        /* Since the old box has been destroyed,
        we need to re-add the new box to the container */
        ns->mainw = GTK_WIDGET(newbox);
        gtk_container_add(GTK_CONTAINER(p->pwid), ns->mainw);
    }
}

PluginClass netstat_plugin_class = {
    fname: NULL,
    count: 0,

    type : "netstat",
    name : N_("Manage Networks"),
    version: "1.0",
    description : N_("Monitor and Manage networks"),

    constructor : netstat_constructor,
    destructor  : netstat_destructor,
    orientation : orientation_changed,
    config : NULL,
    save : NULL
};
