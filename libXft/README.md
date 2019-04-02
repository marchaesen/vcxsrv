libXft - X FreeType library
---------------------------

libXft is the client side font rendering library, using libfreetype,
libX11, and the X Render extension to display anti-aliased text.

Xft version 2.1 was the first stand alone release of Xft, a library that
connects X applications with the FreeType font rasterization library. Xft
uses fontconfig to locate fonts so it has no configuration files.

Before building Xft you will need to have installed:
 - FreeType                             https://freetype.org/
 - Fontconfig                           https://fontconfig.org/
 - libX11, libXext, & libXrender        https://x.org/

All questions regarding this software should be directed at the
Xorg mailing list:

  https://lists.x.org/mailman/listinfo/xorg

The master development code repository can be found at:

  https://gitlab.freedesktop.org/xorg/lib/libXft

Please submit bug reports and requests to merge patches there.

For patch submission instructions, see:

  https://www.x.org/wiki/Development/Documentation/SubmittingPatches

To release a version of this library:

 1. Update the version number in configure.ac
 2. Fix the NEWS file  
    Change version number  
    Set the date  
    add highlights
 3. Commit those changes
 4. rebuild the configuration files with autogen.sh  
    sh autogen.sh --sysconfdir=/etc --prefix=/usr --mandir=/usr/share/man
 5. Follow the steps listed in
    https://www.x.org/wiki/Development/Documentation/ReleaseHOWTO/

Keith Packard  
keithp@keithp.com


