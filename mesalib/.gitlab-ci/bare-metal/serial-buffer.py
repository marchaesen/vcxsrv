#!/usr/bin/python3

# Copyright © 2020 Google LLC
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

# Tiny script to read bytes from serial, and write the output to stdout, with a
# buffer in between so we don't lose serial output from its buffer.
#
# We don't use 'cu' because it requires stdin to be hooked up and I never
# managed to make that work without getting blocked somewhere.  We don't use
# 'conserver' because it's non-free.

import sys
import serial
import select
import os
import posix

dev=sys.argv[1]

ser = serial.Serial(dev, 115200, timeout=10)

while True:
    bytes = ser.read()
    sys.stdout.buffer.write(bytes)
    sys.stdout.flush()

ser.close()
