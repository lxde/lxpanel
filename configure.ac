AC_PREREQ(2.53)
AC_INIT(lxpanel, 0.2.4, http://lxde.sourceforge.net/)
AM_INIT_AUTOMAKE()
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
plugin_volume=
plugin_volumealsa=
plugin_cpu=
plugin_deskno=
plugin_batt=

AC_MSG_CHECKING([which plugins should be built dynamically])
AC_ARG_WITH(plugins,
[  --with-plugins=<list>  compile dynamic plugins in <list> ]
[                         plugins may be comma separated ]
[                         'all' builds all plugins (default), 'none' builds none ]
[                         Possible plugins are: ]
[                         netstatus, volume, volumealsa, cpu, deskno, batt], plugins="$withval",[plugins="all"])

if test x"$plugins" != xall -a x"$plugins" != xnone; then
	if test x"$plugins_loading" = xno; then
		AC_MSG_ERROR(
		[--disable-plugins-loading and --with-plugins conflict each other.])
	fi
fi

if test x"$plugins" = xall; then
	plugin_netstatus=netstatus
	plugin_volume=volume
	plugin_volumealsa=volumealsa
	plugin_cpu=cpu
	plugin_deskno=deskno
	plugin_batt=batt
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
				plugin_deskno=batt
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
PLUGINS_LIST="$plugin_netstatus $plugin_volume $plugin_volumealsa $plugin_cpu $plugin_deskno $plugin_batt"
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
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.14.1)
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

ALL_LINGUAS="zh_TW ko hu sk pl"
AM_GLIB_GNU_GETTEXT

AC_CONFIG_FILES([
	Makefile
	src/Makefile
	src/plugins/Makefile
	src/plugins/netstatus/Makefile
	src/plugins/volume/Makefile
	src/plugins/volumealsa/Makefile
	src/plugins/cpu/Makefile
	src/plugins/deskno/Makefile
	src/plugins/batt/Makefile
	po/Makefile.in
	data/Makefile
	data/default
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
else
echo Dynamic loader for plugins....... : disabled.
fi
