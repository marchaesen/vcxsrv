#! /bin/sh
# -*- sh -*-
# Copyright (C) 2023 fontconfig Authors
# SPDX-License-Identifier: MIT

. $(dirname $0)/functions

dotest "Remap - same family name but different filename"
prep

TESTRESULT1=$(mktemp "$TMPDIR"/fontconfig.XXXXXXXX)
TESTRESULT2=$(mktemp "$TMPDIR"/fontconfig.XXXXXXXX)
TESTRESULT3=$(mktemp "$TMPDIR"/fontconfig.XXXXXXXX)
TESTFONT1DIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
TESTFONT2DIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
TESTCACHE1DIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
TESTCACHE2DIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
TESTBUILD1DIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
TESTBUILD2DIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
TESTRUNDIR=$(mktemp -d "$TMPDIR"/fontconfig.XXXXXXXX)
mkdir -p "$TESTBUILD1DIR"/build
mkdir -p "$TESTBUILD2DIR"/build
mkdir -p "$TESTRUNDIR"/fonts
mkdir -p "$TESTRUNDIR"/fonts-cache
cp "$FONT1" "$TESTFONT1DIR"/foo.pcf
cp "$FONT1" "$TESTFONT2DIR"/bar.pcf
touch -m -t $(date -d @0 +%y%m%d%H%M.%S) "$TESTFONT1DIR"
touch -m -t $(date -d @0 +%y%m%d%H%M.%S) "$TESTFONT1DIR"/*
touch -m -t $(date -d @0 +%y%m%d%H%M.%S) "$TESTFONT2DIR"
touch -m -t $(date -d @0 +%y%m%d%H%M.%S) "$TESTFONT2DIR"/*

cat<<_EOF_>>"$TESTBUILD1DIR"/fonts.conf
<fontconfig>
  <dir>/usr/share/fonts</dir>
  <cachedir>/usr/lib/fontconfig/cache</cachedir>
</fontconfig>
_EOF_
cat<<_EOF_>>"$TESTBUILD2DIR"/fonts.conf
<fontconfig>
  <dir salt="flatpak">/usr/share/fonts</dir>
  <cachedir>/usr/lib/fontconfig/cache</cachedir>
</fontconfig>
_EOF_
cat<<_EOF_>>"$TESTBUILD2DIR"/bind-fonts.conf
<fontconfig>
<dir salt="flatpak">/usr/share/fonts</dir>
<dir>$TESTRUNDIR/fonts</dir>
<cachedir>/usr/lib/fontconfig/cache</cachedir>
<cachedir>$TESTRUNDIR/fonts-cache</cachedir>

<remap-dir as-path="/usr/share/fonts">$TESTRUNDIR/fonts</remap-dir>
</fontconfig>
_EOF_

# Generate host caches
$BWRAP --bind / / --bind "$TESTCACHE1DIR" /usr/lib/fontconfig/cache --bind "$TESTFONT1DIR" /usr/share/fonts --bind "$TESTBUILD1DIR" /usr/share/fontconfig --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTBUILD1DIR"/fonts.conf $FCCACHE
touch -m -t $(date -d @0 +%y%m%d%H%M.%S) "$TESTCACHE1DIR"
touch -m -t $(date -d @0 +%y%m%d%H%M.%S) "$TESTCACHE1DIR"/*
$BWRAP --bind / / --bind "$TESTCACHE1DIR" /usr/lib/fontconfig/cache --bind "$TESTFONT1DIR" /usr/share/fonts --bind "$TESTBUILD1DIR" /usr/share/fontconfig --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTBUILD1DIR"/fonts.conf $FCMATCH Fixed file > "$TESTRESULT1"

if grep foo.pcf "$TESTRESULT1" > /dev/null; then : ; else
    echo "*** Test failed: $TEST"
    echo "file property doesn't point to the expected file."
    cat "$TESTRESULT1"
    exit 1
fi

# Generate runtime caches
$BWRAP --bind / / --bind "$TESTCACHE2DIR" /usr/lib/fontconfig/cache --bind "$TESTFONT2DIR" /usr/share/fonts --bind "$TESTBUILD2DIR" /usr/share/fontconfig --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTBUILD2DIR"/fonts.conf $FCCACHE
touch -m -t $(date -d @0 +%y%m%d%H%M.%S) "$TESTCACHE2DIR"
touch -m -t $(date -d @0 +%y%m%d%H%M.%S) "$TESTCACHE2DIR"/*
$BWRAP --bind / / --bind "$TESTCACHE2DIR" /usr/lib/fontconfig/cache --bind "$TESTFONT2DIR" /usr/share/fonts --bind "$TESTBUILD2DIR" /usr/share/fontconfig --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTBUILD2DIR"/fonts.conf $FCMATCH Fixed file > "$TESTRESULT2"

if grep bar.pcf "$TESTRESULT2" > /dev/null; then : ; else
    echo "*** Test failed: $TEST"
    echo "file property doesn't point to the expected file."
    cat "$TESTRESULT2"
    exit 1
fi

# Ask for fonts on similar environemnt to flatpak
$BWRAP --bind / / --ro-bind "$TESTCACHE2DIR" /usr/lib/fontconfig/cache --ro-bind "$TESTFONT2DIR" /usr/share/fonts --bind "$TESTBUILD2DIR" /usr/share/fontconfig --ro-bind "$TESTRUNDIR" "$TESTRUNDIR" --ro-bind "$TESTCACHE1DIR" "$TESTRUNDIR"/fonts-cache --ro-bind "$TESTFONT1DIR" "$TESTRUNDIR"/fonts --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTBUILD2DIR"/bind-fonts.conf $FCMATCH Fixed file > "$TESTRESULT3"
$BWRAP --bind / / --ro-bind "$TESTCACHE2DIR" /usr/lib/fontconfig/cache --ro-bind "$TESTFONT2DIR" /usr/share/fonts --bind "$TESTBUILD2DIR" /usr/share/fontconfig --ro-bind "$TESTRUNDIR" "$TESTRUNDIR" --ro-bind "$TESTCACHE1DIR" "$TESTRUNDIR"/fonts-cache --ro-bind "$TESTFONT1DIR" "$TESTRUNDIR"/fonts --dev-bind /dev /dev --setenv FONTCONFIG_FILE "$TESTBUILD2DIR"/bind-fonts.conf ls $(sed 's/:file=//' "$TESTRESULT3") > /dev/null

# Check the amount of cache files
if [ $(ls "$TESTCACHE1DIR"|wc -l) == 2 ]; then : ; else
    echo "*** Test failed: $TEST"
    echo "Too much cache files created at host cache dir."
    ls "$TESTCACHE1DIR"
    exit 1
fi
if [ $(ls "$TESTCACHE2DIR"|wc -l) == 2 ]; then : ; else
    echo "*** Test failed: $TEST"
    echo "Too much cache files created at runtime cache dir."
    ls "$TESTCACHE2DIR"
    exit 1
fi

rm -rf "$TESTFONT1DIR" "$TESTFONT2DIR" "$TESTCACHE1DIR" "$TESTCACHE2DIR" "$TESTBUILD1DIR" "$TESTBUILD2DIR"
rm -f "$TESTRESULT1" "$TESTRESULT2"

TEST=""

echo "Success."
