## Process this file with automake to produce Makefile.in

bin_PROGRAMS = lxpanel lxpanelctl

INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/plugins \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS)

PLUGINS_SOURCES= \
	plugins/cpu.c \
	plugins/deskno.c \
	plugins/launchbar.c \
	plugins/ptk-app-menu.c \
	plugins/taskbar.c \
	plugins/dclock.c \
	plugins/icons.c \
	plugins/menu.c \
	plugins/separator.c \
	plugins/deskno2.c \
	plugins/image.c \
	plugins/pager.c \
	plugins/space.c \
	plugins/wincmd.c

TRAY_SOURCES= \
	systray/eggmarshalers.c systray/eggmarshalers.h \
	systray/eggtraymanager.c systray/eggtraymanager.h \
	systray/fixedtip.c systray/fixedtip.h\
	systray/tray.c

lxpanel_SOURCES = \
	misc.c misc.h \
	$(PLUGINS_SOURCES) \
	$(TRAY_SOURCES) \
	bg.c bg.h  \
	configurator.c \
	dbg.c dbg.h \
	ev.c ev.h \
	gtkbar.h gtkbar.c \
	gtkbgbox.h gtkbgbox.c \
	panel.c panel.h \
	plugin.c plugin.h \
	gtk-run.c

lxpanel_LDADD = $(PACKAGE_LIBS) $(INTLLIBS)

lxpanelctl_SOURCES = lxpanelctl.c lxpanelctl.h
lxpanelctl_LDADD = -lX11 $(INTLLIBS)

EXTRA_DIST = \
	plugins/icon.xpm
