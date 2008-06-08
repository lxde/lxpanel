AC_PREREQ(2.53)
AC_INIT(lxpanel, 0.3.7, http://lxde.org/)
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/bg.c])
AC_CONFIG_HEADER([config.h])

# Force to dynamic
AC_DISABLE_STATIC

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_LIBTOOL
AC_PROG_INTLTOOL(, [no-xml])

# Checks for libraries.
pkg_modules="gtk+-2.0 >= 2.6.0 \
             gthread-2.0"
#             libstartup-notification-1.0"
PKG_CHECK_MODULES(PACKAGE, [$pkg_modules])
AC_SUBST(PACKAGE_CFLAGS)
AC_SUBST(PACKAGE_LIBS)

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

# check libiw for netstat plugin
AC_ARG_ENABLE([libiw],
    AS_HELP_STRING([--disable-libiw],
                    [compile libiw-based netstat plugin (default: enable)]),
            compile_libiw=$enableval, compile_libiw="yes")
if test x"$compile_libiw" = "xyes"; then
    AC_CHECK_HEADERS(iwlib.h,
                     [AC_CHECK_LIB(iw,
                                   iw_sockets_open,
                                   compile_libiw=yes,
                                   compile_libiw=no)],
                                   compile_libiw=no)
fi
AM_CONDITIONAL(BUILD_LIBIW_PLUGINS, test x$compile_libiw = xyes)

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

AC_MSG_CHECKING([which plugins should be built dynamically])
AC_ARG_WITH(plugins,
[  --with-plugins=<list>  compile dynamic plugins in <list> ]
[                         plugins may be comma separated ]
[                         'all' builds all plugins (default), 'none' builds none ]
[                         Possible plugins are: ]
[                         netstatus, netstat, volume, volumealsa, cpu, deskno, batt, kbled, xkb], plugins="$withval",[plugins="all"])

if test x"$plugins" != xall -a x"$plugins" != xnone; then
    if test x"$plugins_loading" = xno; then
        AC_MSG_ERROR(
        [--disable-plugins-loading and --with-plugins conflict each other.])
    fi
fi

ALL_PLUGINS_LIST="netstatus netstat volume volumealsa cpu deskno batt kbled xkb"
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
    plugin_xkb=

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
if test x"$compile_libiw" = "xno"; then
   if test ! -z $plugin_netstat; then
       AC_MSG_RESULT([no libiw headers/libraries found!])
   fi
   plugin_netstat=
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
    $plugin_xkb"

AC_SUBST(PLUGINS_LIST)


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


GETTEXT_PACKAGE=lxpanel
#AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.14.1)
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

ALL_LINGUAS="zh_TW ko hu sk pl fr pt_BR fi de"
AM_GLIB_GNU_GETTEXT

AC_CONFIG_FILES([
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
    po/Makefile.in
    data/Makefile
    data/default/panels/panel
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
        AC_MSG_RESULT([    netstat - Monitor networking status (Linux Only)])
    fi
    if test x"$plugin_volume" != x; then
        AC_MSG_RESULT([    volume - Display and adjust volume of sound card])
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
else
echo Dynamic loader for plugins....... : disabled.
fi
