AC_PREREQ(2.53)
AC_INIT(lxpanel, 0.5.10, http://lxde.org/)
AM_INIT_AUTOMAKE([1.11 -Wall -Werror foreign])
AC_CONFIG_HEADER([config.h])

# Support silent build rules. Disable by either passing --disable-silent-rules
# to configure or passing V=1 to make
AM_SILENT_RULES([yes])

# Force to dynamic
AC_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_INTLTOOL(, [no-xml])
AM_PROG_CC_C_O

#Initialize libtool
LT_PREREQ([2.2])
LT_INIT

# Checks for libraries.
AC_ARG_ENABLE(gtk3,
AC_HELP_STRING([--enable-gtk3],[enable to use gtk-3.0 instead of gtk-2.0]),
[case "${enableval}" in
  yes)  enable_gtk3=yes ;;
  no)   enable_gtk3=no ;;
  *) AC_MSG_ERROR([bad value "${enableval}" for --enable-gtk3, use "yes" (default) or "no".]) ;;
esac],[])

if test "x$enable_gtk3" = "xyes" ; then
  CFLAGS="$CFLAGS -DENABLE_GTK3"
  pkg_modules="$pkg_modules gtk+-3.0"
else
  pkg_modules="$pkg_modules gtk+-2.0 >= 2.12.0"
fi

pkg_modules="$pkg_modules \
             gio-unix-2.0 \
             gthread-2.0 \
             libmenu-cache"
#             libstartup-notification-1.0"
PKG_CHECK_MODULES(PACKAGE, [$pkg_modules])
AC_SUBST(PACKAGE_CFLAGS)
AC_SUBST(PACKAGE_LIBS)

###########################
# Indicator Info
###########################
AC_ARG_ENABLE([indicator_support],
    AS_HELP_STRING([--enable-indicator-support],
               [enable plugin loading (default: disable)]),
               indicator_support=$enableval, indicator_support="no")
AM_CONDITIONAL(ENABLE_INDICATOR_SUPPORT, test x"$indicator_support" = x"yes")

if test "$indicator_support" != "no"; then
	INDICATOR_REQUIRED_VERSION=0.3.0
	INDICATOR_PKG_NAME=indicator-0.4

	PKG_CHECK_MODULES(APPLET, $INDICATOR_PKG_NAME >= $INDICATOR_REQUIRED_VERSION)
	AC_SUBST(APPLET_CFLAGS)

	if test "x$with_localinstall" = "xyes"; then
		INDICATORDIR="${libdir}/indicators/2/"
		INDICATORICONSDIR="${datadir}/indicator-applet/icons/"
	else
		INDICATORDIR=`$PKG_CONFIG --variable=indicatordir $INDICATOR_PKG_NAME`
		INDICATORICONSDIR=`$PKG_CONFIG --variable=iconsdir $INDICATOR_PKG_NAME`
fi
	AC_SUBST(INDICATORDIR)
	AC_SUBST(INDICATORICONSDIR)
fi


pkg_modules="x11"
PKG_CHECK_MODULES(X11, [$pkg_modules])
AC_SUBST(X11_LIBS)

pkg_modules="libwnck-1.0"
PKG_CHECK_MODULES(WNCK, [$pkg_modules])
AC_SUBST(WNCK_CFLAGS)
AC_SUBST(WNCK_LIBS)

pkg_modules="libmenu-cache"
PKG_CHECK_MODULES(MENU_CACHE, [$pkg_modules],
		  enable_menu_cache=yes, enable_menu_cache=no)
if test x"$enable_menu_cache" = "xno"; then
	AC_WARN([No libmenu-cache found.  All menu related routines are not built.])
	AC_DEFINE(DISABLE_MENU, [1], [Disable menu related routines])
fi
AC_SUBST(MENU_CACHE_CFLAGS)
AC_SUBST(MENU_CACHE_LIBS)
AM_CONDITIONAL(ENABLE_MENU_CACHE, test x"$enable_menu_cache" = x"yes")

