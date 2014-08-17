netstat_la_CFLAGS = \
    -I. \
    -I$(top_srcdir)/src \
    -DPACKAGE_DATA_DIR=\""$(datadir)/lxpanel"\" \
    $(PACKAGE_CFLAGS) \
    $(G_CAST_CHECKS)

module_LTLIBRARIES = netstat.la

moduledir = $(libdir)/lxpanel/plugins

netstat_la_SOURCES = \
        netstat.c \
        netstat.h \
        nsconfig.h \
        devproc.c \
        devproc.h \
        statusicon.c \
        statusicon.h \
        wireless.c \
        wireless.h \
        lxnm_client.c \
        lxnm_client.h \
        passwd_gui.c \
        passwd_gui.h

netstat_la_LIBADD = \
        -liw \
        $(PACKAGE_LIBS)

netstat_la_LDFLAGS = \
    -module \
    @LXPANEL_MODULE@

install-exec-hook:
	rm -f "$(moduledir)\*.la" *.a
