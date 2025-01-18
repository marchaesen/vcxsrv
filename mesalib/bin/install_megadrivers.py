#!/usr/bin/env python3
# encoding=utf-8
# Copyright 2017-2018 Intel Corporation

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

import argparse
import os


def resolve_libdir(libdir):
    if os.path.isabs(libdir):
        destdir = os.environ.get('DESTDIR')
        if destdir:
            return os.path.join(destdir, libdir[1:])
        else:
            return libdir
    return os.path.join(os.environ['MESON_INSTALL_DESTDIR_PREFIX'], libdir)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('megadriver')
    parser.add_argument('libdir')
    parser.add_argument('drivers', nargs='+')
    parser.add_argument('--megadriver-libdir')
    parser.add_argument('--libname-suffix', required=True)
    args = parser.parse_args()

    # Not neccesarily at the end, there might be a version suffix, but let's
    # make sure that the same suffix is in the megadriver lib name.
    assert '.' + args.libname_suffix in args.megadriver

    to = resolve_libdir(args.libdir)
    if args.megadriver_libdir:
        md_to = resolve_libdir(args.megadriver_libdir)
    else:
        md_to = to

    basename = os.path.basename(args.megadriver)
    master = os.path.join(to, basename)

    if not os.path.exists(to):
        if os.path.lexists(to):
            os.unlink(to)
        os.makedirs(to)

    for driver in args.drivers:
        abs_driver = os.path.join(to, driver)

        if os.path.lexists(abs_driver):
            os.unlink(abs_driver)

        symlink = os.path.relpath(os.path.join(md_to, basename), start=to)

        print(f'Installing symlink pointing to {symlink} to {abs_driver}')
        os.symlink(symlink, abs_driver)

        try:
            ret = os.getcwd()
            os.chdir(to)

            name, ext = os.path.splitext(driver)
            while ext != '.' + args.libname_suffix:
                if os.path.lexists(name):
                    os.unlink(name)
                os.symlink(driver, name)
                name, ext = os.path.splitext(name)
        finally:
            os.chdir(ret)

    # Remove meson-created symlinks
    name, ext = os.path.splitext(master)
    while ext != '.' + args.libname_suffix:
        if os.path.lexists(name):
            os.unlink(name)
        name, ext = os.path.splitext(name)


if __name__ == '__main__':
    main()
