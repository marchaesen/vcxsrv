#!/usr/bin/env python3
#
# Copyright © 2020 Igalia, S.L.
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

import argparse
import os
import re
from serial_buffer import SerialBuffer
import sys
import threading

class PoERun:
    def __init__(self, args):
        self.powerup = args.powerup
        self.powerdown = args.powerdown
        self.ser = SerialBuffer(args.dev, "results/serial-output.txt", "", args.timeout)

    def print_error(self, message):
        RED = '\033[0;31m'
        NO_COLOR = '\033[0m'
        print(RED + message + NO_COLOR)

    def logged_system(self, cmd):
        print("Running '{}'".format(cmd))
        return os.system(cmd)

    def run(self):
        if self.logged_system(self.powerup) != 0:
            return 1

        boot_detected = False
        for line in self.ser.lines():
            if re.search("Booting Linux", line):
                boot_detected = True
                break

        if not boot_detected:
            self.print_error("Something wrong; couldn't detect the boot start up sequence")
            return 2

        for line in self.ser.lines():
            if re.search("---. end Kernel panic", line):
                return 1

            # Binning memory problems
            if re.search("binner overflow mem", line):
                self.print_error("Memory overflow in the binner; GPU hang")
                return 1

            result = re.search("hwci: mesa: (\S*)", line)
            if result:
                if result.group(1) == "pass":
                    return 0
                else:
                    return 1

        self.print_error("Reached the end of the CPU serial log without finding a result")
        return 2

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dev', type=str, help='Serial device to monitor', required=True)
    parser.add_argument('--powerup', type=str, help='shell command for rebooting', required=True)
    parser.add_argument('--powerdown', type=str, help='shell command for powering off', required=True)
    parser.add_argument('--timeout', type=int, default=60,
                        help='time in seconds to wait for activity', required=False)
    args = parser.parse_args()

    poe = PoERun(args)
    retval = poe.run()

    poe.logged_system(args.powerdown)

    sys.exit(retval)

if __name__ == '__main__':
    main()
