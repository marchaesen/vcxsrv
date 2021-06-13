#!/usr/bin/env python3

# Copyright © 2021 Igalia, S.L.
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
import fcntl
import os
import telnetlib
import time

class Telnet:
    def __init__(self, args):
        self.retries = 3
        self.lock_file = "/var/run/poe.lock"
        self.debug = args.debug
        self.lock = None
        self.tn = None
        try:
            self.host = os.environ['BM_POE_ADDRESS']
            self.username = os.environ['BM_POE_USERNAME'].encode('ascii')
            self.password = os.environ['BM_POE_PASSWORD'].encode('ascii')
        except KeyError as k:
            raise OSError("envvar " + str(k) + " undefined")

    def login(self):
        # Sometimes login fails; retry 3 times before aborting
        logged = False
        for retry in range(self.retries):
            self.lock = open(self.lock_file, 'w')
            fcntl.flock(self.lock,  fcntl.LOCK_EX)
            self.tn = telnetlib.Telnet(self.host)
            self.tn.set_debuglevel(1 if self.debug else 0)
            self.tn.read_until(b'Password:')
            self.tn.write(self.username + b'\t' + self.password + b'\r')
            w = self.tn.read_until(b'Back', 3).decode('ascii')
            if w.endswith("Back"):
                logged = True
                break
            self.tn.close()
            self.lock.close()
            time.sleep(3)

        if not logged:
            raise OSError("Can not log in")

        self.tn.write(b'\x01')
        self.tn.read_until(b'>')
        self.tn.write(b'lcli\r')
        self.tn.read_until(b'User Name:')
        self.tn.write(self.username + b'\r')
        self.tn.read_until(b'Password:')
        self.tn.write(self.password + b'\r')
        self.tn.read_until(b'#')

    def logout(self):
        self.lock.close()
        self.tn.close()

    def select_port(self, port):
        self.tn.write(b'configure\r')
        self.tn.read_until(b'#')

        self.tn.write(b'interface GE ' + str(port).encode('ascii') + b'\r')
        self.tn.read_until(b'#')

    def poe_on(self):
        self.tn.write(b'power inline auto\r')
        self.tn.read_until(b'#')

    def poe_off(self):
        self.tn.write(b'power inline never\r')
        self.tn.read_until(b'#')

    def poe_reset(self):
        self.poe_off()
        time.sleep(3)
        self.poe_on()

def main():
    parser = argparse.ArgumentParser(description='Powers on/off switch port')
    parser.add_argument('-d', '--debug',
                        action='store_true',
                        help='Enable debug')
    parser.add_argument('port',
                        type=int,
                        help='Port to turn on/off')
    parser.add_argument('operation',
                        choices=['on', 'off', 'reset'],
                        help='Operation to perform')
    args = parser.parse_args()

    try:
        telnet = Telnet(args)

        telnet.login()

        telnet.select_port(args.port)
        if args.operation == "on":
            telnet.poe_on()
        elif args.operation == "off":
            telnet.poe_off()
        elif args.operation == "reset":
            telnet.poe_reset()

        telnet.logout()
    except Exception as e:
        print("Error! " + str(e))

if __name__ == '__main__':
    main()
