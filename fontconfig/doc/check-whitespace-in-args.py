#! /usr/bin/env python3

import argparse
import glob
import re
import sys
from pathlib import Path

ret = 0

parser = argparse.ArgumentParser()
parser.add_argument('path')

args = parser.parse_args()

for fn in glob.glob(str(Path(args.path) / '*.fncs')):
    with open(fn) as f:
        for i, line in enumerate(f):
            if re.search(r'[\w\[\]]+\s+@ARG', line, re.A):
                ret = 1
                print("{file}:{line}\t{str}".format(file=fn, line=i+1, str=line.rstrip()))

sys.exit(ret)
