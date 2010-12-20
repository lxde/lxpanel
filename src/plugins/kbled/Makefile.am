kbled_la_CFLAGS = \
    -I. \
    -I$(top_srcdir)/src \
    -DPACKAGE_DATA_DIR=\""$(datadir)"\" \
    $(PACKAGE_CFLAGS) \
    $(G_CAST_CHECKS)

module_LTLIBRARIES = kbled.la

moduledir = $(libdir)/lxpanel/plugins

kbled_la_SOURCES = kbled.c

kbled_la_LIBADD = $(PACKAGE_LIBS)

kbled_la_LDFLAGS = \
    -module \
    @LXPANEL_MODULE@