AC_ARG_ENABLE(more_warnings,
       [AC_HELP_STRING([--enable-more-warnings],
               [Add more warnings @<:@default=no@:>@])],
       [enable_more_warnings="${enableval}"],
       [enable_more_warnings=no]
)

if test x"$enable_more_warnings" = x"yes"; then
  ADDITIONAL_FLAGS="-Wall -Werror=all -Werror=format -Werror=implicit-function-declaration -Werror=implicit-int -Werror=missing-braces -Werror=parentheses -Werror=return-type -Werror=strict-aliasing -Werror=switch -Wuninitialized -Werror=unused-label -Werror=unused-value -Wextra -Wno-missing-field-initializers -Wno-unused-parameter -Werror=missing-declarations -Wredundant-decls -Wmissing-noreturn -Wpointer-arith -Wcast-align -Wwrite-strings -Werror=inline -Werror=format-nonliteral -Wformat-nonliteral -Werror=format-security -Wformat-security -Winit-self -Werror=missing-include-dirs -Werror=undef -Werror=aggregate-return -Wmissing-format-attribute -Werror=nested-externs -fno-strict-aliasing -fmessage-length=0 -Wp,-D_FORTIFY_SOURCE=2 -DG_DISABLE_DEPRECATED -DG_DISABLE_SINGLE_INCLUDES -DGDK_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_DEPRECATED -DGDK_PIXBUF_DISABLE_SINGLE_INCLUDES -DGTK_DISABLE_DEPRECATED -DGTK_DISABLE_SINGLE_INCLUDES"
fi
AC_SUBST(ADDITIONAL_FLAGS)

PACKAGE_CFLAGS="$PACKAGE_CFLAGS $MENU_CACHE_CFLAGS $ADDITIONAL_FLAGS"
PACKAGE_LIBS="$PACKAGE_LIBS $MENU_CACHE_LIBS"

AC_ARG_ENABLE(man,
	[AC_HELP_STRING([--enable-man],
		[regenerate roff man pages from Docbook @<:@default=no@:>@])],
	[enable_man="${enableval}"],
	[enable_man=no]
)

AC_ARG_ENABLE([cast-checks],
    AS_HELP_STRING([--enable-cast-checks],
               [enable Glib casting checks (default: disable)]),
               cast_checks=$enableval, cast_checks="no")
G_CAST_CHECKS=""
if test x"$cast_checks" = "xno"; then
    G_CAST_CHECKS="-DG_DISABLE_CAST_CHECKS"
fi
AC_SUBST(G_CAST_CHECKS)

dnl linker tweaking
# The function of the link flag --as-needed is to prevent unnecesary linking.
# Example: A -> B -> C
# Normally, A would link to B and also depend on C, this is of cource
# unnecesary. In this situation, however we do need to link to C, so this
# must be done explicitly. This flag comes in handy when a library ABI
# is changed, minimizing the amount of recompilations needed.
AC_MSG_CHECKING([whether $LD accepts --as-needed])
case `$LD --as-needed -v 2>&1 </dev/null` in
*GNU* | *'with BFD'*)
    LDFLAGS="$LDFLAGS -Wl,--as-needed"
    AC_MSG_RESULT([yes])
    ;;
*)
    AC_MSG_RESULT([no])
    ;;
esac

dnl linker optimizations
AC_MSG_CHECKING([whether $LD accepts -O1])
case `$LD -O1 -v 2>&1 </dev/null` in
*GNU* | *'with BFD'*)
  LDFLAGS="$LDFLAGS -Wl,-O1"
  AC_MSG_RESULT([yes])
  ;;
*)
  AC_MSG_RESULT([no])
  ;;
esac
AC_MSG_CHECKING([whether $LD accepts -Bsymbolic-functions])
case `$LD -Bsymbolic-functions -v 2>&1 </dev/null` in
*GNU* | *'with BFD'*)
  LDFLAGS="$LDFLAGS -Wl,-Bsymbolic-functions"
  AC_MSG_RESULT([yes])
  ;;
