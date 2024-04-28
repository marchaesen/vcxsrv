#!/bin/bash
# fontconfig/test/run-test.sh
#
# Copyright © 2000 Keith Packard
#
# Permission to use, copy, modify, distribute, and sell this software and its
# documentation for any purpose is hereby granted without fee, provided that
# the above copyright notice appear in all copies and that both that
# copyright notice and this permission notice appear in supporting
# documentation, and that the name of the author(s) not be used in
# advertising or publicity pertaining to distribution of the software without
# specific, written prior permission.  The authors make no
# representations about the suitability of this software for any purpose.  It
# is provided "as is" without express or implied warranty.
#
# THE AUTHOR(S) DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
# INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
# EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY SPECIAL, INDIRECT OR
# CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
# DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
# TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
# PERFORMANCE OF THIS SOFTWARE.
set -e

: "${TMPDIR=/tmp}"

case "$OSTYPE" in
    msys ) MyPWD=$(pwd -W) ;;  # On Msys/MinGW, returns a MS Windows style path.
    *    ) MyPWD=$(pwd)    ;;  # On any other platforms, returns a Unix style path.
esac

normpath() {
    printf "%s" "$1" | sed -E 's,/+,/,g'
}

TESTDIR=${srcdir-"$MyPWD"}
BUILDTESTDIR=${builddir-"$MyPWD"}

BASEDIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
FONTDIR=$(normpath "$BASEDIR"/fonts)
CACHEDIR=$(normpath "$BASEDIR"/cache.dir)
EXPECTED=${EXPECTED-"out.expected"}

FCLIST="$LOG_COMPILER $BUILDTESTDIR/../fc-list/fc-list$EXEEXT"
FCCACHE="$LOG_COMPILER $BUILDTESTDIR/../fc-cache/fc-cache$EXEEXT"

if [ -x "$(command -v bwrap)" ]; then
    BWRAP="$(command -v bwrap)"
fi

if [ -x "$(command -v md5sum)" ]; then
    MD5SUM="$(command -v md5sum)"
elif [ -x "$(command -v md5)" ]; then
    MD5SUM="$(command -v md5)"
else
    echo "E: No md5sum or equivalent command"
    exit 1
fi

FONT1=$(normpath $TESTDIR/4x6.pcf)
FONT2=$(normpath $TESTDIR/8x16.pcf)
TEST=""
export TZ=UTC

fdate() {
    sdate=$1
    ret=0
    date -d @0 > /dev/null 2>&1 || ret=$?
    if [ $ret -eq 0 ]; then
        ret=$(date -u -d @${sdate} +%y%m%d%H%M.%S)
    else
        ret=$(date -u -j -f "%s" +%y%m%d%H%M.%S $sdate)
    fi
    echo $ret
}

fstat() {
    fmt=$1
    fn=$2
    ret=0
    stat -c %Y "$fn" > /dev/null 2>&1 || ret=$?
    if [ $ret -eq 0 ]; then
        # GNU
        ret=$(stat -c "$fmt" "$fn")
    else
        # BSD
        if [ "x$fmt" == "x%Y" ]; then
            ret=$(stat -f "%m" "$fn")
        elif [ "x$fmt" == "x%y" ]; then
            ret=$(stat -f "%Sm" -t "%F %T %z" "$fn")
        elif [ "x$fmt" == "x%n %s %y %z" ]; then
            ret=$(stat -f "%SN %z %Sm %Sc" -t "%F %T %z" "$fn")
        else
            echo "E: Unknown format"
            exit 1
        fi
    fi
    echo $ret
}

clean_exit() {
    rc=$?
    trap - INT TERM ABRT EXIT
    if [ "x$TEST" != "x" ]; then
        echo "Aborting from '$TEST' with the exit code $rc"
    fi
    exit $rc
}
trap clean_exit INT TERM ABRT EXIT

