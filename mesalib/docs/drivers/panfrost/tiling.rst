
U-interleaved tiling
====================

Panfrost supports u-interleaved tiling. U-interleaved tiling is
indicated by the ``DRM_FORMAT_MOD_ARM_16X16_BLOCK_U_INTERLEAVED`` modifier.

The tiling reorders whole pixels (blocks). It does not compress or modify the
pixels themselves, so it can be used for any image format. Internally, images
are divided into tiles. Tiles occur in source order, but pixels (blocks) within
each tile are reordered according to a space-filling curve.

For regular formats, 16x16 tiles are used. This harmonizes with the default tile
size for binning and CRCs (transaction elimination). It also means a single line
(16 pixels) at 4 bytes per pixel equals a single 64-byte cache line.

For formats that are already block compressed (S3TC, RGTC, etc), 4x4 tiles are
used, where entire blocks are reorder. Most of these formats compress 4x4
blocks, so this gives an effective 16x16 tiling. This justifies the tile size
intuitively, though it's not a rule: ASTC may uses larger blocks.

Within a tile, the X and Y bits are interleaved (like Morton order), but with a
twist: adjacent bit pairs are XORed. The reason to add XORs is not obvious.
Visually, addresses take the form::

   | y3 | (x3 ^ y3) | y2 | (y2 ^ x2) | y1 | (y1 ^ x1) | y0 | (y0 ^ x0) |

Reference routines to encode/decode u-interleaved images are available in
``src/panfrost/shared/test/test-tiling.cpp``, which documents the space-filling
curve. This reference implementation is used to unit test the optimized
implementation used in production. The optimized implementation is available in
``src/panfrost/shared/pan_tiling.c``.

Although these routines are part of Panfrost, they are also used by Lima, as Arm
introduced the format with Utgard. It is the only tiling supported on Utgard. On
Mali-T760 and newer, Arm Framebuffer Compression (AFBC) is more efficient and
should be used instead where possible. However, not all formats are
compressible, so u-interleaved tiling remains an important fallback on Panfrost.
