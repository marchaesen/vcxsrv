#!/usr/bin/env python
# encoding=utf-8
# Copyright © 2017-2018 Intel Corporation

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

    if os.path.isabs(args.libdir):
        to = os.path.join(os.environ.get('DESTDIR', '/'), args.libdir[1:])
    else:
        to = os.path.join(os.environ['MESON_INSTALL_DESTDIR_PREFIX'], args.libdir)

    master = os.path.join(to, os.path.basename(args.megadriver))

    if not os.path.exists(to):
        os.makedirs(to)
    shutil.copy(args.megadriver, master)

    for driver in args.drivers:
        abs_driver = os.path.join(to, driver)

        if os.path.exists(abs_driver):
            os.unlink(abs_driver)
        print('installing {} to {}'.format(args.megadriver, abs_driver))
        os.link(master, abs_driver)

        try:
            ret = os.getcwd()
            os.chdir(to)

            name, ext = os.path.splitext(driver)
            while ext != '.so':
                if os.path.exists(name):
                    os.unlink(name)
                os.symlink(driver, name)
                name, ext = os.path.splitext(name)
        finally:
            os.chdir(ret)
    os.unlink(master)


if __name__ == '__main__':
    main()
