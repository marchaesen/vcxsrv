#!/bin/sh

d=$(dirname $0)

rnndb=$1

if [ ! -f $rnndb/rnndb/adreno/adreno_common.xml ]; then
	echo directory does not look like envytools: $rnndb
	exit 1
fi

for f in $d/*.xml.h; do
	cp -v $rnndb/rnndb/adreno/$(basename $f) $d
done
