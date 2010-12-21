monitors_la_CFLAGS = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = monitors.la

moduledir = $(libdir)/lxpanel/plugins

monitors_la_SOURCES = \
	monitors.c

monitors_la_LIBADD = \
	$(PACKAGE_LIBS)

monitors_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@
