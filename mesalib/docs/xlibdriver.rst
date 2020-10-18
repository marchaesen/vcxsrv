Xlib Software Driver
====================

Mesa's Xlib driver provides an emulation of the GLX interface so that
OpenGL programs which use the GLX API can render to any X display, even
those that don't support the GLX extension. Effectively, the Xlib driver
converts all OpenGL rendering into Xlib calls.

The Xlib driver is the oldest Mesa driver and the most mature of Mesa's
software-only drivers.

Since the Xlib driver *emulates* the GLX extension, it's not totally
conformant with a true GLX implementation. The differences are fairly
obscure, however.

The unique features of the Xlib driver follows.

X Visual Selection
------------------

Mesa supports RGB(A) rendering into almost any X visual type and depth.

The glXChooseVisual function tries to choose the best X visual for the
given attribute list. However, if this doesn't suit your needs you can
force Mesa to use any X visual you want (any supported by your X server
that is) by setting the **MESA_RGB_VISUAL** and **MESA_CI_VISUAL**
environment variables. When an RGB visual is requested, glXChooseVisual
will first look if the MESA_RGB_VISUAL variable is defined. If so, it
will try to use the specified visual. Similarly, when a color index
visual is requested, glXChooseVisual will look for the MESA_CI_VISUAL
variable.

The format of accepted values is: ``visual-class depth``

Here are some examples:

::

   using csh:
       % setenv MESA_RGB_VISUAL "TrueColor 8"      // 8-bit TrueColor
       % setenv MESA_CI_VISUAL "PseudoColor 12"    // 12-bit PseudoColor
       % setenv MESA_RGB_VISUAL "PseudoColor 8"    // 8-bit PseudoColor

   using bash:
       $ export MESA_RGB_VISUAL="TrueColor 8"
       $ export MESA_CI_VISUAL="PseudoColor 12"
       $ export MESA_RGB_VISUAL="PseudoColor 8"

Double Buffering
----------------

