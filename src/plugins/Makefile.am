## Process this file with automake to produce Makefile.in

if BUILD_PLUGIN_LOADER
DYNAMIC_PLUGINS = \
	$(PLUGINS_LIST)
endif

SUBDIRS = $(DYNAMIC_PLUGINS)

noinst_LIBRARIES = libbuiltin_plugins.a

INCLUDES = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/plugins \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\" \
	$(PACKAGE_CFLAGS)

PLUGINS_SOURCES= \
	deskno.c \
	launchbar.c \
	ptk-app-menu.c \
	taskbar.c \
	dclock.c \
	icons.c \
	menu.c \
	separator.c \
	deskno2.c \
	image.c \
	pager.c \
	space.c \
	wincmd.c

libbuiltin_plugins_a_SOURCES = \
	$(PLUGINS_SOURCES)

libbuiltin_plugins_a_LIBADD =

EXTRA_DIST = \
	icon.xpm
