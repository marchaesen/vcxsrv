#!/bin/bash

set -e
set -x

p=`dirname $0`

# Run cffdump/crashdec tests:
$p/genoutput.sh
diff -r $p/reference $p/out

# Also, while we are still using headergen2 for generating kernel
# headers, make sure that doesn't break:
headergen="_build/src/freedreno/rnn/headergen2"
$headergen adreno.xml
$headergen msm.xml