Mesa can use either an X Pixmap or XImage as the back color buffer when
in double-buffer mode. The default is to use an XImage. The
**MESA_BACK_BUFFER** environment variable can override this. The valid
values for **MESA_BACK_BUFFER** are: **Pixmap** and **XImage** (only the
first letter is checked, case doesn't matter).

Using XImage is almost always faster than a Pixmap since it resides in
the application's address space. When glXSwapBuffers() is called,
XPutImage() or XShmPutImage() is used to transfer the XImage to the
on-screen window.

A Pixmap may be faster when doing remote rendering of a simple scene.
Some OpenGL features will be very slow with a Pixmap (for example,
blending will require a round-trip message for pixel readback.)

Experiment with the MESA_BACK_BUFFER variable to see which is faster for
your application.

Colormaps
---------

When using Mesa directly or with GLX, it's up to the application writer
to create a window with an appropriate colormap. The GLUT toolkit tries
to minimize colormap *flashing* by sharing colormaps when possible.
Specifically, if the visual and depth of the window matches that of the
root window, the root window's colormap will be shared by the Mesa
window. Otherwise, a new, private colormap will be allocated.

When sharing the root colormap, Mesa may be unable to allocate the
colors it needs, resulting in poor color quality. This can happen when a
large number of colorcells in the root colormap are already allocated.
To prevent colormap sharing in GLUT, set the **MESA_PRIVATE_CMAP**
environment variable. The value isn't significant.

Gamma Correction
----------------

To compensate for the nonlinear relationship between pixel values and
displayed intensities, there is a gamma correction feature in Mesa. Some
systems, such as Silicon Graphics, support gamma correction in hardware
(man gamma) so you won't need to use Mesa's gamma facility. Other
systems, however, may need gamma adjustment to produce images which look
correct. If you believe that Mesa's images are too dim, read on.

Gamma correction is controlled with the **MESA_GAMMA** environment
variable. Its value is of the form **Gr Gg Gb** or just **G** where Gr
is the red gamma value, Gg is the green gamma value, Gb is the blue
gamma value and G is one gamma value to use for all three channels. Each
value is a positive real number typically in the range 1.0 to 2.5. The
defaults are all 1.0, effectively disabling gamma correction. Examples:

::

   % export MESA_GAMMA="2.3 2.2 2.4"  // separate R,G,B values
   % export MESA_GAMMA="2.0"       // same gamma for R,G,B

The ``demos/gamma.c`` program in mesa/demos repository may help you to
determine reasonable gamma value for your display. With correct gamma
values, the color intensities displayed in the top row (drawn by
dithering) should nearly match those in the bottom row (drawn as grays).

Alex De Bruyn reports that gamma values of 1.6, 1.6 and 1.9 work well on
HP displays using the HP-ColorRecovery technology.

Mesa implements gamma correction with a lookup table which translates a
"linear" pixel value to a gamma-corrected pixel value. There is a small
performance penalty. Gamma correction only works in RGB mode. Also be
aware that pixel values read back from the frame buffer will not be
"un-corrected" so glReadPixels may not return the same data drawn with
glDrawPixels.

For more information about gamma correction, see the `Wikipedia
article <https://en.wikipedia.org/wiki/Gamma_correction>`__

Overlay Planes
--------------

Hardware overlay planes are supported by the Xlib driver. To determine
if your X server has overlay support you can test for the
SERVER_OVERLAY_VISUALS property:

::

   xprop -root | grep SERVER_OVERLAY_VISUALS

HPCR Dithering
--------------

If you set the **MESA_HPCR_CLEAR** environment variable then dithering
will be used when clearing the color buffer. This is only applicable to
HP systems with the HPCR (Color Recovery) feature. This incurs a small
performance penalty.

Extensions
----------

The following Mesa-specific extensions are implemented in the Xlib
driver.

GLX_MESA_pixmap_colormap
~~~~~~~~~~~~~~~~~~~~~~~~

This extension adds the GLX function:

::

   GLXPixmap glXCreateGLXPixmapMESA( Display *dpy, XVisualInfo *visual,
                                     Pixmap pixmap, Colormap cmap )

It is an alternative to the standard glXCreateGLXPixmap() function.
Since Mesa supports RGB rendering into any X visual, not just True-
Color or DirectColor, Mesa needs colormap information to convert RGB
values into pixel values. An X window carries this information but a
pixmap does not. This function associates a colormap to a GLX pixmap.
See the xdemos/glxpixmap.c file for an example of how to use this
extension.

`GLX_MESA_pixmap_colormap
specification <specs/MESA_pixmap_colormap.spec>`__

GLX_MESA_release_buffers
~~~~~~~~~~~~~~~~~~~~~~~~

Mesa associates a set of ancillary (depth, accumulation, stencil and
alpha) buffers with each X window it draws into. These ancillary buffers
are allocated for each X window the first time the X window is passed to
glXMakeCurrent(). Mesa, however, can't detect when an X window has been
destroyed in order to free the ancillary buffers.

The best it can do is to check for recently destroyed windows whenever
the client calls the glXCreateContext() or glXDestroyContext()
functions. This may not be sufficient in all situations though.

The GLX_MESA_release_buffers extension allows a client to explicitly
deallocate the ancillary buffers by calling glxReleaseBuffersMESA() just
before an X window is destroyed. For example:

::

   #ifdef GLX_MESA_release_buffers
      glXReleaseBuffersMESA( dpy, window );
   #endif
   XDestroyWindow( dpy, window );

`GLX_MESA_release_buffers
specification <specs/MESA_release_buffers.spec>`__

This extension was added in Mesa 2.0.

GLX_MESA_copy_sub_buffer
~~~~~~~~~~~~~~~~~~~~~~~~

This extension adds the glXCopySubBufferMESA() function. It works like
glXSwapBuffers() but only copies a sub-region of the window instead of
the whole window.

`GLX_MESA_copy_sub_buffer
specification <specs/MESA_copy_sub_buffer.spec>`__

This extension was added in Mesa 2.6

Summary of X-related environment variables
------------------------------------------

::

   MESA_RGB_VISUAL - specifies the X visual and depth for RGB mode (X only)
   MESA_CI_VISUAL - specifies the X visual and depth for CI mode (X only)
   MESA_BACK_BUFFER - specifies how to implement the back color buffer (X only)
   MESA_PRIVATE_CMAP - force aux/tk libraries to use private colormaps (X only)
   MESA_GAMMA - gamma correction coefficients (X only)