*)
  AC_MSG_RESULT([no])
  ;;
esac
AC_MSG_CHECKING([whether $LD accepts --sort-common])
case `$LD --sort-common -v 2>&1 </dev/null` in
*GNU* | *'with BFD'*)
  LDFLAGS="$LDFLAGS -Wl,--sort-common"
  AC_MSG_RESULT([yes])
  ;;
*)
  AC_MSG_RESULT([no])
  ;;
esac

dnl FIXME: filter for undefined symbols in plugins
# when passing "-z defs" to linker, we would better to make sure
# there are no undefined references.  However, we intend not to
# fix at the moment since we don't have spin-off libraray containing
# the necessary functions.
if test ! -z "$LDFLAGS"; then
    LDFLAGS=`echo "$LDFLAGS" | sed -e 's/-Wl,-z,defs//'`
fi

LXPANEL_MODULE="-avoid-version \
-rpath \$(libdir)/lxpanel/plugins \
-no-undefined "'-export-symbols-regex "^[[^_]].*"'
AC_SUBST(LXPANEL_MODULE)

AC_ARG_ENABLE([alsa],
    AS_HELP_STRING([--disable-alsa],
                    [compile ALSA providers (default: enable)]),
            compile_alsa=$enableval, compile_alsa="yes")
if test x"$compile_alsa" = "xyes"; then
    AC_CHECK_HEADERS([alsa/asoundlib.h],
                     [AC_CHECK_LIB([asound],
                                   [snd_seq_open],
                                   compile_alsa=yes,compile_alsa=no)],
                          compile_alsa=no)
fi
AM_CONDITIONAL(BUILD_ALSA_PLUGINS, test x$compile_alsa = xyes)
dnl FIXME: check OSS existence
AM_CONDITIONAL(BUILD_OSS_PLUGINS, test x$compile_alsa = xno)

AC_ARG_ENABLE([plugins-loading],
    AS_HELP_STRING([--disable-plugins-loading],
               [disable plugin loading (default: enable)]),
               plugins_loading=$enableval, plugins_loading="yes")

AM_CONDITIONAL(BUILD_PLUGIN_LOADER, test x$plugins_loading = xyes)

if test x"$plugins_loading" = "xno"; then
    AC_DEFINE(DISABLE_PLUGINS_LOADING, [1], [Disable plugin loading])
fi

dnl Here are plugin listing.
plugin_netstatus=
plugin_netstat=
plugin_volume=
plugin_volumealsa=
plugin_cpu=
plugin_deskno=
plugin_batt=
plugin_xkb=
plugin_kbled=
plugin_thermal=
plugin_cpufreq=
plugin_monitors=
plugin_wnckpager=
plugin_indicator=

AC_MSG_CHECKING([which plugins should be built dynamically])
AC_ARG_WITH(plugins,
[  --with-plugins=<list>  compile dynamic plugins in <list> ]
[                         plugins may be comma separated ]
[                         'all' builds all plugins (default), 'none' builds none ]
[                         Possible plugins are: ]
[                         netstatus, netstat, volume, volumealsa, cpu, deskno, batt, kbled, xkb, thermal, cpufreq, monitors, wnckpager], plugins="$withval",[plugins="all"])

if test x"$plugins" != xall -a x"$plugins" != xnone; then
    if test x"$plugins_loading" = xno; then
        AC_MSG_ERROR(
        [--disable-plugins-loading and --with-plugins conflict each other.])
    fi
fi

ALL_PLUGINS_LIST="netstatus netstat volume volumealsa cpu deskno batt kbled xkb thermal cpufreq monitors wnckpager indicator"
AC_SUBST(ALL_PLUGINS_LIST)

