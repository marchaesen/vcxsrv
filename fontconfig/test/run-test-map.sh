#!/bin/bash
# fontconfig/test/run-test-cache-map.sh
#
# Copyright © 2018 Keith Packard
#
# Permission to use, copy, modify, distribute, and sell this software and its
# documentation for any purpose is hereby granted without fee, provided that
# the above copyright notice appear in all copies and that both that copyright
# notice and this permission notice appear in supporting documentation, and
# that the name of the copyright holders not be used in advertising or
# publicity pertaining to distribution of the software without specific,
# written prior permission.  The copyright holders make no representations
# about the suitability of this software for any purpose.  It is provided "as
# is" without express or implied warranty.
#
# THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
# EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
# DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
# TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
# OF THIS SOFTWARE.
#

case "$OSTYPE" in
    msys ) MyPWD=`pwd -W` ;;  # On Msys/MinGW, returns a MS Windows style path.
    *    ) MyPWD=`pwd`    ;;  # On any other platforms, returns a Unix style path.
esac

TESTDIR=${srcdir-"$MyPWD"}
BUILDTESTDIR=${builddir-"$MyPWD"}

FONTDIRA="$MyPWD"/fontsa
FONTDIRB="$MyPWD"/fontsb
CACHEDIR="$MyPWD"/cache.dir
EXPECTEDIN=${EXPECTEDIN-"out-map.expected.in"}
EXPECTEDA="out-map-a.expected"
EXPECTEDB="out-map-b.expected"
EXPECTED="out-map.expected"

FCLIST=../fc-list/fc-list$EXEEXT
FCCACHE=../fc-cache/fc-cache$EXEEXT

which bwrap > /dev/null 2>&1
if [ $? -eq 0 ]; then
    BWRAP=`which bwrap`
fi

FONT1=$TESTDIR/4x6.pcf
FONT2=$TESTDIR/8x16.pcf

check () {
  $FCLIST - file family pixelsize | sort > out
  echo "=" >> out
  $FCLIST - file family pixelsize | sort >> out
  echo "=" >> out
  $FCLIST - file family pixelsize | sort >> out
  tr -d '\015' <out >out.tmp; mv out.tmp out
  if cmp out $BUILDTESTDIR/$EXPECTED > /dev/null ; then : ; else
    echo "*** Test failed: $TEST"
    echo "*** output is in 'out', expected output in '$EXPECTED'"
    exit 1
  fi
  rm -f out
}

prep() {
  rm -rf $CACHEDIR
  rm -rf $FONTDIRA $FONTDIRB
  mkdir $FONTDIRA
  mkdir $CACHEDIR
}

dotest () {
  TEST=$1
  test x$VERBOSE = x || echo Running: $TEST
}

sed "s!@FONTDIR@!$FONTDIRA!
s!@MAP@!!
s!@CACHEDIR@!$CACHEDIR!" < $TESTDIR/fonts.conf.in > fonts-a.conf

sed "s!@FONTDIR@!$FONTDIRB!
s!@MAP@!map="'"'"$FONTDIRA"'"'"!
s!@CACHEDIR@!$CACHEDIR!" < $TESTDIR/fonts.conf.in > fonts-b.conf

sed "s!@FONTDIR@!$FONTDIRA!" < $EXPECTEDIN > $EXPECTEDA
sed "s!@FONTDIR@!$FONTDIRB!" < $EXPECTEDIN > $EXPECTEDB

FONTCONFIG_FILE="$MyPWD"/fonts-a.conf
export FONTCONFIG_FILE

dotest "Basic check"
prep
cp $FONT1 $FONT2 $FONTDIRA
cp $EXPECTEDA $EXPECTED
$FCCACHE $FONTDIRA
check

dotest "mapped check"
prep
cp $FONT1 $FONT2 $FONTDIRA
cp $EXPECTEDB $EXPECTED
$FCCACHE $FONTDIRA
mv $FONTDIRA $FONTDIRB
export FONTCONFIG_FILE="$MyPWD"/fonts-b.conf
check
