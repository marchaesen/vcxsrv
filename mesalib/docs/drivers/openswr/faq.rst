FAQ
===

Why another software rasterizer?
--------------------------------

Good question, given there are already three (swrast, softpipe,
llvmpipe) in the Mesa tree. Two important reasons for this:

 * Architecture - given our focus on scientific visualization, our
   workloads are much different than the typical game; we have heavy
   vertex load and relatively simple shaders.  In addition, the core
   counts of machines we run on are much higher.  These parameters led
   to design decisions much different than llvmpipe.

 * Historical - Intel had developed a high performance software
   graphics stack for internal purposes.  Later we adapted this
   graphics stack for use in visualization and decided to move forward
   with Mesa to provide a high quality API layer while at the same
   time benefiting from the excellent performance the software
   rasterizerizer gives us.

What's the architecture?
------------------------

SWR is a tile based immediate mode renderer with a sort-free threading
model which is arranged as a ring of queues.  Each entry in the ring
represents a draw context that contains all of the draw state and work
queues.  An API thread sets up each draw context and worker threads
will execute both the frontend (vertex/geometry processing) and
backend (fragment) work as required.  The ring allows for backend
threads to pull work in order.  Large draws are split into chunks to
allow vertex processing to happen in parallel, with the backend work
pickup preserving draw ordering.

Our pipeline uses just-in-time compiled code for the fetch shader that
does vertex attribute gathering and AOS to SOA conversions, the vertex
shader and fragment shaders, streamout, and fragment blending. SWR
core also supports geometry and compute shaders but we haven't exposed
them through our driver yet. The fetch shader, streamout, and blend is
built internally to swr core using LLVM directly, while for the vertex
and pixel shaders we reuse bits of llvmpipe from
``gallium/auxiliary/gallivm`` to build the kernels, which we wrap
differently than llvmpipe's ``auxiliary/draw`` code.

What's the performance?
-----------------------

For the types of high-geometry workloads we're interested in, we are
significantly faster than llvmpipe.  This is to be expected, as
llvmpipe only threads the fragment processing and not the geometry
frontend.  The performance advantage over llvmpipe roughly scales
linearly with the number of cores available.

While our current performance is quite good, we know there is more
potential in this architecture.  When we switched from a prototype
OpenGL driver to Mesa we regressed performance severely, some due to
interface issues that need tuning, some differences in shader code
generation, and some due to conformance and feature additions to the
core swr.  We are looking to recovering most of this performance back.

What's the conformance?
-----------------------

The major applications we are targeting are all based on the
Visualization Toolkit (VTK), and as such our development efforts have
been focused on making sure these work as best as possible.  Our
current code passes vtk's rendering tests with their new "OpenGL2"
(really OpenGL 3.2) backend at 99%.

piglit testing shows a much lower pass rate, roughly 80% at the time
of writing.  Core SWR undergoes rigorous unit testing and we are quite
confident in the rasterizer, and understand the areas where it
currently has issues (example: line rendering is done with triangles,
so doesn't match the strict line rendering rules).  The majority of
the piglit failures are errors in our driver layer interfacing Mesa
and SWR.  Fixing these issues is one of our major future development
goals.

Why are you open sourcing this?
-------------------------------

 * Our customers prefer open source, and allowing them to simply
   download the Mesa source and enable our driver makes life much
   easier for them.

 * The internal gallium APIs are not stable, so we'd like our driver
   to be visible for changes.

 * It's easier to work with the Mesa community when the source we're
   working with can be used as reference.

What are your development plans?
--------------------------------

 * Performance - see the performance section earlier for details.

 * Conformance - see the conformance section earlier for details.

 * Features - core SWR has a lot of functionality we have yet to
   expose through our driver, such as MSAA, geometry shaders, compute
   shaders, and tesselation.

 * AVX512 support

What is the licensing of the code?
----------------------------------

 * All code is under the normal Mesa MIT license.

Will this work on AMD?
----------------------

 * If using an AMD processor with AVX or AVX2, it should work though
   we don't have that hardware around to test.  Patches if needed
   would be welcome.

Will this work on ARM, MIPS, POWER, <other non-x86 architecture>?
-------------------------------------------------------------------------

 * Not without a lot of work.  We make extensive use of AVX and AVX2
   intrinsics in our code and the in-tree JIT creation.  It is not the
   intention for this codebase to support non-x86 architectures.

What hardware do I need?
------------------------

 * Any x86 processor with at least AVX (introduced in the Intel
   SandyBridge and AMD Bulldozer microarchitectures in 2011) will
   work.

 * You don't need a fire-breathing Xeon machine to work on SWR - we do
   day-to-day development with laptops and desktop CPUs.

Does one build work on both AVX and AVX2?
-----------------------------------------

Yes. The build system creates two shared libraries, ``libswrAVX.so`` and
``libswrAVX2.so``, and ``swr_create_screen()`` loads the appropriate one at
runtime.

