I. OVERVIEW
-----------

The xauth program is used to edit and display the authorization
information used in connecting to the X server.
The underlying "Authorization Protocol for X" is described in the
README file of the libXau module of X11.

II. BUILDING
------------

Use "./autogen.sh" to configure the package and "make" to compile it.
A black box check for the correctness of the package can be initiated
by "make check" (make sure to install "cmdtest" from
http://liw.fi/cmdtest/). The installation is done by "make install".

III. COMMUNICATION
------------------

All questions regarding this software should be directed at the
Xorg mailing list:

  https://lists.x.org/mailman/listinfo/xorg

The master development code repository can be found at:

  https://gitlab.freedesktop.org/xorg/app/xauth

Please submit bug reports and requests to merge patches there.

For patch submission instructions, see:

  https://www.x.org/wiki/Development/Documentation/SubmittingPatches

IV. RELEASING
-------------

This section describes how to release a new version of xauth to the
public. A detailed description of this process can be found at
https://www.x.org/wiki/Development/Documentation/ReleaseHOWTO with a
few clarification below.

Remember, that the last commit _must_ include the version string in
its diff (not the commit message). This is typically done by
incrementing the version string in configure.ac.

For releasing under Fedora make sure, that
/usr/share/util-macros/INSTALL exists. If not, then please create that
file.

To release a new version of xauth, please follow this steps:

 * git clone ssh://git.freedesktop.org/git/xorg/app/xauth
 * cd xauth ; ./autogen.sh ; make ; make check
 * follow ReleaseHowto inside this directory.

Ignore these errors shown during release.sh:

    /bin/sh: ../.changelog.tmp: Permission denied
    git directory not found: installing possibly empty changelog.
    
    cp: cannot create regular file '../.INSTALL.tmp': Permission denied
    util-macros "pkgdatadir" from xorg-macros.pc not found: installing possibly empty INSTALL.

[eof]
