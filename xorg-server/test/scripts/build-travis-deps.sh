#!/bin/sh

export PREFIX=/usr
export TRAVIS_BUILD_DIR=/root
export PIGLIT_DIR=$TRAVIS_BUILD_DIR/piglit
export XTEST_DIR=$TRAVIS_BUILD_DIR/xts

cat > "$PIGLIT_DIR"/piglit.conf << _EOF_
[xts]
path=$XTEST_DIR
_EOF_

# awful
cp test/tetexec.cfg $XTEST_DIR/xts5

set -x

meson setup build/
meson configure -Dprefix=$PREFIX build/
ninja -C build/ install
ninja -C build/ test

status=$?

cat build/meson-logs/testlog.txt
cat build/test/piglit-results/xvfb/long-summary || :
# there should be a better way of extracting results, but:
# find build/test/piglit-results/xvfb/ | grep setfontpath | xargs cat
# isn't the worst thing ever

exit $status
