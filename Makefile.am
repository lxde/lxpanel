## Process this file with automake to produce Makefile.in

SUBDIRS = src po data man

EXTRA_DIST = \
	autogen.sh \
        mkinstalldirs \
        autogen.sh \
        intltool-extract.in \
        intltool-merge.in \
        intltool-update.in

##desktopdir = $(datadir)/applications
##desktop_DATA = pcmanfm.desktop

## icondir = $(datadir)/pixmaps
## icon_DATA = data/pcmanfm.png

install-data-local:
	@$(NORMAL_INSTALL)
	if test -d $(srcdir)/pixmaps; then \
	  $(mkinstalldirs) $(DESTDIR)$(pkgdatadir)/pixmaps; \
	  for pixmap in $(srcdir)/pixmaps/*; do \
	    if test -f $$pixmap; then \
	      $(INSTALL_DATA) $$pixmap $(DESTDIR)$(pkgdatadir)/pixmaps; \
	    fi \
	  done \
	fi

dist-hook:
	if test -d pixmaps; then \
	  mkdir $(distdir)/pixmaps; \
	  for pixmap in pixmaps/*; do \
	    if test -f $$pixmap; then \
	      cp -p $$pixmap $(distdir)/pixmaps; \
	    fi \
	  done \
	fi

