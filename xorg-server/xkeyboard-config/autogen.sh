#! /bin/sh

srcdir=`dirname "$0"`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd "$srcdir"

autopoint --force
AUTOPOINT='intltoolize --automake --copy' autoreconf -v --install --force || exit 1

git config --local --get format.subjectPrefix >/dev/null 2>&1 ||
    git config --local format.subjectPrefix "PATCH xkeyboard-config"

cd "$ORIGDIR" || exit $?
if test -z "$NOCONFIGURE"; then
    exec "$srcdir"/configure "$@"
fi
