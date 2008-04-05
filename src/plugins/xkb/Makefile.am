INCLUDES = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	-DFLAGSDIR=\"$(datadir)/lxpanel/images/xkb-flags\"
	$(G_CAST_CHECKS)

module_LTLIBRARIES = xkb.la
moduledir = $(libdir)/lxpanel/plugins

xkb_la_SOURCES = \
	xkb-plugin.c \
	xkb.c \
	xkb.h

xkb_la_LIBADD = \
	$(PACKAGE_LIBS)

xkb_la_LDFLAGS = \
	-module \
	@LXPANEL_MODULE@

flagsdir=$(datadir)/lxpanel/images/xkb-flags
flags_DATA= \
	flags/ae.png \
	flags/am.png \
	flags/ar.png \
	flags/at.png \
	flags/az.png \
	flags/be.png \
	flags/bg.png \
	flags/bh.png \
	flags/br.png \
	flags/by.png \
	flags/ca.png \
	flags/ch.png \
	flags/cu.png \
	flags/cz.png \
	flags/de.png \
	flags/dj.png \
	flags/dk.png \
	flags/dz.png \
	flags/ee.png \
	flags/eg.png \
	flags/es.png \
	flags/eu.png \
	flags/fi.png \
	flags/fr.png \
	flags/ge.png \
	flags/gr.png \
	flags/hr.png \
	flags/hu.png \
	flags/il.png \
	flags/iq.png \
	flags/is.png \
	flags/it.png \
	flags/jo.png \
	flags/jp.png \
	flags/km.png \
	flags/kr.png \
	flags/kw.png \
	flags/la.png \
	flags/lb.png \
	flags/lt.png \
	flags/ly.png \
	flags/ma.png \
	flags/mk.png \
	flags/mn.png \
	flags/mx.png \
	flags/nl.png \
	flags/no.png \
	flags/om.png \
	flags/pl.png \
	flags/ps.png \
	flags/pt.png \
	flags/qa.png \
	flags/qc.png \
	flags/ro.png \
	flags/ru.png \
	flags/sa.png \
	flags/sd.png \
	flags/se.png \
	flags/si.png \
	flags/sk.png \
	flags/so.png \
	flags/sr.png \
	flags/sy.png \
	flags/th.png \
	flags/tn.png \
	flags/tr.png \
	flags/ua.png \
	flags/uk.png \
	flags/un.png \
	flags/us.png \
	flags/uy.png \
	flags/vn.png \
	flags/ye.png \
	flags/yu.png

EXTRA_DIST =	\
	$(flags_DATA)
