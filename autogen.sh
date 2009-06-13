#! /bin/sh
AC_VERSION=

AUTOMAKE=${AUTOMAKE:-automake}
AM_INSTALLED_VERSION=$($AUTOMAKE --version | sed -e '2,$ d' -e 's/ *([^()]*)$//' -e 's/.* \(.*\)/\1/' -e 's/-p[0-9]\+//')

if [ "$AM_INSTALLED_VERSION" != "1.10" \
    -a "$AM_INSTALLED_VERSION" != "1.11" ];then
	echo
	echo "You must have automake > 1.10 or 1.11 installed to compile lxpanel."
	echo "Install the appropriate package for your distribution,"
	echo "or get the source tarball at http://ftp.gnu.org/gnu/automake/"
	exit 1
fi

set -x

if [ "x${ACLOCAL_DIR}" != "x" ]; then
  ACLOCAL_ARG=-I ${ACLOCAL_DIR}
fi

${ACLOCAL:-aclocal$AM_VERSION} ${ACLOCAL_ARG}
${AUTOHEADER:-autoheader$AC_VERSION} --force
$AUTOMAKE libtoolize -c --automake --force
$AUTOMAKE intltoolize -c --automake --force
$AUTOMAKE --add-missing --copy --include-deps
${AUTOCONF:-autoconf$AC_VERSION}

# mkinstalldirs was not correctly installed in some cases.
cp -f /usr/share/${AUTOMAKE:-automake$AM_VERSION}/mkinstalldirs .

rm -rf autom4te.cache
