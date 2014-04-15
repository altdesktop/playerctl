#!/bin/sh
# Run this to generate all the initial makefiles, etc.

test -n "$srcdir" || srcdir=`dirname "$0"`
test -n "$srcdir" || srcdir=.

olddir=`pwd`

cd $srcdir
PROJECT=playerctl
TEST_TYPE=-f
FILE=playerctl/playerctl.h

test $TEST_TYPE $FILE || {
	echo "You must run this script in the top-level $PROJECT directory"
	exit 1
}

GTKDOCIZE=`which gtkdocize`
if test -z $GTKDOCIZE; then
        echo "*** No GTK-Doc found, please install it ***"
        exit 1
fi

AUTORECONF=`which autoreconf`
if test -z $AUTORECONF; then
        echo "*** No autoreconf found, please install it ***"
        exit 1
fi

# NOCONFIGURE is used by gnome-common
if test -z "$NOCONFIGURE"; then
        if test -z "$*"; then
                echo "I am going to run ./configure with no arguments - if you wish "
                echo "to pass any to it, please specify them on the $0 command line."
        fi
fi

rm -rf autom4te.cache

gtkdocize || exit $?
autoreconf --force --install --verbose || exit $?

cd "$olddir"
test -n "$NOCONFIGURE" || "$srcdir/configure" "$@"
