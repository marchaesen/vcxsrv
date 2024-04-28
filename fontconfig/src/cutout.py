#!/usr/bin/env python3

from pathlib import Path
import argparse
import re

if __name__== '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('input')
    parser.add_argument('output')
    args = parser.parse_args()

    input_lines = Path(args.input).read_text(encoding='utf-8').splitlines()
    with Path(args.output).open('w', encoding='utf-8') as out:
        write = True
        for l in input_lines:
            if l.startswith('CUT_OUT_BEGIN'):
                write = False

            if write and l:
                stripped = re.sub(r'^\s+', '', l)
                stripped = re.sub(r'\s*,\s*', ',', stripped)
                if not stripped.isspace() and stripped:
                    out.write('%s\n' % stripped)

            if l.startswith('CUT_OUT_END'):
                write = True
