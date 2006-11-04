INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS)

module_LTLIBRARIES = volume.la

moduledir = $(libdir)/lxpanel/plugins

volume_la_SOURCES = \
	volume_xpm.h \
	volume-impl.h \
	volume-impl.c \
	volume.c

volume_la_LIBADD = \
	$(PACKAGE_CFLAGS)

volume_la_LDFLAGS = \
	-avoid-version \
	-rpath $(moduledir) \
	-module \
	-export-symbols-regex '_plugin_class$$'