check () {
    {
	$FCLIST - family pixelsize | sort;
	echo "=";
	$FCLIST - family pixelsize | sort;
	echo "=";
	$FCLIST - family pixelsize | sort;
    } > "$BUILDTESTDIR"/out
  tr -d '\015' <"$BUILDTESTDIR"/out >"$BUILDTESTDIR"/out.tmp; mv "$BUILDTESTDIR"/out.tmp "$BUILDTESTDIR"/out
  if cmp "$BUILDTESTDIR"/out "$BUILDTESTDIR"/"$EXPECTED" > /dev/null ; then : ; else
    echo "*** Test failed: $TEST"
    echo "*** output is in 'out', expected output in '$EXPECTED'"
    exit 1
  fi
  rm -f "$BUILDTESTDIR"/out
}

prep() {
  rm -rf "$CACHEDIR"
  rm -rf "$FONTDIR"
  mkdir "$FONTDIR"
}

dotest () {
  TEST=$1
  test x"$VERBOSE" = x || echo "Running: $TEST"
}

sed "s!@FONTDIR@!$FONTDIR!
s!@REMAPDIR@!!
s!@CACHEDIR@!$CACHEDIR!" < "$TESTDIR"/fonts.conf.in > "$BUILDTESTDIR"/fonts.conf

FONTCONFIG_FILE="$BUILDTESTDIR"/fonts.conf
export FONTCONFIG_FILE

dotest "Basic check"
prep
cp "$FONT1" "$FONT2" "$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"
fi
check

dotest "With a subdir"
prep
cp "$FONT1" "$FONT2" "$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"
fi
$FCCACHE "$FONTDIR"
check

dotest "Subdir with a cache file"
prep
mkdir "$FONTDIR"/a
cp "$FONT1" "$FONT2" "$FONTDIR"/a
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"/a
fi
$FCCACHE "$FONTDIR"/a
check

dotest "Complicated directory structure"
prep
mkdir "$FONTDIR"/a
mkdir "$FONTDIR"/a/a
mkdir "$FONTDIR"/b
mkdir "$FONTDIR"/b/a
cp "$FONT1" "$FONTDIR"/a
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"/a
fi
cp "$FONT2" "$FONTDIR"/b/a
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"/b/a
fi
check

dotest "Subdir with an out-of-date cache file"
prep
mkdir "$FONTDIR"/a
$FCCACHE "$FONTDIR"/a
sleep 1
cp "$FONT1" "$FONT2" "$FONTDIR"/a
check

dotest "Dir with an out-of-date cache file"
prep
cp "$FONT1" "$FONTDIR"
$FCCACHE "$FONTDIR"
sleep 1
mkdir "$FONTDIR"/a
cp "$FONT2" "$FONTDIR"/a
check

dotest "Keep mtime of the font directory"
prep
cp "$FONT1" "$FONTDIR"
touch -t $(fdate 0) "$FONTDIR"
fstat "%y" "$FONTDIR" > "$BUILDTESTDIR"/out1
$FCCACHE -v "$FONTDIR"
fstat "%y" "$FONTDIR" > "$BUILDTESTDIR"/out2
if cmp "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2 > /dev/null ; then : ; else
    echo "*** Test failed: $TEST"
    echo "mtime was modified"
    exit 1
fi

if [ x"$BWRAP" != "x" ] && [ "x$EXEEXT" = "x" ]; then
dotest "Basic functionality with the bind-mounted cache dir"
prep
cp "$FONT1" "$FONT2" "$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"
fi
$FCCACHE "$FONTDIR"
sleep 1
ls -l "$CACHEDIR" > "$BUILDTESTDIR"/out1
TESTTMPDIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
# Once font dir is remapped, we could use $FONTDIR as different one in theory.
# but we don't use it here and to avoid duplicate entries, set the non-existing
# directory here.
sed "s!@FONTDIR@!$FONTDIR/a!
s!@REMAPDIR@!<remap-dir as-path="'"'"$FONTDIR"'"'">$TESTTMPDIR/fonts</remap-dir>!
s!@CACHEDIR@!$TESTTMPDIR/cache.dir!" < "$TESTDIR"/fonts.conf.in > "$BUILDTESTDIR"/bind-fonts.conf
$BWRAP --bind / / --bind "$CACHEDIR" "$TESTTMPDIR"/cache.dir --bind "$FONTDIR" "$TESTTMPDIR"/fonts --bind "$BUILDTESTDIR"/.. "$TESTTMPDIR"/build --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTTMPDIR"/build/test/bind-fonts.conf "$TESTTMPDIR"/build/fc-match/fc-match"$EXEEXT" -f "%{file}\n" ":foundry=Misc" > "$BUILDTESTDIR"/xxx
if test -x "$BUILDTESTDIR"/test-bz106618"$EXEEXT"; then
    TESTEXE=test-bz106618"$EXEEXT"