if test x"$plugins" = xall; then
dnl    for name in $ALL_PLUGINS
dnl    do
dnl        export plugin_$name="$name"
dnl    done

    plugin_netstatus=netstatus
    plugin_netstat=netstat
    plugin_volume=volume
    plugin_volumealsa=volumealsa
    plugin_cpu=cpu
    plugin_deskno=deskno
    plugin_batt=batt
    plugin_kbled=kbled
    plugin_xkb=xkb
    plugin_thermal=thermal
    plugin_cpufreq=cpufreq
    plugin_monitors=monitors
    plugin_wnckpager=wnckpager
    plugin_indicator=indicator

    AC_MSG_RESULT(all)
else
    if test "$plugins" != "none"; then
        plugins=`echo $plugins | sed 's/,/ /g'`
        for plugin in $plugins
        do
        case "$plugin" in
            netstatus)
                plugin_netstatus=netstatus
                ;;
            netstat)
                plugin_netstat=netstat
                ;;
            volume)
                plugin_volume=volume
                ;;
            volumealsa)
                plugin_volumealsa=volumealsa
                ;;
            cpu)
                plugin_cpu=cpu
                ;;
            deskno)
                plugin_deskno=deskno
                ;;
            batt)
                plugin_batt=batt
                ;;
            kbled)
                plugin_kbled=kbled
                ;;
            xkb)
                plugin_xkb=xkb
                ;;
            thermal)
                plugin_thermal=thermal
                ;;
            cpufreq)
                plugin_cpufreq=cpufreq
                ;;
            monitors)
                plugin_monitors=monitors
                ;;
            wnckpager)
                plugin_wnckpager=wnckpager
                ;;
            indicator)
                plugin_indicator=indicator
                ;;
            *)
                echo "Unknown plugin $plugin."
                exit 1
                ;;
        esac
        done
        AC_MSG_RESULT($plugins)
    fi
fi

dnl FIXME: OSS/ALSA checking
if test x"$compile_alsa" = "xno"; then
    if test ! -z $plugin_volumealsa; then
        AC_MSG_RESULT([no ALSA headers/libraries found!])
        plugin_volumealsa=
    fi
else
    dnl force OSS plugin to <null>
    plugin_volume=
fi

dnl Exclude indicator support when there is no support.
if test x"$indicator_support" = "xno"; then
    plugin_indicator=
fi

PLUGINS_LIST="\
    $plugin_netstatus \
    $plugin_netstat \
    $plugin_volume \
    $plugin_volumealsa \
    $plugin_cpu \
    $plugin_deskno \
    $plugin_batt \
    $plugin_kbled \
    $plugin_xkb \
    $plugin_thermal \
    $plugin_cpufreq \
    $plugin_monitors \
    $plugin_wnckpager\
    $plugin_indicator"

AC_SUBST(PLUGINS_LIST)

# Plugin-specific dependencies
# nestat
if test x"$plugin_netstat" != "x";then
AC_CHECK_HEADER([iwlib.h],,
 AC_MSG_ERROR([netstat plugin requires wireless-tools-devel]))
fi

