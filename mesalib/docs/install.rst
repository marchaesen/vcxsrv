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
   is used when building ARC.

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

3. Running against a local build
--------------------------------

It's often necessary or useful when debugging driver issues or testing new
branches to run against a local build of Mesa without doing a system-wide
install.  To do this, choose a temporary location for the install.  A directory
called ``installdir`` inside your mesa tree is as good as anything.  All of the
commands below will assume ``$MESA_INSTALLDIR`` is an absolute path to this
location.

First, configure Mesa and install in the temporary location:

.. code-block:: console

   meson builddir/ -Dprefix="$MESA_INSTALLDIR" OTHER_OPTIONS
   ninja -C builddir/ install

where ``OTHER_OPTIONS`` is replaced by any meson configuration options you may
want.  For instance, if you want to build the LLVMpipe drivers, it would look
like this:

.. code-block:: console

   meson builddir/ -Dprefix="$MESA_INSTALLDIR" -Ddri-drivers= \
      -Dgallium-drivers=swrast -Dvulkan-drivers=swrast
   ninja -C builddir/ install

Once Mesa has built and installed to ``$MESA_INSTALLDIR``, you can run any app
against your temporary install by setting the right environment variables.
Which variable you have to set depends on the API.

OpenGL
~~~~~~

.. code-block:: console

   LD_LIBRARY_PATH="$MESA_INSTALLDIR/lib64" glxinfo

You may need to use ``lib`` instead of ``lib64`` on some systems or a full
library specifier on debian.  Look inside ``installdir`` for the directory that
contains ``libGL.so`` and use that one.

Vulkan
~~~~~~

.. code-block:: console

   VK_ICD_FILENAMES="$MESA_INSTALLDIR/share/vulkan/icd/my_icd.json" vulkaninfo

where ``my_icd.json`` is replaced with the actual ICD json file name.  This
will depend on your driver.  For instance, the 64-bit lavapipe driver ICD file
is named ``lvp_icd.x86_64.json``.

OpenCL
~~~~~~

.. code-block:: console

   OCL_ICD_VENDORS="$MESA_INSTALLDIR/etc/OpenCL/vendors" clinfo

Unlike Vulkan, OpenCL takes a path to the whole ``vendors`` folder and will
enumerate any drivers found there.

Troubleshooting local builds
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you are trying to run an app against a local build and it's not working,
here are a few things to check:

 1. Double-check your paths and try with the simplest app you can.  Before
    banging your head on a Steam game, make sure your path works with
    ``glxgears`` first.

 2. Watch out for wrapper scripts.  Some more complex apps such as games have
    big start-up scripts.  Sometimes those scripts scrub the environment or set
    ``LD_LIBRARY_PATH`` to something in the game's install directory.

 3. Is your Mesa build the same arch as your app?  Lots of games are still
    32-bit and your Mesa build is probably 64-bit by default.

 4. 32 and 64-bit builds in the same local install directory doesn't typically
    work.  Distros go to great lengths to make this work in your system install
    and it's hard to get it right for a local install.  If you've recently
    built 64-bit and are now building 32-bit, throw away the install directory
    first to prevent conflicts.

4. Building with AOSP (Android)
-------------------------------

<TODO>

5. Library Information
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

6. Building OpenGL programs with pkg-config
-------------------------------------------

Running ``ninja install`` will install package configuration files for
the pkg-config utility.

When compiling your OpenGL application you can use pkg-config to
determine the proper compiler and linker flags.

For example, compiling and linking a GLUT application can be done with:

.. code-block:: console

      gcc `pkg-config --cflags --libs glut` mydemo.c -o mydemo