elif test -x "$BUILDTESTDIR"/test_bz106618"$EXEEXT"; then
    TESTEXE=test_bz106618"$EXEEXT"
else
    echo "*** Test failed: no test case for bz106618"
    exit 1
fi
$BWRAP --bind / / --bind "$CACHEDIR" "$TESTTMPDIR"/cache.dir --bind "$FONTDIR" "$TESTTMPDIR"/fonts --bind "$BUILDTESTDIR"/.. "$TESTTMPDIR"/build --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTTMPDIR"/build/test/bind-fonts.conf "$TESTTMPDIR"/build/test/"$TESTEXE" | sort > "$BUILDTESTDIR"/flist1
$BWRAP --bind / / --bind "$CACHEDIR" "$TESTTMPDIR"/cache.dir --bind "$FONTDIR" "$TESTTMPDIR"/fonts --bind "$BUILDTESTDIR"/.. "$TESTTMPDIR"/build --dev-bind /dev /dev find "$TESTTMPDIR"/fonts/ -type f -name '*.pcf' | sort > "$BUILDTESTDIR"/flist2
ls -l "$CACHEDIR" > "$BUILDTESTDIR"/out2
if cmp "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2 > /dev/null ; then : ; else
  echo "*** Test failed: $TEST"
  echo "cache was created/updated."
  echo "Before:"
  cat "$BUILDTESTDIR"/out1
  echo "After:"
  cat "$BUILDTESTDIR"/out2
  exit 1
fi
if [ x"$(cat $BUILDTESTDIR/xxx)" != "x$TESTTMPDIR/fonts/4x6.pcf" ]; then
  echo "*** Test failed: $TEST"
  echo "file property doesn't point to the new place: $TESTTMPDIR/fonts/4x6.pcf"
  exit 1
fi
if cmp "$BUILDTESTDIR"/flist1 "$BUILDTESTDIR"/flist2 > /dev/null ; then : ; else
  echo "*** Test failed: $TEST"
  echo "file properties doesn't point to the new places"
  echo "Expected result:"
  cat "$BUILDTESTDIR"/flist2
  echo "Actual result:"
  cat "$BUILDTESTDIR"/flist1
  exit 1
fi
rm -rf "$TESTTMPDIR" "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2 "$BUILDTESTDIR"/xxx "$BUILDTESTDIR"/flist1 "$BUILDTESTDIR"/flist2 "$BUILDTESTDIR"/bind-fonts.conf

