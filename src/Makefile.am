## Process this file with automake to produce Makefile.in

SUBDIRS = plugins

bin_PROGRAMS = lxpanel lxpanelctl

INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src/plugins \
	-I$(top_srcdir)/src/ptk-ui-xml \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

BUILTIN_PLUGINS = $(top_builddir)/src/plugins/libbuiltin_plugins.a

PTK_UI_XML_SOURCES=\
	ptk-ui-xml/ptk-ui-xml.c \
        ptk-ui-xml/ptk-ui-xml.h \
	ptk-ui-xml/ptk-xml-tree.c \
        ptk-ui-xml/ptk-xml-tree.h

TRAY_SOURCES= \
	systray/eggmarshalers.c systray/eggmarshalers.h \
	systray/eggtraymanager.c systray/eggtraymanager.h \
	systray/fixedtip.c systray/fixedtip.h \
	systray/tray.c

lxpanel_SOURCES = \
	$(PTK_UI_XML_SOURCES) \
	glib-mem.h \
	misc.c misc.h \
	$(TRAY_SOURCES) \
	bg.c bg.h  \
	configurator.c \
	dbg.c dbg.h \
	ev.c ev.h \
	gtkbar.h gtkbar.c \
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
