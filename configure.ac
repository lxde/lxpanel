AC_PREREQ(2.53)
AC_INIT(lxpanel, 0.1, http://lxpanel.sourceforge.net/)
AM_INIT_AUTOMAKE()
AC_CONFIG_SRCDIR([src/bg.c])
AC_CONFIG_HEADER([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_LIBTOOL
AC_PROG_INTLTOOL(, [no-xml])

# Checks for libraries.
pkg_modules="gtk+-2.0 >= 2.6.0 \
             gthread-2.0 \
             libstartup-notification-1.0"
PKG_CHECK_MODULES(PACKAGE, [$pkg_modules])
AC_SUBST(PACKAGE_CFLAGS)
AC_SUBST(PACKAGE_LIBS)

AC_ARG_ENABLE([plugins-loading],
	AS_HELP_STRING([--disable-plugins-loading],
		       [disable plugin loading (default: enable)]),
		       plugins_loading=$enableval, plugins_loading="yes")

if test "$plugins_loading" = "no"; then
	AC_DEFINE(DISABLE_PLUGINS_LOADING, [1], [Disable plugin loading])
	AM_CONDITIONAL(BUILD_NETSTATUS, no)
else
	dnl check for netstatus
	PKG_CHECK_MODULES(GCONF, [gconf-2.0], HAVE_GCONF=yes, HAVE_GCONF=no)
	AM_CONDITIONAL(BUILD_NETSTATUS, test x$HAVE_GCONF = xyes)
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


GETTEXT_PACKAGE=lxpanel
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION(0.14.1)
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE,"$GETTEXT_PACKAGE", [Gettext package.])

ALL_LINGUAS="zh_TW"
AM_GLIB_GNU_GETTEXT

AC_CONFIG_FILES([
	Makefile
	src/Makefile
	src/plugins/Makefile
	src/plugins/netstatus/Makefile
	po/Makefile.in
	data/Makefile
	data/default
])
AC_OUTPUT
