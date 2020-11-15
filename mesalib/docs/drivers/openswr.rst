OpenSWR
=======

The Gallium OpenSWR driver is a high performance, highly scalable
software renderer targeted towards visualization workloads.  For such
geometry heavy workloads there is a considerable speedup over llvmpipe,
which is to be expected as the geometry frontend of llvmpipe is single
threaded.

This rasterizer is x86 specific and requires AVX or above.  The driver
fits into the gallium framework, and reuses gallivm for doing the TGSI
to vectorized llvm-IR conversion of the shader kernels.

.. toctree::
   :glob:

   openswr/usage
   openswr/faq
   openswr/profiling
   openswr/knobs

