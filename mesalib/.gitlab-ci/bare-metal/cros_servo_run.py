#!/usr/bin/env python3
#
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

import argparse
import queue
import re
from serial_buffer import SerialBuffer
import sys
import threading


class CrosServoRun:
    def __init__(self, cpu, ec):
        # Merged FIFO for the two serial buffers, fed by threads.
        self.serial_queue = queue.Queue()
        self.sentinel = object()
        self.threads_done = 0

        self.ec_ser = SerialBuffer(
            ec, "results/serial-ec.txt", "R SERIAL-EC> ")
        self.cpu_ser = SerialBuffer(
            cpu, "results/serial.txt", "R SERIAL-CPU> ")

        self.iter_feed_ec = threading.Thread(
            target=self.iter_feed_queue, daemon=True, args=(self.ec_ser.lines(),))
        self.iter_feed_ec.start()

        self.iter_feed_cpu = threading.Thread(
            target=self.iter_feed_queue, daemon=True, args=(self.cpu_ser.lines(),))
        self.iter_feed_cpu.start()

    # Feed lines from our serial queues into the merged queue, marking when our
    # input is done.
    def iter_feed_queue(self, it):
        for i in it:
            self.serial_queue.put(i)
        self.serial_queue.put(sentinel)

    # Return the next line from the queue, counting how many threads have
    # terminated and joining when done
    def get_serial_queue_line(self):
        line = self.serial_queue.get()
        if line == self.sentinel:
            self.threads_done = self.threads_done + 1
            if self.threads_done == 2:
                self.iter_feed_cpu.join()
                self.iter_feed_ec.join()
        return line

    # Returns an iterator for getting the next line.
    def serial_queue_lines(self):
        return iter(self.get_serial_queue_line, self.sentinel)

    def ec_write(self, s):
        print("W SERIAL-EC> %s" % s)
        self.ec_ser.serial.write(s.encode())

    def cpu_write(self, s):
        print("W SERIAL-CPU> %s" % s)
        self.cpu_ser.serial.write(s.encode())

    def run(self):
        # Flush any partial commands in the EC's prompt, then ask for a reboot.
        self.ec_write("\n")
        self.ec_write("reboot\n")

        # This is emitted right when the bootloader pauses to check for input.
        # Emit a ^N character to request network boot, because we don't have a
        # direct-to-netboot firmware on cheza.
        for line in self.serial_queue_lines():
            if re.search("load_archive: loading locale_en.bin", line):
                self.cpu_write("\016")
                break

            # The Cheza boards have issues with failing to bring up power to
            # the system sometimes, possibly dependent on ambient temperature
            # in the farm.
            if re.search("POWER_GOOD not seen in time", line):
                print("Detected intermittent poweron failure, restarting run...")
                return 2

        tftp_failures = 0
        for line in self.serial_queue_lines():
            if re.search("---. end Kernel panic", line):
                return 1

            # The Cheza firmware seems to occasionally get stuck looping in
            # this error state during TFTP booting, possibly based on amount of
            # network traffic around it, but it'll usually recover after a
            # reboot.
            if re.search("R8152: Bulk read error 0xffffffbf", line):
                tftp_failures += 1
                if tftp_failures >= 100:
                    print("Detected intermittent tftp failure, restarting run...")
                    return 2

            result = re.search("bare-metal result: (\S*)", line)
            if result:
                if result.group(1) == "pass":
                    return 0
                else:
                    return 1

        print("Reached the end of the CPU serial log without finding a result")
        return 1


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cpu', type=str,
                        help='CPU Serial device', required=True)
    parser.add_argument(
        '--ec', type=str, help='EC Serial device', required=True)
    args = parser.parse_args()

    servo = CrosServoRun(args.cpu, args.ec)

    while True:
        retval = servo.run()
        if retval != 2:
            break

    # power down the CPU on the device
    servo.ec_write("power off\n")

    sys.exit(retval)


if __name__ == '__main__':
    main()