dotest "Different directory content between host and sandbox"
prep
cp "$FONT1" "$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"
fi
$FCCACHE "$FONTDIR"
sleep 1
ls -1 --color=no "$CACHEDIR"/*cache*> "$BUILDTESTDIR"/out1
fstat "%n %s %y %z" "$(cat $BUILDTESTDIR/out1)" > "$BUILDTESTDIR"/stat1
TESTTMPDIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
TESTTMP2DIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
cp "$FONT2" "$TESTTMP2DIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$TESTTMP2DIR"
fi
sed "s!@FONTDIR@!$TESTTMPDIR/fonts</dir><dir salt="'"'"salt-to-make-different"'"'">$FONTDIR!
s!@REMAPDIR@!<remap-dir as-path="'"'"$FONTDIR"'"'">$TESTTMPDIR/fonts</remap-dir>!
s!@CACHEDIR@!$TESTTMPDIR/cache.dir!" < "$TESTDIR"/fonts.conf.in > "$BUILDTESTDIR"/bind-fonts.conf
$BWRAP --bind / / --bind "$CACHEDIR" "$TESTTMPDIR"/cache.dir --bind "$FONTDIR" "$TESTTMPDIR"/fonts --bind "$TESTTMP2DIR" "$FONTDIR" --bind "$BUILDTESTDIR"/.. "$TESTTMPDIR"/build --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTTMPDIR"/build/test/bind-fonts.conf "$TESTTMPDIR"/build/fc-match/fc-match"$EXEEXT" -f "%{file}\n" ":foundry=Misc" > "$BUILDTESTDIR"/xxx
if test -x "$BUILDTESTDIR"/test-bz106618"$EXEEXT"; then
    TESTEXE=test-bz106618"$EXEEXT"
elif test -x "$BUILDTESTDIR"/test_bz106618"$EXEEXT"; then
    TESTEXE=test_bz106618"$EXEEXT"
else
    echo "*** Test failed: no test case for bz106618"
    exit 1
fi
$BWRAP --bind / / --bind "$CACHEDIR" "$TESTTMPDIR"/cache.dir --bind "$FONTDIR" "$TESTTMPDIR"/fonts --bind "$TESTTMP2DIR" "$FONTDIR" --bind "$BUILDTESTDIR"/.. "$TESTTMPDIR"/build --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTTMPDIR"/build/test/bind-fonts.conf "$TESTTMPDIR"/build/test/"$TESTEXE" | sort > "$BUILDTESTDIR"/flist1
$BWRAP --bind / / --bind "$CACHEDIR" "$TESTTMPDIR"/cache.dir --bind "$FONTDIR" "$TESTTMPDIR"/fonts --bind "$TESTTMP2DIR" "$FONTDIR" --bind "$BUILDTESTDIR"/.. "$TESTTMPDIR"/build --dev-bind /dev /dev find "$TESTTMPDIR"/fonts/ -type f -name '*.pcf' | sort > "$BUILDTESTDIR"/flist2
ls -1 --color=no "$CACHEDIR"/*cache* > "$BUILDTESTDIR"/out2
fstat "%n %s %y %z" "$(cat $BUILDTESTDIR/out1)" > "$BUILDTESTDIR"/stat2
if cmp "$BUILDTESTDIR"/stat1 "$BUILDTESTDIR"/stat2 > /dev/null ; then : ; else
  echo "*** Test failed: $TEST"
  echo "cache was created/updated."
  cat "$BUILDTESTDIR"/stat1 "$BUILDTESTDIR"/stat2
  exit 1
fi
if grep -v -- "$(cat $BUILDTESTDIR/out1)" "$BUILDTESTDIR"/out2 > /dev/null ; then : ; else
  echo "*** Test failed: $TEST"
  echo "cache wasn't created for dir inside sandbox."
  cat "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2
  exit 1
fi
if [ x"$(cat $BUILDTESTDIR/xxx)" != "x$TESTTMPDIR/fonts/4x6.pcf" ]; then
  echo "*** Test failed: $TEST"
  echo "file property doesn't point to the new place: $TESTTMPDIR/fonts/4x6.pcf"
  exit 1
fi
if cmp "$BUILDTESTDIR"/flist1 "$BUILDTESTDIR"/flist2 > /dev/null ; then
  echo "*** Test failed: $TEST"
  echo "Missing fonts should be available on sandbox"
  echo "Expected result:"
  cat "$BUILDTESTDIR"/flist2
  echo "Actual result:"
  cat "$BUILDTESTDIR"/flist1
  exit 1
fi
rm -rf "$TESTTMPDIR" "$TESTTMP2DIR" "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2 "$BUILDTESTDIR"/xxx "$BUILDTESTDIR"/flist1 "$BUILDTESTDIR"/flist2 "$BUILDTESTDIR"/stat1 "$BUILDTESTDIR"/stat2 "$BUILDTESTDIR"/bind-fonts.conf

dotest "Check consistency of MD5 in cache name"
prep
mkdir -p "$FONTDIR"/sub
cp "$FONT1" "$FONTDIR"/sub
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"/sub
fi
$FCCACHE "$FONTDIR"
sleep 1
(cd "$CACHEDIR"; ls -1 --color=no ./*cache*) > "$BUILDTESTDIR"/out1
TESTTMPDIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
mkdir -p "$TESTTMPDIR"/cache.dir
# Once font dir is remapped, we could use $FONTDIR as different one in theory.
# but we don't use it here and to avoid duplicate entries, set the non-existing
# directory here.
sed "s!@FONTDIR@!$FONTDIR/a!
s!@REMAPDIR@!<remap-dir as-path="'"'"$FONTDIR"'"'">$TESTTMPDIR/fonts</remap-dir>!
s!@CACHEDIR@!$TESTTMPDIR/cache.dir!" < "$TESTDIR"/fonts.conf.in > "$BUILDTESTDIR"/bind-fonts.conf
$BWRAP --bind / / --bind "$FONTDIR" "$TESTTMPDIR"/fonts --bind "$BUILDTESTDIR"/.. "$TESTTMPDIR"/build --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTTMPDIR"/build/test/bind-fonts.conf "$TESTTMPDIR"/build/fc-cache/fc-cache"$EXEEXT" "$TESTTMPDIR"/fonts
(cd "$TESTTMPDIR"/cache.dir; ls -1 --color=no ./*cache*) > "$BUILDTESTDIR"/out2
if cmp "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2 > /dev/null ; then : ; else
    echo "*** Test failed: $TEST"
    echo "cache was created unexpectedly."
    echo "Before:"
    cat "$BUILDTESTDIR"/out1
    echo "After:"
    cat "$BUILDTESTDIR"/out2
    exit 1
fi
rm -rf "$TESTTMPDIR" "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2 "$BUILDTESTDIR"/bind-fonts.conf

dotest "Fallback to uuid"
prep
cp "$FONT1" "$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"
fi
touch -t "$(fdate $(fstat "%Y" "$FONTDIR"))" "$FONTDIR"
$FCCACHE "$FONTDIR"
sleep 1
_cache=$(ls -1 --color=no "$CACHEDIR"/*cache*)
_mtime=$(fstat "%Y" "$FONTDIR")
_uuid=$(uuidgen)
_newcache=$(echo "$_cache" | sed "s/\([0-9a-f]*\)\(\-.*\)/$_uuid\2/")
mv "$_cache" "$_newcache"
echo "$_uuid" > "$FONTDIR"/.uuid
touch -t "$(fdate "$_mtime")" "$FONTDIR"
(cd "$CACHEDIR"; ls -1 --color=no ./*cache*) > "$BUILDTESTDIR"/out1
TESTTMPDIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
mkdir -p "$TESTTMPDIR"/cache.dir
sed "s!@FONTDIR@!$TESTTMPDIR/fonts!
s!@REMAPDIR@!<remap-dir as-path="'"'"$FONTDIR"'"'">$TESTTMPDIR/fonts</remap-dir>!
s!@CACHEDIR@!$TESTTMPDIR/cache.dir!" < "$TESTDIR"/fonts.conf.in > "$BUILDTESTDIR"/bind-fonts.conf
$BWRAP --bind / / --bind "$CACHEDIR" "$TESTTMPDIR"/cache.dir --bind "$FONTDIR" "$TESTTMPDIR"/fonts --bind "$BUILDTESTDIR"/.. "$TESTTMPDIR"/build --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTTMPDIR"/build/test/bind-fonts.conf "$TESTTMPDIR"/build/fc-match/fc-match"$EXEEXT" -f ""
(cd "$CACHEDIR"; ls -1 --color=no ./*cache*) > "$BUILDTESTDIR"/out2
if cmp "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2 > /dev/null ; then : ; else
    echo "*** Test failed: $TEST"
    echo "cache was created unexpectedly."
    echo "Before:"
    cat "$BUILDTESTDIR"/out1
    echo "After:"
    cat "$BUILDTESTDIR"/out2
    exit 1
fi
rm -rf "$TESTTMPDIR" "$BUILDTESTDIR"/out1 "$BUILDTESTDIR"/out2 "$BUILDTESTDIR"/bind-fonts.conf

else
    echo "No bubblewrap installed. skipping..."
fi # if [ x"$BWRAP" != "x" -a "x$EXEEXT" = "x" ]

if [ "x$EXEEXT" = "x" ]; then
dotest "sysroot option"
prep
mkdir -p "$BUILDTESTDIR"/sysroot/"$FONTDIR"
mkdir -p "$BUILDTESTDIR"/sysroot/"$CACHEDIR"
mkdir -p "$BUILDTESTDIR"/sysroot/"$BUILDTESTDIR"
cp "$FONT1" "$BUILDTESTDIR"/sysroot/"$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$BUILDTESTDIR"/sysroot/"$FONTDIR"
fi
cp "$BUILDTESTDIR"/fonts.conf "$BUILDTESTDIR"/sysroot/"$BUILDTESTDIR"/fonts.conf
$FCCACHE -y "$BUILDTESTDIR"/sysroot

dotest "creating cache file on sysroot"
md5=$(printf "%s" "$FONTDIR" | $MD5SUM | sed 's/ .*$//')
echo "checking for cache file $md5"
if ! ls "$BUILDTESTDIR/sysroot/$CACHEDIR/$md5"*; then
  echo "*** Test failed: $TEST"
  echo "No cache for $FONTDIR ($md5)"
  ls "$BUILDTESTDIR"/sysroot/"$CACHEDIR"
  exit 1
fi

rm -rf "$BUILDTESTDIR"/sysroot

dotest "read newer caches when multiple places are allowed to store"
prep
cp "$FONT1" "$FONT2" "$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
    # epoch 0 has special meaning. increase to avoid epoch 0
    old_epoch=${SOURCE_DATE_EPOCH}
    SOURCE_DATE_EPOCH=$(("$SOURCE_DATE_EPOCH" + 1))
fi
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"
fi
MYCACHEBASEDIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
MYCACHEDIR="$MYCACHEBASEDIR"/cache.dir
MYOWNCACHEDIR="$MYCACHEBASEDIR"/owncache.dir
MYCONFIG=$(mktemp "$TMPDIR"/fontconfig.XXXXXXXX)

mkdir -p "$MYCACHEDIR"
mkdir -p "$MYOWNCACHEDIR"

sed "s!@FONTDIR@!$FONTDIR!
s!@REMAPDIR@!!
s!@CACHEDIR@!$MYCACHEDIR!" < "$TESTDIR"/fonts.conf.in > "$BUILDTESTDIR"/my-fonts.conf

FONTCONFIG_FILE="$BUILDTESTDIR"/my-fonts.conf $FCCACHE "$FONTDIR"

sleep 1
cat<<EOF>"$MYCONFIG"
<fontconfig>
  <match target="scan">
    <test name="file"><string>$FONTDIR/4x6.pcf</string></test>
    <edit name="pixelsize"><int>8</int></edit>
  </match>
</fontconfig>
EOF
sed "s!@FONTDIR@!$FONTDIR!
s!@REMAPDIR@!<include ignore_missing=\"yes\">$MYCONFIG</include>!
s!@CACHEDIR@!$MYOWNCACHEDIR!" < "$TESTDIR"/fonts.conf.in > "$BUILDTESTDIR"/my-fonts.conf

if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  SOURCE_DATE_EPOCH=$(("$SOURCE_DATE_EPOCH" + 1))
fi
FONTCONFIG_FILE="$BUILDTESTDIR"/my-fonts.conf $FCCACHE -f "$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ]; then
  SOURCE_DATE_EPOCH=${old_epoch}
fi

sed "s!@FONTDIR@!$FONTDIR!
s!@REMAPDIR@!<include ignore_missing=\"yes\">$MYCONFIG</include>!
s!@CACHEDIR@!$MYCACHEDIR</cachedir><cachedir>$MYOWNCACHEDIR!" < "$TESTDIR"/fonts.conf.in > "$BUILDTESTDIR"/my-fonts.conf

{
    FONTCONFIG_FILE="$BUILDTESTDIR"/my-fonts.conf $FCLIST - family pixelsize | sort;
    echo "=";
    FONTCONFIG_FILE="$BUILDTESTDIR"/my-fonts.conf $FCLIST - family pixelsize | sort;
    echo "=";
    FONTCONFIG_FILE="$BUILDTESTDIR"/my-fonts.conf $FCLIST - family pixelsize | sort;
} > "$BUILDTESTDIR"/my-out
tr -d '\015' <"$BUILDTESTDIR"/my-out >"$BUILDTESTDIR"/my-out.tmp; mv "$BUILDTESTDIR"/my-out.tmp "$BUILDTESTDIR"/my-out
sed -e 's/pixelsize=6/pixelsize=8/g' "$BUILDTESTDIR"/"$EXPECTED" > "$BUILDTESTDIR"/my-out.expected

if cmp "$BUILDTESTDIR"/my-out "$BUILDTESTDIR"/my-out.expected > /dev/null ; then : ; else
    echo "*** Test failed: $TEST"
    echo "*** output is in 'my-out', expected output in 'my-out.expected'"
    echo "Actual Result"
    cat "$BUILDTESTDIR"/my-out
    echo "Expected Result"
    cat "$BUILDTESTDIR"/my-out.expected
    exit 1
fi

rm -rf "$MYCACHEBASEDIR" "$MYCONFIG" "$BUILDTESTDIR"/my-fonts.conf "$BUILDTESTDIR"/my-out "$BUILDTESTDIR"/my-out.expected

fi # if [ "x$EXEEXT" = "x" ]

if [ -x "$BUILDTESTDIR"/test-crbug1004254 ]; then
    dotest "MT-safe global config"
    prep
    curl -s -o "$FONTDIR"/noto.zip https://noto-website-2.storage.googleapis.com/pkgs/NotoSans-hinted.zip
    (cd "$FONTDIR"; unzip noto.zip)
    if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
        touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"
    fi
    "$BUILDTESTDIR"/test-crbug1004254
else
    echo "No test-crbug1004254: skipped"
fi

if [ "x$EXEEXT" = "x" ]; then

dotest "empty XDG_CACHE_HOME"
prep
export XDG_CACHE_HOME=""
export old_HOME="$HOME"
export temp_HOME=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
export HOME="$temp_HOME"
cp "$FONT1" "$FONT2" "$FONTDIR"
if [ -n "${SOURCE_DATE_EPOCH:-}" ] && [ ${#SOURCE_DATE_EPOCH} -gt 0 ]; then
    touch -m -t "$(fdate ${SOURCE_DATE_EPOCH})" "$FONTDIR"
fi
echo "<fontconfig><dir>$FONTDIR</dir><cachedir prefix=\"xdg\">fontconfig</cachedir></fontconfig>" > "$BUILDTESTDIR"/my-fonts.conf
FONTCONFIG_FILE="$BUILDTESTDIR"/my-fonts.conf $FCCACHE "$FONTDIR" || :
if [ -d "$HOME"/.cache ] && [ -d "$HOME"/.cache/fontconfig ]; then : ; else
  echo "*** Test failed: $TEST"
  echo "No \$HOME/.cache/fontconfig directory"
  ls -a "$HOME"
  ls -a "$HOME"/.cache
  exit 1
fi

export HOME="$old_HOME"
rm -rf "$temp_HOME" "$BUILDTESTDIR"/my-fonts.conf
unset XDG_CACHE_HOME
unset old_HOME
unset temp_HOME

fi # if [ "x$EXEEXT" = "x" ]

rm -rf "$FONTDIR" "$CACHEFILE" "$CACHEDIR" "$BASEDIR" "$FONTCONFIG_FILE" out

TEST=""
