#!/bin/sh

INDIR=$1
OUTFILE=base.ml1_s.part

> $OUTFILE

awk '{ 
  if (index($2, "(") == 0) {
    printf "  *		%s			=	pc+%s%%(v[1])\n", $1, $2; 
  } else {
    printf "  *		%s			=	pc+%s\n", $1, $2; 
  }
}' < $INDIR/layoutsMapping.lst >> $OUTFILE

awk '{ 
  printf "  *		%s(%s)			=	pc+%s(%s)\n", $1, $2, $3, $4; 
}' < $INDIR/variantsMapping.lst >> $OUTFILE
