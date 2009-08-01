INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_UI_DIR=\""$(datadir)/lxpanel/ui"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = netstatus.la

moduledir = $(libdir)/lxpanel/plugins

netstatus_la_SOURCES = \
	netstatus-dialog.h \
	netstatus-fallback-pixbuf.h \
	netstatus-sysdeps.c \
	netstatus-icon.c \
	netstatus-sysdeps.h \
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
