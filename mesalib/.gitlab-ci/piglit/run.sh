#!/bin/bash

set -e
set -o xtrace

VERSION=`cat artifacts/VERSION`

cd /piglit

PIGLIT_OPTIONS=$(echo $PIGLIT_OPTIONS | head -n 1)
xvfb-run --server-args="-noreset" sh -c \
         "export LD_LIBRARY_PATH=$OLDPWD/install/lib;
         wflinfo --platform glx --api gl --profile core | grep \"Mesa $VERSION\\\$\" &&
         ./piglit run -j4 $PIGLIT_OPTIONS $PIGLIT_PROFILES $OLDPWD/results"

PIGLIT_RESULTS=${PIGLIT_RESULTS:-$PIGLIT_PROFILES}
mkdir -p .gitlab-ci/piglit
cp $OLDPWD/artifacts/piglit/$PIGLIT_RESULTS.txt .gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline
./piglit summary console $OLDPWD/results | head -n -1 | grep -v ": pass" >.gitlab-ci/piglit/$PIGLIT_RESULTS.txt

if diff -q .gitlab-ci/piglit/$PIGLIT_RESULTS.txt{.baseline,}; then
    exit 0
fi

./piglit summary html --exclude-details=pass $OLDPWD/summary $OLDPWD/results

echo Unexpected change in results:
diff -u .gitlab-ci/piglit/$PIGLIT_RESULTS.txt{.baseline,}
exit 1
