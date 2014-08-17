xkb_la_CFLAGS = \
	-I. \
	-I$(top_srcdir)/src \
	$(PACKAGE_CFLAGS) \
	-DFLAGSDIR=\"$(datadir)/lxpanel/images/xkb-flags\" \
	-DFLAGSCUSTDIR=\"$(datadir)/lxpanel/images/xkb-flags-cust\" \
	-DXKBCONFDIR=\"$(datadir)/lxpanel/xkeyboardconfig\" \
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

xkeyboardconfigdir=$(datadir)/lxpanel/xkeyboardconfig
xkeyboardconfig_DATA=xkeyboardconfig/models.cfg \
	xkeyboardconfig/layouts.cfg \
	xkeyboardconfig/toggle.cfg

flagsdir=$(datadir)/lxpanel/images/xkb-flags
flags_DATA= \
	flags/ad.png \
	flags/ae.png \
	flags/af.png \
	flags/al.png \
	flags/am.png \
	flags/ar.png \
	flags/ara.png \
	flags/at.png \
	flags/az.png \
	flags/ba.png \
	flags/bd.png \
	flags/be.png \
	flags/ben.png \
	flags/bg.png \
	flags/bh.png \
	flags/br.png \
	flags/brai.png \
	flags/bt.png \
	flags/bw.png \
	flags/by.png \
	flags/ca.png \
	flags/cd.png \
	flags/ch.png \
	flags/cm.png \
	flags/cn.png \
	flags/cu.png \
	flags/cz.png \
	flags/de.png \
	flags/dev.png \
	flags/dj.png \
	flags/dk.png \
	flags/dvorak.png \
	flags/dz.png \
	flags/ee.png \
	flags/eg.png \
	flags/epo.png \
	flags/es.png \
	flags/et.png \
	flags/fi.png \
	flags/fo.png \
	flags/fr.png \
	flags/gb.png \
	flags/ge.png \
	flags/gh.png \
	flags/gn.png \
	flags/gr.png \
	flags/hr.png \
	flags/hu.png \
	flags/ie.png \
	flags/il.png \
	flags/in.png \
	flags/iq.png \
	flags/ir.png \
	flags/is.png \
	flags/it.png \
	flags/jo.png \
	flags/jp.png \
	flags/ke.png \
	flags/kg.png \
	flags/kh.png \
	flags/km.png \
	flags/kp.png \
	flags/kr.png \
	flags/kw.png \
	flags/kz.png \
	flags/la.png \
	flags/latam.png \
	flags/lb.png \
	flags/lk.png \
	flags/lt.png \
	flags/lv.png \
	flags/ly.png \
	flags/ma.png \
	flags/mao.png \
	flags/me.png \
	flags/mk.png \
	flags/ml.png \
	flags/mm.png \
	flags/mn.png \
	flags/mt.png \
	flags/mv.png \
	flags/mx.png \
	flags/nec_vndr-jp.png \
	flags/ng.png \
	flags/nl.png \
	flags/no.png \
	flags/np.png \
	flags/om.png \
	flags/ph.png \
	flags/pk.png \
	flags/pl.png \
	flags/pt.png \
	flags/qa.png \
	flags/ro.png \
	flags/rs.png \
	flags/ru.png \
	flags/sa.png \
	flags/sd.png \
	flags/se.png \
	flags/si.png \
	flags/sk.png \
	flags/sn.png \
	flags/so.png \
	flags/sr.png \
	flags/sy.png \
	flags/th.png \
	flags/tj.png \
	flags/tm.png \
	flags/tn.png \
	flags/tr.png \
	flags/tw.png \
	flags/tz.png \
	flags/ua.png \
	flags/us.png \
	flags/uy.png \
	flags/uz.png \
	flags/vn.png \
	flags/ye.png \
	flags/za.png

EXTRA_DIST = \
	$(flags_DATA) \
	$(xkeyboardconfig_DATA)
