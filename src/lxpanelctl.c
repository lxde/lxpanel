/**
 * Copyright (c) 2006 LxDE Developers, see the file AUTHORS for details.
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

/* Remote controller of lxpanel */

#include "lxpanelctl.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Display* dpy;

static const char usage[] =
        "\nlxpanelctl - LXPanel Controller\n"
        "Usage: lxpanelctl <command>\n\n"
        "Available commands:\n"
        "menu\tshow system menu\n"
        "run\tshow run dialog\n"
        "config\tshow config dialog\n"
        "restart\trestart lxpanel\n"
        "exit\texit lxpanel\n\n";

static int get_cmd( const char* cmd )
{
    if( ! strcmp( cmd, "menu") )
        return LXPANEL_CMD_SYS_MENU;
    else if( ! strcmp( cmd, "run") )
        return LXPANEL_CMD_RUN;
    else if( ! strcmp( cmd, "config") )
        return LXPANEL_CMD_CONFIG;
    else if( ! strcmp( cmd, "restart") )
        return LXPANEL_CMD_RESTART;
    else if( ! strcmp( cmd, "exit") )
        return LXPANEL_CMD_EXIT;
    return -1;
}

int main( int argc, char** argv )
{
    char *display_name = (char *)getenv("DISPLAY");
    XEvent ev;
    Window root;
    Atom cmd_atom;
    int cmd;
    /* int restart; */

    if( argc < 2 )
    {
        printf( usage );
        return 1;
    }

    /*
    if( restart = !strcmp( argv[1], "restart" ) )
        argv[1] = "exit";
    */

    if( ( cmd = get_cmd( argv[1] ) ) == -1 )
        return 1;

    dpy = XOpenDisplay(display_name);
    if (dpy == NULL) {
        printf("Cant connect to display: %s\n", display_name);
        exit(1);
    }
    root = DefaultRootWindow(dpy);
    cmd_atom = XInternAtom(dpy, "_LXPANEL_CMD", False);
    memset(&ev, '\0', sizeof ev);
    ev.xclient.type = ClientMessage;
    ev.xclient.window = root;
    ev.xclient.message_type = cmd_atom;
    ev.xclient.format = 8;

    ev.xclient.data.l[0] = cmd;

    XSendEvent(dpy, root, False,
               SubstructureRedirectMask|SubstructureNotifyMask, &ev);
    XSync(dpy, False);
    XCloseDisplay(dpy);

/*
    if( restart ) {
        system( PACKAGE_BIN_DIR "/lxpanel &" );
    }
*/
    return 0;
}

