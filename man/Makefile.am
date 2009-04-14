
dist_man_MANS = lxpanel.1 lxpanelctl.1

EXTRA_DIST = lxpanel.sgml lxpanelctl.sgml

dist-hook: lxpanel.1 lxpanelctl.1

lxpanel.1: lxpanel.sgml
	(test -f "`which docbook-to-man`" && docbook-to-man $< > $@) || \
	(test -f "`which db2man`" && db2man $< > $@) || \
	(test -f "`which docbook2man`" && docbook2man $< > $@)

lxpanelctl.1: lxpanelctl.sgml
	(test -f "`which docbook-to-man`" && docbook-to-man $< > $@) || \
	(test -f "`which db2man`" && db2man $< > $@) || \
	(test -f "`which docbook2man`" && docbook2man $< > $@)