# Checks for header files.
AC_PATH_X
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_CHECK_HEADERS([locale.h stdlib.h string.h sys/time.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_STRUCT_TM

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
AC_TYPE_SIGNAL
AC_FUNC_STAT
AC_FUNC_STRFTIME
AC_CHECK_FUNCS([bzero memset mkdir setlocale strchr])


# Generate po/LINGUAS on the fly rather than relying on translators
# to maintain it manually. This also overcome the problem that Transifex
# cannot add a language to po/LINGUAS if a new po file is submitted.
rm -f $srcdir/po/LINGUAS
for po_file in `ls $srcdir/po/*.po | sort`;
do
    lang=`echo "$po_file" | sed "s|.*/po/\(.*\)\.po|\1|g"`
    echo $lang >> $srcdir/po/LINGUAS
done

GETTEXT_PACKAGE=lxpanel
#AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.14.1)
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

AM_GLIB_GNU_GETTEXT

if test x"$enable_man" = x"yes"; then
	AC_PATH_PROG([XSLTPROC], [xsltproc])
	if test -z "$XSLTPROC"; then
		enable_man=no
	fi

	dnl check for DocBook DTD and stylesheets in the local catalog.
	JH_CHECK_XML_CATALOG([-//OASIS//DTD DocBook XML V4.1.2//EN],
		[DocBook XML DTD V4.1.2], [], enable_man=no)
	JH_CHECK_XML_CATALOG([http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl],
		[DocBook XSL Stylesheets >= 1.70.1], [], enable_man=no)
fi

AM_CONDITIONAL(ENABLE_REGENERATE_MAN, test "x$enable_man" != "xno")

AC_CONFIG_FILES([
    lxpanel.pc
    Makefile
    src/Makefile
    src/plugins/Makefile
    src/plugins/netstatus/Makefile
    src/plugins/netstat/Makefile
    src/plugins/volume/Makefile
    src/plugins/volumealsa/Makefile
    src/plugins/cpu/Makefile
    src/plugins/deskno/Makefile
    src/plugins/batt/Makefile
    src/plugins/kbled/Makefile
    src/plugins/xkb/Makefile
    src/plugins/thermal/Makefile
    src/plugins/cpufreq/Makefile
    src/plugins/monitors/Makefile
    src/plugins/wnckpager/Makefile
    src/plugins/indicator/Makefile
    po/Makefile.in
    data/Makefile
    data/default/panels/panel
    data/two_panels/panels/top
    data/two_panels/panels/bottom
    man/Makefile
])
AC_OUTPUT

echo
echo lxpanel ......................... : Version $VERSION
echo
echo Prefix........................... : $prefix
if test x$plugins_loading = xyes; then
echo Building dynamic plugins:
    if test x"$plugin_netstatus" != x; then
        AC_MSG_RESULT([    netstatus - Monitor networking status])
    fi
    if test x"$plugin_netstat" != x; then
        AC_MSG_RESULT([    netstat - Monitor networking status])
    fi
    if test x"$plugin_volume" != x; then
        AC_MSG_RESULT([    volume - Display and adjust volume of sound card for OSS])
    fi
    if test x"$plugin_volumealsa" != x; then
        AC_MSG_RESULT([    volumealsa - Display and adjust volume of sound card for ALSA])
    fi
    if test x"$plugin_cpu" != x; then
        AC_MSG_RESULT([    cpu - Display CPU loading])
    fi
    if test x"$plugin_deskno" != x; then
        AC_MSG_RESULT([    deskno - Display desktop number])
    fi
    if test x"$plugin_batt" != x; then
        AC_MSG_RESULT([    batt - Monitor battery status])
    fi
    if test x"$plugin_kbled" != x; then
        AC_MSG_RESULT([    kbled - Indicator for CapsLock, NumLock, and ScrLock])
    fi
    if test x"$plugin_xkb" != x; then
        AC_MSG_RESULT([    xkb - Keyboard layout switcher])
    fi
    if test x"$plugin_thermal" != x; then
        AC_MSG_RESULT([    thermal - Temperature monitor])
    fi
    if test x"$plugin_cpufreq" != x; then
        AC_MSG_RESULT([    cpufreq - CpuFreq frontend])
    fi
    if test x"$plugin_monitors" != x; then
        AC_MSG_RESULT([    monitors - Monitors for CPU & RAM])
    fi
    if test x"$plugin_wnckpager" != x; then
        AC_MSG_RESULT([    wnckpager - Pager which support openbox and compiz])
    fi
    if test x"$plugin_indicator" != x; then
        AC_MSG_RESULT([    indicator - Indicator support])
    fi
else
echo Dynamic loader for plugins....... : disabled.
fi

echo
echo 'LXPanel requires menu-cache and lxmenu-data packages since'
echo 'version 0.4.0. Please make sure they are correctly installed.'
echo 'Otherwise, you will not be able to use applications menu.'
echo

