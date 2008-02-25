INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = cpu.la

moduledir = $(libdir)/lxpanel/plugins

cpu_la_SOURCES = \
	cpu.c

cpu_la_LIBADD = \
	$(PACKAGE_LIBS)

cpu_la_LDFLAGS = \
	-avoid-version \
	-rpath $(moduledir) \
	-module \
	-export-symbols-regex '_plugin_class$$'
