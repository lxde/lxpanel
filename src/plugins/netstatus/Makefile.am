netstatus_la_CFLAGS = \
	-I. \
	-I$(top_srcdir)/src \
	-DPACKAGE_DATA_DIR=\""$(datadir)"\" \
	-DPACKAGE_UI_DIR=\""$(datadir)/lxpanel/ui"\" \
	$(PACKAGE_CFLAGS) \
	$(G_CAST_CHECKS)

module_LTLIBRARIES = netstatus.la

moduledir = $(libdir)/lxpanel/plugins

netstatus_la_SOURCES = \
	netstatus.c \
	netstatus-dialog.c \
	netstatus-dialog.h \
	netstatus-enums.c \
	netstatus-enums.h \
	netstatus-fallback-pixbuf.h \
	netstatus-icon.c \
	netstatus-icon.h \
	netstatus-iface.c \
	netstatus-iface.h \
	netstatus-sysdeps.c \
	netstatus-sysdeps.h \
	netstatus-util.c \
	netstatus-util.h

netstatus_la_LIBADD = \
	$(PACKAGE_LIBS)

netstatus_la_LDFLAGS = \
	-module \
	$(LXPANEL_MODULE)

install-exec-hook:
	rm -f "$(moduledir)\*.la" *.a
