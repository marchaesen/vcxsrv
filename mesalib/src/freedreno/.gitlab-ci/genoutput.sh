#!/bin/bash

# This file contains all of the cmdlines used to generate output
# for the test step in the CI pipeline.  It can also be used to
# regenerate reference output

set -x
set -e

# input/output directories:
base=src/freedreno
traces=$base/.gitlab-ci/traces
reference=$base/.gitlab-ci/reference
output=$base/.gitlab-ci/out

# use the --update arg to update reference output:
if [ "$1" = "--update" ]; then
	output=$reference
fi

mkdir -p $output

# binary locations:
cffdump=./install/bin/cffdump
crashdec=./install/bin/crashdec
asm=./install/bin/afuc-asm
disasm=./install/bin/afuc-disasm

# helper to filter out paths that can change depending on
# who is building:
basepath=`dirname $0`
basepath=`dirname $basepath`
basepath=`pwd $basepath`
filter() {
	out=$1
	grep -vF "$basepath" > $out
}

#
# The Tests:
#

# dump only a single frame, and single tile pass, to keep the
# reference output size managable
$cffdump --frame 0 --once $traces/fd-clouds.rd.gz | filter $output/fd-clouds.log
$cffdump --frame 0 --once $traces/es2gears-a320.rd.gz | filter $output/es2gears-a320.log
$cffdump --frame 1 --once $traces/glxgears-a420.rd.gz | filter $output/glxgears-a420.log
$cffdump --once $traces/dEQP-GLES2.functional.texture.specification.basic_teximage2d.rgba16f_2d.rd.gz | filter $output/dEQP-GLES2.functional.texture.specification.basic_teximage2d.rgba16f_2d.log
$cffdump --frame 0 --once $traces/dEQP-VK.draw.indirect_draw.indexed.indirect_draw_count.triangle_list.rd.gz | filter $output/dEQP-VK.draw.indirect_draw.indexed.indirect_draw_count.triangle_list.log

# test a lua script to ensure we don't break scripting API:
$cffdump --script $base/decode/scripts/parse-submits.lua $traces/shadow.rd.gz | filter $output/shadow.log

$crashdec -sf $traces/crash.devcore | filter $output/crash.log

$asm -g 6 $traces/afuc_test.asm $output/afuc_test.fw
$disasm -g 6 $reference/afuc_test.fw | filter $output/afuc_test.asm
