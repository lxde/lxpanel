wnckpager_la_CFLAGS = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(WNCK_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = wnckpager.la

moduledir = $(libdir)/lxpanel/plugins

wnckpager_la_SOURCES = \
	wnckpager.c

wnckpager_la_LIBADD = \
	$(PACKAGE_LIBS) \
	$(WNCK_LIBS)

wnckpager_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@
