#! /bin/sh

srcdir=`dirname "$0"`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd "$srcdir"

autoreconf --force --verbose --install || exit 1
cd "$ORIGDIR" || exit $?

if test -z "$NOCONFIGURE"; then
    "$srcdir"/configure "$@"
fi
