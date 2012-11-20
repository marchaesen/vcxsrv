#!/bin/sh

variant=$1

INDIR=$2
OUTFILE=base.ml${variant}_s.part

> $OUTFILE

awk '{
  if (index($2, "(") == 0) {
    printf "  *		%s		=	+%s%%(v['${variant}']):'${variant}'\n", $1, $2;
  } else {
    printf "  *		%s		=	+%s:'${variant}'\n", $1, $2;
  }
}' < $INDIR/layoutsMapping.lst >> $OUTFILE

awk '{
  printf "  *		%s(%s)	=	+%s(%s):'${variant}'\n", $1, $2, $3, $4;
}' < $INDIR/variantsMapping.lst >> $OUTFILE
