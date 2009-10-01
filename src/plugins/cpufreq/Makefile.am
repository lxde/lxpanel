INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = cpufreq.la

moduledir = $(libdir)/lxpanel/plugins

cpufreq_la_SOURCES = \
	cpufreq.c

cpufreq_la_LIBADD = \
	$(PACKAGE_LIBS)

cpufreq_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@
