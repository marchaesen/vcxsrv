#!/bin/bash

set -e
set -o xtrace

VERSION=`head -1 install/VERSION`
ROOTDIR=`pwd`

if [ -d results ]; then
    cd results && rm -rf ..?* .[!.]* *
fi
cd /piglit

export OCL_ICD_VENDORS=$ROOTDIR/install/etc/OpenCL/vendors/

set +e
unset DISPLAY
export LD_LIBRARY_PATH=$ROOTDIR/install/lib
clinfo

# If the job is parallel at the gitlab job level, will take the corresponding
# fraction of the caselist.
if [ -n "$CI_NODE_INDEX" ]; then

    if [ "$PIGLIT_PROFILES" != "${PIGLIT_PROFILES% *}" ]; then
        echo "Can't parallelize piglit with multiple profiles"
        exit 1
    fi
    USE_CASELIST=1
fi

if [ -n "$USE_CASELIST" ]; then
    ./piglit print-cmd $PIGLIT_TESTS $PIGLIT_PROFILES --format "{name}" > /tmp/case-list.txt

    sed -ni $CI_NODE_INDEX~$CI_NODE_TOTAL"p" /tmp/case-list.txt

    PIGLIT_TESTS="--test-list /tmp/case-list.txt"
fi

./piglit run -c -j${FDO_CI_CONCURRENT:-4} $PIGLIT_OPTIONS $PIGLIT_TESTS $PIGLIT_PROFILES $ROOTDIR/results
retVal=$?
if [ $retVal -ne 0 ]; then
    echo "Found $(cat /tmp/version.txt), expected $VERSION"
fi
set -e

PIGLIT_RESULTS=${PIGLIT_RESULTS:-$PIGLIT_PROFILES}
mkdir -p .gitlab-ci/piglit
./piglit summary console $ROOTDIR/results \
  | tee ".gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig" \
  | head -n -1 \
  | grep -v ": pass" \
  | sed '/^summary:/Q' \
  > .gitlab-ci/piglit/$PIGLIT_RESULTS.txt

if [ -n "$USE_CASELIST" ]; then
    # Just filter the expected results based on the tests that were actually
    # executed, and switch to the version with no summary
    cat .gitlab-ci/piglit/$PIGLIT_RESULTS.txt.orig | sed '/^summary:/Q' | rev \
         | cut -f2- -d: | rev | sed "s/$/:/g" > /tmp/executed.txt
    grep -F -f /tmp/executed.txt $ROOTDIR/install/$PIGLIT_RESULTS.txt \
         > .gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline || true
else
    cp $ROOTDIR/install/$PIGLIT_RESULTS.txt .gitlab-ci/piglit/$PIGLIT_RESULTS.txt.baseline
fi

if diff -q .gitlab-ci/piglit/$PIGLIT_RESULTS.txt{.baseline,}; then
    exit 0
fi

./piglit summary html --exclude-details=pass $ROOTDIR/results/summary $ROOTDIR/results

echo Unexpected change in results:
diff -u .gitlab-ci/piglit/$PIGLIT_RESULTS.txt{.baseline,}
exit 1
