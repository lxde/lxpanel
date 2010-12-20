if BUILD_OSS_PLUGINS

volume_la_CFLAGS = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = volume.la

moduledir = $(libdir)/lxpanel/plugins

volume_la_SOURCES = \
	volume_xpm.h \
	volume-impl.h \
	volume-impl.c \
	volume.c

volume_la_LIBADD = \
	$(PACKAGE_LIBS)

volume_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@

endif
