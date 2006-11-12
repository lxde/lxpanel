INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS)

module_LTLIBRARIES = deskno.la

moduledir = $(libdir)/lxpanel/plugins

deskno_la_SOURCES = \
	deskno.c

deskno_la_LIBADD = \
	$(PACKAGE_CFLAGS)

deskno_la_LDFLAGS = \
	-avoid-version \
	-rpath $(moduledir) \
	-module \
	-export-symbols-regex '_plugin_class$$'
