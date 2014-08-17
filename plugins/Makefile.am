## Process this file with automake to produce Makefile.in

DIST_SUBDIRS=$(ALL_PLUGINS_LIST)

if BUILD_PLUGIN_LOADER
DYNAMIC_PLUGINS = \
	$(PLUGINS_LIST)
endif

SUBDIRS = $(DYNAMIC_PLUGINS)

noinst_LIBRARIES = libbuiltin_plugins.a

libbuiltin_plugins_a_CFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-DPACKAGE_DATA_DIR=\""$(datadir)/lxpanel"\" \
	-DPACKAGE_UI_DIR=\""$(datadir)/lxpanel/ui"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

if ENABLE_MENU_CACHE
MENU_SOURCES = \
	menu.c
endif

PLUGINS_SOURCES = \
	dclock.c \
	dirmenu.c \
	launchtaskbar.c \
	pager.c \
	separator.c \
	space.c \
	tray.c \
	wincmd.c \
	$(MENU_SOURCES)

libbuiltin_plugins_a_SOURCES = \
	$(PLUGINS_SOURCES)

libbuiltin_plugins_a_LIBADD =

EXTRA_DIST = \
	icon.xpm

install-exec-hook:
	rm -f $(DESTDIR)$(libdir)/lxpanel/plugins/*.la
	rm -f $(DESTDIR)$(libdir)/lxpanel/plugins/*.a
	rm -f $(DESTDIR)$(libdir)/lxpanel/plugins/*.
