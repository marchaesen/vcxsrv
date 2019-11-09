#!/bin/sh

GPU_VERSION="$1"

DEQP_OPTIONS="--deqp-surface-width=256 --deqp-surface-height=256"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-visibility=hidden"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-log-images=disable"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-watchdog=enable"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-crashhandler=enable"
DEQP_OPTIONS="$DEQP_OPTIONS --deqp-surface-type=pbuffer"

export LIBGL_DRIVERS_PATH=/mesa/lib/dri/
export LD_LIBRARY_PATH=/mesa/lib/
export MESA_GLES_VERSION_OVERRIDE=3.0

DEVFREQ_GOVERNOR=`echo /sys/devices/platform/*.gpu/devfreq/devfreq0/governor`
echo performance > $DEVFREQ_GOVERNOR

cd /deqp/modules/gles2

# Generate test case list file
./deqp-gles2 $DEQP_OPTIONS --deqp-runmode=stdout-caselist | grep "TEST: dEQP-GLES2" | cut -d ' ' -f 2 > /tmp/case-list.txt

# Note: not using sorted input and comm, becuase I want to run the tests in
# the same order that dEQP would.
while read -r line; do
   if echo "$line" | grep -q '^[^#]'; then
       sed -i "/$line/d" /tmp/case-list.txt
   fi
done < /deqp/deqp-$GPU_VERSION-skips.txt

/deqp/deqp-volt --cts-build-dir=/deqp \
                --threads=8 \
                --test-names-file=/tmp/case-list.txt \
                --results-file=/tmp/results.txt \
                --no-passed-results \
                --regression-file=/deqp/deqp-$GPU_VERSION-fails.txt \
                --no-rerun-tests \
                --print-regression \
                --no-print-fail \
                --no-print-quality \
                --no-colour-term \
                 $DEQP_OPTIONS

if [ $? -ne 0 ]; then
    echo "Regressions detected"
    echo "deqp: fail"
else
    echo "No regressions detected"
    echo "deqp: pass"
fi
