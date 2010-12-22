## Process this file with automake to produce Makefile.in

SUBDIRS = plugins

bin_PROGRAMS = lxpanel lxpanelctl

lxpanel_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src/plugins \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_UI_DIR=\""$(datadir)/lxpanel/ui"\" \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

BUILTIN_PLUGINS = $(top_builddir)/src/plugins/libbuiltin_plugins.a

if ENABLE_MENU_CACHE
MENU_SOURCES = gtk-run.c
endif

lxpanel_SOURCES = \
	misc.c misc.h \
	bg.c bg.h  \
	configurator.c \
	dbg.c dbg.h \
	ev.c ev.h \
	gtkbar.h gtkbar.c \
	icon-grid.h icon-grid.c \
	menu-policy.h menu-policy.c \
	panel.c panel.h \
	plugin.c plugin.h \
	$(MENU_SOURCES)

if BUILD_PLUGIN_LOADER
DYNAMIC_FLAGS = -export-dynamic
lxpanel_includedir = $(includedir)/lxpanel
lxpanel_include_HEADERS = $(top_srcdir)/include/lxpanel/plugin.h
endif
lxpanel_LDFLAGS = $(DYNAMIC_FLAGS)

lxpanel_LDADD = \
		$(BUILTIN_PLUGINS) \
		$(PACKAGE_LIBS) \
		$(X11_LIBS) \
		$(INTLLIBS)

lxpanelctl_SOURCES = lxpanelctl.c lxpanelctl.h
lxpanelctl_LDADD = $(X11_LIBS) $(INTLLIBS)

noinst_PROGRAMS=xml-purge
xml_purge_SOURCES=xml-purge.c
xml_purge_CFLAGS=@PACKAGE_CFLAGS@
xml_purge_LDADD=@PACKAGE_LIBS@


EXTRA_DIST = \
	plugins/icon.xpm
