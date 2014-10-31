## Process this file with automake to produce Makefile.in
ABI_VERSION=0:0:0

bin_PROGRAMS = lxpanel lxpanelctl

lxpanel_CFLAGS = \
	-I$(top_srcdir) \
	-DPACKAGE_LIB_DIR=\""$(libdir)"\" \
	-DPACKAGE_DATA_DIR=\""$(datadir)/lxpanel"\" \
	-DPACKAGE_UI_DIR=\""$(datadir)/lxpanel/ui"\" \
	-DPACKAGE_BIN_DIR=\""$(bindir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

BUILTIN_PLUGINS = $(top_builddir)/plugins/libbuiltin_plugins.a

if ENABLE_MENU_CACHE
MENU_SOURCES = menu-policy.c
endif

pkglib_LTLIBRARIES = liblxpanel.la

liblxpanel_la_CFLAGS = $(lxpanel_CFLAGS)
liblxpanel_la_SOURCES = \
	misc.c \
	bg.c \
	configurator.c \
	dbg.c \
	ev.c \
	icon-grid.c \
	panel.c \
	plugin.c \
	conf.c

liblxpanel_la_LIBADD = \
	$(PACKAGE_LIBS) \
	$(X11_LIBS)

lxpanel_includedir = $(includedir)/lxpanel
lxpanel_include_HEADERS = \
	plugin.h \
	panel.h \
	misc.h \
	icon-grid.h \
	conf.h

lxpanel_SOURCES = \
	icon-grid-old.c \
	gtk-run.c \
	main.c \
	$(MENU_SOURCES)

if BUILD_PLUGIN_LOADER
DYNAMIC_FLAGS = -export-dynamic
endif
lxpanel_LDFLAGS = $(DYNAMIC_FLAGS)
lxpanel_DEPENDENCIES_EXTRA = $(BUILTIN_PLUGINS)

lxpanel_LDADD = \
		liblxpanel.la \
		$(BUILTIN_PLUGINS) \
		$(PACKAGE_LIBS) \
		$(X11_LIBS)

lxpanelctl_SOURCES = lxpanelctl.c lxpanelctl.h
lxpanelctl_LDADD = $(X11_LIBS)

EXTRA_DIST = \
	bg.h \
	dbg.h \
	ev.h \
	menu-policy.h \
	icon-grid-old.h \
	private.h

$(top_builddir)/plugins/libbuiltin_plugins.a: $(lxpanel_include_HEADERS)
	cd $(top_builddir)/plugins && $(MAKE) libbuiltin_plugins.a
