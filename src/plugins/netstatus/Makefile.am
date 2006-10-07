if BUILD_NETSTATS

INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(GCONF_CFLAGS)

module_LTLIBRARIES = netstatus.la

moduledir = ${prefix}/share/lxpanel/plugins

netstatus_la_SOURCES = \
	netstatus-dialog.h \
	netstatus-fallback-pixbuf.h \
	netstatus-sysdeps.c \
	glade-support.c \
	netstatus-dialog-ui.c \
	netstatus-icon.c \
	netstatus-sysdeps.h \
	glade-support.h \
	netstatus-dialog-ui.h \
	netstatus-icon.h \
	netstatus-util.c \
	netstatus.c \
	netstatus-enums.c \
	netstatus-iface.c \
	netstatus-util.h \
	netstatus-dialog.c \
	netstatus-enums.h \
	netstatus-iface.h

netstatus_la_LIBADD = \
	$(PACKAGE_CFLAGS) \
	$(GCONF_LIBS)
netstatus_la_LDFLAGS = \
	-avoid-version \
	-rpath $(moduledir) \
	-module

endif
