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

## Release 1.8.10

 * Re-fix XIM input sometimes jumbled (#205, #206, #207, #208, !246)
 * Fix various static analysis errors (!250)
 * Add compose sequences for Arabic hamza (!218), Ezh (!221), and
   hryvnia currency (!259)
 * Make colormap private interfaces thread safe (#215, !254)
 * Fix deadlock in XRebindKeysym() (!256)
 * Assorted memory handling cleanups (!251, !258)
 * Restore VAX support still in use by NetBSD (!257)

## Release 1.8.9

 * Fix regressions introduced in 1.8.8 (!245, !248) - this includes reverting
   for now the previous "Fix XIM input sometimes jumbled (#198, !236)"

## Release 1.8.8

 * Fix XIM input sometimes jumbled (#198, !236)
 * Fix _XkbReadGetDeviceInfoReply for nButtons == dev->buttons (!237)
 * Drop ifdefs for platforms that are no longer supported (!242, !243)
 * Assorted memory handling cleanups

## Release 1.8.7

 * Security fixes and hardening in XImage and pixmap handling code
   (CVE-2023-43786, CVE-2023-43787, !234)
 * Fix buffer allocation in _XkbReadKeySyms() (CVE-2023-43785)
 * Fail XOpenDisplay() if server-provided default visual is invalid (!233)
 * Bring XKB docs in line with actual implementation (!231, !228)
 * Xutil.h: declare XEmptyRegion() and XEqualRegion() as Bool (!225)
 * Assorted updates to en_US.UTF-8 compose keys (!213, !214, !215, !216,
   !217, !219, !220, !222, !223, !226, !227, !229)

## Release 1.8.6

 * Add bounds checks in InitExt.c (CVE-2023-3138)

## Release 1.8.5

 * autoconf & libtool updates (!187, !188)
 * Restore missing text in XSetScreenSaver man page (#187, !203)
 * Update am_ET.UTF-8 compose keys to use dead-vowel symbols,
   in coordination with xkeyboard-config 2.39 (!205)
 * Assorted updates to en_US.UTF-8 compose keys (!189, !195, !196, !198,
   !199, !200, !201, !207, !208, !209)

## Release 1.8.4

 * Revert AddressSanitizer fix from 1.8.3 that caused regression (#176, !180)
 * Add two compose sequences for "capital B with stroke", remove others (!179)
 * Further improved handling of reentering libX11 via X*IfEvent() calls (!176)


## Release 1.8.3

 * Improved handling of reentering libX11 via X*IfEvent() calls (!171, !173)
 * Fix loading of en_US.UTF-8/XLC_LOCALE (#167, !174)
 * Add XFreeThreads() and automatic call from a destructor function when
   thread-safety-constructor is enabled (!167).
 * Address issues found by UBSan and AddressSanitizer
 * Fix build with older gcc versions (!169)

## Release 1.8.2

 * Allow X*IfEvent() to reenter libX11 to avoid deadlock from unsafe
   calls when thread-safety-constructor is enabled (!150).
 * Remove Xlib's pthread function stubs - instead use system provided
   threads functions, including linking against any needed pthread
   libraries if thread-safety-constructor is enabled (!155, !156).
 * Fix off-by-one error in XKeycodeToKeysym for indexes > 3 (!78).
 * Allow XNSpotLocation with OnTheSpot (!127).
 * Fix Win32 build when -fno-common is in effect (!140).
 * Fix memory leak in XRegisterIMInstantiateCallback (!158).
 * Add compose sequences for the double-struck capitals ℕ ℤ ℚ ℝ ℂ (!144),
   the Samogitian E with dot above and macron (!147), Unicode minus sign (!163).
 * Change <Compose> <^> <-> to mean superscript minus instead of macron (!162).
 * Delete compose sequences that mix top-row digits with numpad digits (!139)
   or mix upper & lower case letters (!144).
 * Delete some unuseful compose sequences meant for Bépo layout (!146).
 * Delete compose sequences using leftcaret & rightcaret keysyms (!163).
 * Remove KOI8-R character set from en_US.UTF-8/XLC_LOCALE (!148).
 * Map sr locales to sr_RS compose files (!161).

## Release 1.8.1

 * Fix --enable-thread-safety-constructor configure option

## Release 1.8

 * Add --enable-thread-safety-constructor configure option (default: enabled)
   to call XInitThreads() from the library's constructor, thus enabling
   thread-safety and locking by default.  This may expose bugs in clients
   which did not follow documented rules for calling libX11 functions.
 * Fix Ethopian (am_ET.UTF-8) compose sequences.
 * Remove 8 compose sequences that generated the input symbols.
 * Add compose seuences for abovedot (\<period\> \<space\>),
   diaeresis (\<quotedbl\> \<space\>), and ogonek (\<semicolon\> \<space\>).

## Release 1.7.5

 * Avoids a segfault when an invalid name is used for opening a display.

## Release 1.7.4

 * Fixes the "Unknown sequence number" error by allowing backward jumps
   in the sequence number when widening it.
 * Any changes to virtual modifiers get propagated properly.
 * Greek case-conversion tables were updated to Unicode Data 14.0.
 * Compose sequences for  ☮  🄯  ⇐  ⇑  ⇓  were added,
   being the following: OY, ()), =<, =^, and =v.
 * Hammer-and-sickle can be composed with question mark plus backslash.

## Release 1.7.3

 * Fixes a hanging issue in _XReply() where the replying thread would
   wait for an event when another thread was already waiting for one.
 * Avoids a crash when the X connection gets broken while closing down.

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

This release includes a bunch of bug fixes, some of which have been
pending for over three years:

 * A bunch of nls cleanups to remove obsolete entries and clean up
   formatting of the list. Thanks to Benno Schulenberg for these.

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
