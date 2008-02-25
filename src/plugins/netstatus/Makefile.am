INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = netstatus.la

moduledir = $(libdir)/lxpanel/plugins

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
	$(PACKAGE_LIBS)

netstatus_la_LDFLAGS = \
	-module \
	$(LXPANEL_MODULE)

install-exec-hook:
	rm -f "$(moduledir)\*.la" *.a
