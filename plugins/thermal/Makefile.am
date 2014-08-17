thermal_la_CFLAGS = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = thermal.la

moduledir = $(libdir)/lxpanel/plugins

thermal_la_SOURCES = \
	thermal.c

thermal_la_LIBADD = \
	$(PACKAGE_LIBS)

thermal_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@
