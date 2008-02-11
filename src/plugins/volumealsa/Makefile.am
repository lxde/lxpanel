if BUILD_ALSA_PLUGINS

INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = volumealsa.la

moduledir = $(libdir)/lxpanel/plugins

volumealsa_la_SOURCES = volumealsa.c

volumealsa_la_LIBADD = -lasound $(PACKAGE_CFLAGS)

volumealsa_la_LDFLAGS = \
	-avoid-version \
	-rpath $(moduledir) \
	-module \
	-export-symbols-regex '_plugin_class$$'

endif
