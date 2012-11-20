#!/bin/sh

INDIR=$1
OUTFILE=base.ml1v1_s.part

> $OUTFILE

awk '{ 
  printf "  *		%s		%s		=	pc+%s(%s)\n", $1, $2, $3, $4; 
}' < $INDIR/variantsMapping.lst >> $OUTFILE
