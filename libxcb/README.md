About libxcb
============

libxcb provides an interface to the X Window System protocol, which
replaces the traditional Xlib interface. It has several advantages over
Xlib, including:
- size: small, simple library, and lower memory footprint
- latency hiding: batch several requests and wait for the replies later
- direct protocol access: interface and protocol correspond exactly
- proven thread support: transparently access XCB from multiple threads
- easy extension implementation: interfaces auto-generated from XML-XCB

Xlib also uses XCB as a transport layer, allowing software to make
requests and receive responses with both, which eases porting to XCB.
However, client programs, libraries, and toolkits will gain the most
benefit from a native XCB port.

More information about xcb is available from our website:

  https://xcb.freedesktop.org/

Please report any issues you find to the freedesktop.org bug tracker at:

  https://gitlab.freedesktop.org/xorg/lib/libxcb/issues

Discussion about XCB occurs on the XCB mailing list:

  https://lists.freedesktop.org/mailman/listinfo/xcb

You can obtain the latest development versions of XCB using GIT from
the libxcb code repository at:

  https://gitlab.freedesktop.org/xorg/lib/libxcb

  For anonymous checkouts, use:

    git clone https://gitlab.freedesktop.org/xorg/lib/libxcb.git

  For developers, use:

    git clone git@gitlab.freedesktop.org:xorg/lib/libxcb.git
