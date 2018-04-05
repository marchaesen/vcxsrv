#!/usr/bin/env python
# encoding=utf-8
# Copyright © 2017 Intel Corporation

# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:

# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.

# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""Script to install megadriver symlinks for meson."""

from __future__ import print_function
import argparse
import os
import shutil


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('megadriver')
    parser.add_argument('libdir')
    parser.add_argument('drivers', nargs='+')
    args = parser.parse_args()

    to = os.path.join(os.environ.get('MESON_INSTALL_DESTDIR_PREFIX'), args.libdir)
    master = os.path.join(to, os.path.basename(args.megadriver))

    if not os.path.exists(to):
        os.makedirs(to)
    shutil.copy(args.megadriver, master)

    for each in args.drivers:
        driver = os.path.join(to, each)

        if os.path.exists(driver):
            os.unlink(driver)
        print('installing {} to {}'.format(args.megadriver, driver))
        os.link(master, driver)

        try:
            ret = os.getcwd()
            os.chdir(to)

            name, ext = os.path.splitext(each)
            while ext != '.so':
                if os.path.exists(name):
                    os.unlink(name)
                os.symlink(each, name)
                name, ext = os.path.splitext(name)
        finally:
            os.chdir(ret)
    os.unlink(master)


if __name__ == '__main__':
    main()
