Low Resolution Z Buffer
=======================

This doc is based on a6xx HW reverse engineering, a5xx should be similar to
a6xx before gen3.

Low Resolution Z buffer is very similar to a depth prepass that helps
the HW to avoid executing the fragment shader on those fragments that will
be subsequently discarded by the depth test afterwards.

The interesting part of this feature is that it allows applications
to submit the vertices in any order.

Citing official Adreno documentation:

::

  [A Low Resolution Z (LRZ)] pass is also referred to as draw order independent
  depth rejection. During the binning pass, a low resolution Z-buffer is constructed,
  and can reject LRZ-tile wide contributions to boost binning performance. This LRZ
  is then used during the rendering pass to reject pixels efficiently before testing
  against the full resolution Z-buffer.

Limitations
-----------

There are two main limitations of LRZ:

- Since LRZ is an early depth test, such test cannot be used when late-z is required;
- LRZ buffer could be formed only in one direction, changing depth comparison directions
  without disabling LRZ would lead to a malformed LRZ buffer.

Pre-a650 (before gen3)
----------------------

The direction is fully tracked on CPU. In render pass LRZ starts with
unknown direction, the direction is set first time when depth write occurs
and if it does change afterwards then the direction becomes invalid and LRZ is
disabled for the rest of the render pass.

Since the direction is not tracked by the GPU, it's impossible to know whether
LRZ is enabled during construction of secondary command buffers.

For the same reason, it's impossible to reuse LRZ between render passes.

A650+ (gen3+)
-------------

Now LRZ direction can be tracked on GPU. There are two parts:

- Direction byte which stores current LRZ direction - ``GRAS_LRZ_CNTL.DIR``.
- Parameters of the last used depth view - ``GRAS_LRZ_DEPTH_VIEW``.

The idea is the same as when LRZ tracked on CPU: when ``GRAS_LRZ_CNTL``
is used, its direction is compared to the previously known direction
and direction byte is set to disabled when directions are incompatible.

Additionally, to reuse LRZ between render passes, ``GRAS_LRZ_CNTL`` checks
if the current value of ``GRAS_LRZ_DEPTH_VIEW`` is equal to the value
stored in the buffer. If not, LRZ is disabled. This is necessary
because depth buffer may have several layers and mip levels, while the
LRZ buffer represents only a single layer + mip level.

A7XX
-------------

A7XX introduces the concept of bidirectional LRZ where there are two LRZ
buffers, one for each direction. This way LRZ doesn't need to be disabled
when the direction changes, by default, this behavior is disabled but the
LRZ buffers have to be allocated with this space in mind as fast clears
will always write metadata for both.

Additionally, there are now two seperate LRZ buffers (on top of one for
each direction, a total of four) - due to concurrent binning, one can be
used for binning and the other for rendering concurrently. These can be
flipped between via the `LRZ_FLIP_BUFFER` event which can be put inside
a conditional block for either the BV or BR.

LRZ Fast-Clear
--------------

The LRZ fast-clear buffer is initialized to zeroes and read/written
when ``GRAS_LRZ_CNTL.FC_ENABLE`` is set. It appears to store 1b/block.
``0`` means block has original depth clear value, and ``1`` means that the
corresponding block in LRZ has been modified.

LRZ fast-clear conservatively clears LRZ buffer. At the point where LRZ is
written the LRZ block which corresponds to a single fast-clear bit is cleared:

- To ``0.0`` if depth comparison is ``GREATER``
- To ``1.0`` if depth comparison is ``LESS``

This way it's always valid to fast-clear.

On A7XX, the original depth clear value can be specified exactly allowing for
fast-clear to any value rather than just ``1.0`` or ``0.0``.

LRZ Feedback
-------------

Some draws do write depth but cannot contribute to LRZ during the BINNING pass
e.g. when fragment shader has "discard" in it, however they can contribute to LRZ
during the RENDERING pass via LRZ feedback mechanism. This may allow the draws
that follow to depth test against the updated LRZ, this is especially important
if such "bad" draws were at the start of the renderpass.

LRZ feedback happens during the RENDERING pass when ``LRZ_FEEDBACK_ZMODE_MASK``
is set, if draw has a6xx_ztest_mode that has corresponding flag set in
``LRZ_FEEDBACK_ZMODE_MASK`` - its depth values would be used for feedback.

LRZ feedback alongside with LRZ testing also works during sysmem rendering.

LRZ Precision
-------------

LRZ always uses ``Z16_UNORM``. The epsilon for it is ``1.f / (1 << 16)`` which is
not enough to represent all values of ``Z32_UNORM`` or ``Z32_FLOAT``.
This especially raises questions in context of fast-clear, if fast-clear
uses a value which cannot be precisely represented by LRZ - we wouldn't
be able to round it in the correct direction since direction is tracked
on GPU.

However, it seems that depth comparisons with LRZ values have some "slack"
and nothing special should be done for such depth clear values.

How it was tested:

- Clear ``Z32_FLOAT`` attachment to ``1.f / (1 << 17)``

  - LRZ buffer contains all zeroes.

- Do draws and check whether all samples are passing:

  - ``OP_GREATER`` with ``(1.f / (1 << 17) + float32_epsilon)`` - passing;
  - ``OP_GREATER`` with ``(1.f / (1 << 17) - float32_epsilon)`` - not passing;
  - ``OP_LESS`` with ``(1.f / (1 << 17) - float32_epsilon)`` - samples;
  - ``OP_LESS`` with ``(1.f / (1 << 17) + float32_epsilon)``- not passing;
  - ``OP_LESS_OR_EQ`` with ``(1.f / (1 << 17) + float32_epsilon)`` - not passing.

In all cases resulting LRZ buffer is all zeroes and LRZ direction is updated.

LRZ Caches
----------

``LRZ_FLUSH`` flushes and invalidates LRZ caches, there are two caches:

- Cache for fast-clear buffer;
- Cache for direction byte + depth view params.

They could be cleared by ``LRZ_CLEAR``. To become visible in GPU memory
the caches should be flushed with ``LRZ_FLUSH`` afterwards.

``GRAS_LRZ_CNTL`` reads from these caches.
