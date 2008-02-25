INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = batt.la

moduledir = $(libdir)/lxpanel/plugins

batt_la_SOURCES = \
	batt.c

batt_la_LIBADD = \
	$(PACKAGE_LIBS)

batt_la_LDFLAGS = \
	-avoid-version \
	-rpath $(moduledir) \
	-module \
	-export-symbols-regex '_plugin_class$$'
