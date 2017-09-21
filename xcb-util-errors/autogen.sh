#! /bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir

# If this is a git checkout, verify that the submodules are initialized,
# otherwise autotools will just fail with an unhelpful error message.
if [ -d ".git" ] && [ -r ".gitmodules" ]
then
	# If git is not in PATH, this will not return 0, thus not keeping us
	# from building. Since the message is worthless when git is not
	# installed, this is what we want.
	if git submodule status 2>/dev/null | grep -q '^-'
	then
		echo "You have uninitialized git submodules." >&2
		echo "Please run: git submodule update --init" >&2
		exit 1
	fi
fi

autoreconf -v --install || exit 1
cd $ORIGDIR || exit $?

$srcdir/configure --enable-maintainer-mode "$@"
