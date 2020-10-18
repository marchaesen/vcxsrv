#!/usr/bin/env python3

import sys
import argparse
import subprocess

if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('fccache')
    args = parser.parse_args()
    sys.exit(subprocess.run([args.fccache, '-s', '-f', '-v']).returncode)
