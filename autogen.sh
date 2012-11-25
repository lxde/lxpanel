#! /bin/sh
set -x

autoreconf -i -f
intltoolize -c --automake --force
