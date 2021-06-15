Compiling and Installing
========================

.. toctree::
   :maxdepth: 1
   :hidden:

   meson

1. Prerequisites for building
-----------------------------

1.1 General
~~~~~~~~~~~

Build system
^^^^^^^^^^^^

-  `Meson <https://mesonbuild.com>`__ is required when building on \*nix
   platforms and on Windows.
-  Android Build system when building as native Android component. Meson
   is used when when building ARC.

Compiler
^^^^^^^^

The following compilers are known to work, if you know of others or
you're willing to maintain support for other compiler get in touch.

-  GCC 4.2.0 or later (some parts of Mesa may require later versions)
-  Clang - exact minimum requirement is currently unknown.
-  Microsoft Visual Studio 2015 or later is required, for building on
   Windows.

Third party/extra tools.
^^^^^^^^^^^^^^^^^^^^^^^^

-  `Python <https://www.python.org/>`__ - Python 3.5 or newer is required.
-  `Python Mako module <http://www.makotemplates.org/>`__ - Python Mako
   module is required. Version 0.8.0 or later should work.
-  Lex / Yacc - for building the Mesa IR and GLSL compiler.

   On Linux systems, Flex and Bison versions 2.5.35 and 2.4.1,
   respectively, (or later) should work. On Windows with MinGW, install
   Flex and Bison with:

   .. code-block:: console

      mingw-get install msys-flex msys-bison

   For MSVC on Windows, install `Win
   flex-bison <http://winflexbison.sourceforge.net/>`__.

.. note::

   Some versions can be buggy (e.g. Flex 2.6.2) so do try others
   if things fail.

1.2 Requirements
~~~~~~~~~~~~~~~~

The requirements depends on the features selected at configure stage.
Check/install the respective development package as prompted by the
configure error message.

Here are some common ways to retrieve most/all of the dependencies based
on the packaging tool used by your distro.

.. code-block:: console

     zypper source-install --build-deps-only Mesa # openSUSE/SLED/SLES
     yum-builddep mesa # yum Fedora, OpenSuse(?)
     dnf builddep mesa # dnf Fedora
     apt-get build-dep mesa # Debian and derivatives
     ... # others

2. Building with meson
----------------------

**Meson >= 0.46.0 is required**

Meson is the latest build system in mesa, it is currently able to build
for \*nix systems like Linux and BSD, macOS, Haiku, and Windows.

The general approach is:

.. code-block:: console

     meson builddir/
     ninja -C builddir/
     sudo ninja -C builddir/ install

On Windows you can also use the Visual Studio backend

.. code-block:: console

     meson builddir --backend=vs
     cd builddir
     msbuild mesa.sln /m

Please read the :doc:`detailed meson instructions <meson>` for more
information

3. Building with AOSP (Android)
-------------------------------

Currently one can build Mesa for Android as part of the AOSP project,
yet your experience might vary.

In order to achieve that one should update their local manifest to point
to the upstream repo, set the appropriate BOARD_GPU_DRIVERS and build
the libGLES_mesa library.

FINISHME: Improve on the instructions add references to Rob H
repos/Jenkins, Android-x86 and/or other resources.

4. Library Information
----------------------

When compilation has finished, look in the top-level ``lib/`` (or
``lib64/``) directory. You'll see a set of library files similar to
this:

.. code-block:: console

   lrwxrwxrwx    1 brian    users          10 Mar 26 07:53 libGL.so -> libGL.so.1*
   lrwxrwxrwx    1 brian    users          19 Mar 26 07:53 libGL.so.1 -> libGL.so.1.5.060100*
   -rwxr-xr-x    1 brian    users     3375861 Mar 26 07:53 libGL.so.1.5.060100*
   lrwxrwxrwx    1 brian    users          14 Mar 26 07:53 libOSMesa.so -> libOSMesa.so.6*
   lrwxrwxrwx    1 brian    users          23 Mar 26 07:53 libOSMesa.so.6 -> libOSMesa.so.6.1.060100*
   -rwxr-xr-x    1 brian    users       23871 Mar 26 07:53 libOSMesa.so.6.1.060100*

**libGL** is the main OpenGL library (i.e. Mesa), while **libOSMesa** is
the OSMesa (Off-Screen) interface library.

If you built the DRI hardware drivers, you'll also see the DRI drivers:

.. code-block:: console

   -rwxr-xr-x   1 brian users 16895413 Jul 21 12:11 i915_dri.so
   -rwxr-xr-x   1 brian users 16895413 Jul 21 12:11 i965_dri.so
   -rwxr-xr-x   1 brian users 11849858 Jul 21 12:12 r200_dri.so
   -rwxr-xr-x   1 brian users 11757388 Jul 21 12:12 radeon_dri.so

If you built with Gallium support, look in lib/gallium/ for
Gallium-based versions of libGL and device drivers.

5. Building OpenGL programs with pkg-config
-------------------------------------------

Running ``ninja install`` will install package configuration files for
the pkg-config utility.

When compiling your OpenGL application you can use pkg-config to
determine the proper compiler and linker flags.

For example, compiling and linking a GLUT application can be done with:

.. code-block:: console

      gcc `pkg-config --cflags --libs glut` mydemo.c -o mydemo
