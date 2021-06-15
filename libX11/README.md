# libX11 - Core X11 protocol client library

Documentation for this library can be found in the included man pages,
and in the Xlib spec from the specs subdirectory, also available at:

 - https://www.x.org/releases/current/doc/libX11/libX11/libX11.html

 - https://www.x.org/releases/current/doc/libX11/libX11/libX11.pdf

and the O'Reilly Xlib books, which they have made freely available online,
though only for older versions of X11:

 - X Series Volume 2: Xlib Reference Manual (1989, covers X11R3)
   https://www.archive.org/details/xlibretmanver1102nyemiss

 - X Series Volume 2: Xlib Reference Manual, 2nd Edition (1990, covers X11R4)
   https://www.archive.org/details/xlibrefmanv115ed02nyemiss

All questions regarding this software should be directed at the
Xorg mailing list:

  https://lists.x.org/mailman/listinfo/xorg

The primary development code repository can be found at:

  https://gitlab.freedesktop.org/xorg/lib/libX11

Please submit bug reports and requests to merge patches there.

For patch submission instructions, see:

  https://www.x.org/wiki/Development/Documentation/SubmittingPatches

## Release 1.7.2

This is a bug fix release, correcting a regression introduced by and
improving the checks from the fix for CVE-2021-31535.

## Release 1.7.1

This is a bug fix release, including a security fix for
CVE-2021-31535, nls and documentation corrections.

 * Reject string longer than USHRT_MAX before sending them on the wire
 * Fix out-of-bound access in KeySymToUcs4()
 * nls: allow composing all breved letters also with a lowercase "u"
 * nls: add 'C.utf8' as an alias for 'en_US.UTF-8'
 * Nroff code fixes
 * Comments fixes

## Release 1.7.0

Version 1.7.0 includes a new API, hence the change from the 1.6 series
to 1.7:

 * XSetIOErrorExitHandler which provides a mechanism for applications
   to recover from I/O error conditions instead of being forced to
   exit. Thanks to Carlos Garnacho for this.

This release includes a bunch of bug fixes, some which have been pending for over three years:

 * A bunch of nls cleanups to remove obsolete entries and clean up
   formatting of the ist. Thanks to Benno Schulenberg for these.

 * Warning fixes and other cleanups across a huge swath of the
   library. Thanks to Alan Coopersmith for these.

 * Memory allocation bugs, including leaks and use after free in the
   locale code. Thanks to Krzesimir Nowak, Jacek Caban and Vittorio
   Zecca for these.

 * Thread safety fixes in the locale code. Thanks to Jacek Caban for
   these.

 * poll_for_response race condition fix. Thanks to Frediano Ziglio for
   the bulk of this effort, and to Peter Hutterer for careful review
   and improvements.

Version 1.7.0 includes a couple of new locales:

 * ia and ie locales. Thanks to Carmina16 for these.

There are also numerous compose entries added, including:

 * |^ or ^| for ↑, |v or v| for ↓, ~~ for ≈. Thanks to Antti
    Savolainen for this.

 * Allowing use of 'v' for caron, in addition to 'c', so things like
   vC for Č, vc for č. Thanks to Benno Schulenberg for this.

 * Compose sequences LT, lt for '<', and GT, gt for '>' for keyboards
   where those are difficult to access. Thanks to Jonathan Belsewir
   for this.
