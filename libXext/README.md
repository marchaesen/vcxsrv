libXext - library for common extensions to the X11 protocol
-----------------------------------------------------------

libXext is the historical libX11-based catchall library for the X11
extensions without their own libraries.

No new extensions should be added to this library - it is now instead
preferred to make per-extension libraries that can be evolved as needed
without breaking compatibility of this core library.

All questions regarding this software should be directed at the
Xorg mailing list:

  https://lists.x.org/mailman/listinfo/xorg

The master development code repository can be found at:

  https://gitlab.freedesktop.org/xorg/lib/libXext

Please submit bug reports and requests to merge patches there.

For patch submission instructions, see:

  https://www.x.org/wiki/Development/Documentation/SubmittingPatches

