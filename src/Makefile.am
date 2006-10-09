## Process this file with automake to produce Makefile.in

SUBDIRS = plugins

bin_PROGRAMS = lxpanel lxpanelctl

INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/plugins \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS)

BUILTIN_PLUGINS = $(top_builddir)/src/plugins/libbuiltin_plugins.a

TRAY_SOURCES= \
	systray/eggmarshalers.c systray/eggmarshalers.h \
	systray/eggtraymanager.c systray/eggtraymanager.h \
	systray/fixedtip.c systray/fixedtip.h \
	systray/tray.c

lxpanel_SOURCES = \
	misc.c misc.h \
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

if BUILD_PLUGIN_LOADER
DYNAMIC_FLAGS = -export-dynamic
endif
lxpanel_LDFLAGS = $(DYNAMIC_FLAGS)

lxpanel_LDADD = \
		$(BUILTIN_PLUGINS) \
		$(PACKAGE_LIBS) \
		$(INTLLIBS)

lxpanelctl_SOURCES = lxpanelctl.c lxpanelctl.h
lxpanelctl_LDADD = -lX11 $(INTLLIBS)

EXTRA_DIST = \
	plugins/icon.xpm
