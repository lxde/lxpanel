INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = deskno.la

moduledir = $(libdir)/lxpanel/plugins

deskno_la_SOURCES = \
	deskno.c

deskno_la_LIBADD = \
	$(PACKAGE_LIBS)

deskno_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@
