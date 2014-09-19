#! /bin/sh
set -ex

autoreconf -i -f
intltoolize -c --automake --force

rm -rf autom4te.cache
