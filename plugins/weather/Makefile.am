module_LTLIBRARIES= weather.la

moduledir= $(libdir)/lxpanel/plugins

weather_la_SOURCES= \
 logutil.c          \
 httputil.c         \
 yahooutil.c        \
 location.c         \
 forecast.c         \
 weatherwidget.c    \
 weather.c

weather_la_CFLAGS= \
 -I.               \
 -I$(top_builddir)/src \
 $(PACKAGE_CFLAGS) \
 $(LIBXML2_CFLAGS)

weather_la_LDFLAGS= \
 -module            \
 @LXPANEL_MODULE@   \
 $(LIBXML2_LIBS)

EXTRA_DIST = \
	logutil.h \
	httputil.h \
	yahooutil.h \
	location.h \
	forecast.h \
	weatherwidget.h

install-exec-hook:
	find . -name "*.{la,a}" -delete