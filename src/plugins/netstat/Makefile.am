INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = netstat.la

moduledir = $(libdir)/lxpanel/plugins

netstat_la_SOURCES = \
        netstat.c \
        fnetdaemon.h \
        nsconfig.h \
        devproc.c \
        devproc.h

netstat_la_LIBADD = $(PACKAGE_LIBS)

netstat_la_LDFLAGS = \
	-avoid-version \
	-rpath $(moduledir) \
	-module \
	-export-symbols-regex '_plugin_class$$'

install-exec-hook:
	rm -f "$(moduledir)\*.la" *.a
