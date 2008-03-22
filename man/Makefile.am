
dist_man_MANS = lxpanel.1 lxpanelctl.1

EXTRA_DIST = lxpanel.sgml lxpanelctl.sgml

dist-hook: lxpanel.1 lxpanelctl.1

lxpanel.1: lxpanel.sgml
	docbook-to-man $< > $@

lxpanelctl.1: lxpanelctl.sgml
	docbook-to-man $< > $@
        