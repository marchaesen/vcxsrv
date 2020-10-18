#!/usr/bin/env python3

import os
import sys
import argparse

if __name__=='__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('availpath')
    parser.add_argument('confpath')
    parser.add_argument('links', nargs='+')
    args = parser.parse_args()

    confpath = os.path.join(os.environ['MESON_INSTALL_DESTDIR_PREFIX'], args.confpath)

    if not os.path.exists(confpath):
        os.makedirs(confpath)

    for link in args.links:
        src = os.path.join(args.availpath, link)
        dst = os.path.join(confpath, link)
        try:
            os.symlink(src, dst)
        except NotImplementedError:
            # Not supported on this version of Windows
            break
        except OSError as e:
            # Symlink privileges are not available
            if len(e.args) == 1 and 'privilege' in e.args[0]:
                break
            raise
        except FileExistsError:
            pass
