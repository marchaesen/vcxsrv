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
import sys
import threading

from custom_logger import CustomLogger
from serial_buffer import SerialBuffer

class PoERun:
    def __init__(self, args, boot_timeout, test_timeout, logger):
        self.powerup = args.powerup
        self.powerdown = args.powerdown
        self.ser = SerialBuffer(
            args.dev, "results/serial-output.txt", ": ")
        self.boot_timeout = boot_timeout
        self.test_timeout = test_timeout
        self.logger = logger

    def print_error(self, message):
        RED = '\033[0;31m'
        NO_COLOR = '\033[0m'
        print(RED + message + NO_COLOR)
        self.logger.update_status_fail(message)

    def logged_system(self, cmd):
        print("Running '{}'".format(cmd))
        return os.system(cmd)

    def run(self):
        if self.logged_system(self.powerup) != 0:
            self.logger.update_status_fail("powerup failed")
            return 1

        boot_detected = False
        self.logger.create_job_phase("boot")
        for line in self.ser.lines(timeout=self.boot_timeout, phase="bootloader"):
            if re.search("Booting Linux", line):
                boot_detected = True
                break

        if not boot_detected:
            self.print_error(
                "Something wrong; couldn't detect the boot start up sequence")
            return 2

        self.logger.create_job_phase("test")
        for line in self.ser.lines(timeout=self.test_timeout, phase="test"):
            if re.search("---. end Kernel panic", line):
                self.logger.update_status_fail("kernel panic")
                return 1

            # Binning memory problems
            if re.search("binner overflow mem", line):
                self.print_error("Memory overflow in the binner; GPU hang")
                return 1

            if re.search("nouveau 57000000.gpu: bus: MMIO read of 00000000 FAULT at 137000", line):
                self.print_error("nouveau jetson boot bug, abandoning run.")
                return 1

            # network fail on tk1
            if re.search("NETDEV WATCHDOG:.* transmit queue 0 timed out", line):
                self.print_error("nouveau jetson tk1 network fail, abandoning run.")
                return 1

            result = re.search(r"hwci: mesa: (\S*), exit_code: (\d+)", line)
            if result:
                status = result.group(1)
                exit_code = int(result.group(2))

                if status == "pass":
                    self.logger.update_dut_job("status", "pass")
                else:
                    self.logger.update_status_fail("test fail")

                self.logger.update_dut_job("exit_code", exit_code)
                return exit_code

        self.print_error(
            "Reached the end of the CPU serial log without finding a result")
        return 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dev', type=str,
                        help='Serial device to monitor', required=True)
    parser.add_argument('--powerup', type=str,
                        help='shell command for rebooting', required=True)
    parser.add_argument('--powerdown', type=str,
                        help='shell command for powering off', required=True)
    parser.add_argument(
        '--boot-timeout-seconds', type=int, help='Boot phase timeout (seconds)', required=True)
    parser.add_argument(
        '--test-timeout-minutes', type=int, help='Test phase timeout (minutes)', required=True)
    args = parser.parse_args()

    logger = CustomLogger("results/job_detail.json")
    logger.update_dut_time("start", None)
    poe = PoERun(args, args.boot_timeout_seconds, args.test_timeout_minutes * 60, logger)
    retval = poe.run()

    poe.logged_system(args.powerdown)
    logger.update_dut_time("end", None)

    sys.exit(retval)


if __name__ == '__main__':
    main()
