libXmu - X miscellaneous utility routines library
-------------------------------------------------

This library contains miscellaneous utilities and is not part of the Xlib
standard.  It contains routines which only use public interfaces so that it
may be layered on top of any proprietary implementation of Xlib or Xt.

It is intended to support clients in the X.Org distribution; vendors
may choose not to distribute this library if they wish.  Therefore,
applications developers who depend on this library should be prepared to
treat it as part of their software base when porting.

API documentation for this library is provided in the docs directory in
DocBook format.  If xmlto is installed, it will be converted to supported
formats and installed in $(docdir) unless --disable-docs is passed to
configure.

All questions regarding this software should be directed at the
Xorg mailing list:

  https://lists.x.org/mailman/listinfo/xorg

The master development code repository can be found at:

  https://gitlab.freedesktop.org/xorg/lib/libXmu

Please submit bug reports and requests to merge patches there.

For patch submission instructions, see:

  https://www.x.org/wiki/Development/Documentation/SubmittingPatches

