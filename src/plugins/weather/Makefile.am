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
 $(PACKAGE_CFLAGS) \
 $(LIBXML2_CFLAGS)

weather_la_LDFLAGS= \
 -module            \
 @LXPANEL_MODULE@   \
 $(LIBXML2_LIBS)

install-exec-hoo:
	find . -name "*.{la,a}" -delete