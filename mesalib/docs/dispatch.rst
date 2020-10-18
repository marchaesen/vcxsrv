GL Dispatch
===========

Several factors combine to make efficient dispatch of OpenGL functions
fairly complicated. This document attempts to explain some of the issues
and introduce the reader to Mesa's implementation. Readers already
familiar with the issues around GL dispatch can safely skip ahead to the
:ref:`overview of Mesa's implementation <overview>`.

1. Complexity of GL Dispatch
----------------------------

Every GL application has at least one object called a GL *context*. This
object, which is an implicit parameter to every GL function, stores all
of the GL related state for the application. Every texture, every buffer
object, every enable, and much, much more is stored in the context.
Since an application can have more than one context, the context to be
used is selected by a window-system dependent function such as
``glXMakeContextCurrent``.

In environments that implement OpenGL with X-Windows using GLX, every GL
function, including the pointers returned by ``glXGetProcAddress``, are
*context independent*. This means that no matter what context is
currently active, the same ``glVertex3fv`` function is used.

This creates the first bit of dispatch complexity. An application can
have two GL contexts. One context is a direct rendering context where
function calls are routed directly to a driver loaded within the
application's address space. The other context is an indirect rendering
context where function calls are converted to GLX protocol and sent to a
server. The same ``glVertex3fv`` has to do the right thing depending on
which context is current.

Highly optimized drivers or GLX protocol implementations may want to
change the behavior of GL functions depending on current state. For
example, ``glFogCoordf`` may operate differently depending on whether or
not fog is enabled.

In multi-threaded environments, it is possible for each thread to have a
different GL context current. This means that poor old ``glVertex3fv``
has to know which GL context is current in the thread where it is being
called.

.. _overview:

2. Overview of Mesa's Implementation
------------------------------------

Mesa uses two per-thread pointers. The first pointer stores the address
of the context current in the thread, and the second pointer stores the
address of the *dispatch table* associated with that context. The
dispatch table stores pointers to functions that actually implement
specific GL functions. Each time a new context is made current in a
thread, these pointers a updated.

The implementation of functions such as ``glVertex3fv`` becomes
conceptually simple:

-  Fetch the current dispatch table pointer.
-  Fetch the pointer to the real ``glVertex3fv`` function from the
   table.
-  Call the real function.

This can be implemented in just a few lines of C code. The file
``src/mesa/glapi/glapitemp.h`` contains code very similar to this.

.. code-block:: c
   :caption: Sample dispatch function

   void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
   {
       const struct _glapi_table * const dispatch = GET_DISPATCH();

       (*dispatch->Vertex3f)(x, y, z);
   }

The problem with this simple implementation is the large amount of
overhead that it adds to every GL function call.

In a multithreaded environment, a naive implementation of
``GET_DISPATCH`` involves a call to ``pthread_getspecific`` or a similar
function. Mesa provides a wrapper function called
``_glapi_get_dispatch`` that is used by default.

3. Optimizations
----------------

A number of optimizations have been made over the years to diminish the
performance hit imposed by GL dispatch. This section describes these
optimizations. The benefits of each optimization and the situations
where each can or cannot be used are listed.

3.1. Dual dispatch table pointers
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The vast majority of OpenGL applications use the API in a single
threaded manner. That is, the application has only one thread that makes
calls into the GL. In these cases, not only do the calls to
``pthread_getspecific`` hurt performance, but they are completely
unnecessary! It is possible to detect this common case and avoid these
calls.

Each time a new dispatch table is set, Mesa examines and records the ID
of the executing thread. If the same thread ID is always seen, Mesa
knows that the application is, from OpenGL's point of view, single
threaded.

As long as an application is single threaded, Mesa stores a pointer to
the dispatch table in a global variable called ``_glapi_Dispatch``. The
pointer is also stored in a per-thread location via
``pthread_setspecific``. When Mesa detects that an application has
become multithreaded, ``NULL`` is stored in ``_glapi_Dispatch``.

Using this simple mechanism the dispatch functions can detect the
multithreaded case by comparing ``_glapi_Dispatch`` to ``NULL``. The
resulting implementation of ``GET_DISPATCH`` is slightly more complex,
but it avoids the expensive ``pthread_getspecific`` call in the common
case.

.. code-block:: c
   :caption: Improved ``GET_DISPATCH`` Implementation

   #define GET_DISPATCH() \
       (_glapi_Dispatch != NULL) \
           ? _glapi_Dispatch : pthread_getspecific(&_glapi_Dispatch_key)

3.2. ELF TLS
~~~~~~~~~~~~

Starting with the 2.4.20 Linux kernel, each thread is allocated an area
of per-thread, global storage. Variables can be put in this area using
some extensions to GCC. By storing the dispatch table pointer in this
area, the expensive call to ``pthread_getspecific`` and the test of
``_glapi_Dispatch`` can be avoided.

The dispatch table pointer is stored in a new variable called
``_glapi_tls_Dispatch``. A new variable name is used so that a single
libGL can implement both interfaces. This allows the libGL to operate
with direct rendering drivers that use either interface. Once the
pointer is properly declared, ``GET_DISPACH`` becomes a simple variable
reference.

.. code-block:: c
   :caption: TLS ``GET_DISPATCH`` Implementation

   extern __thread struct _glapi_table *_glapi_tls_Dispatch
       __attribute__((tls_model("initial-exec")));

   #define GET_DISPATCH() _glapi_tls_Dispatch

Use of this path is controlled by the preprocessor define
``USE_ELF_TLS``. Any platform capable of using ELF TLS should use this
as the default dispatch method.

3.3. Assembly Language Dispatch Stubs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Many platforms has difficulty properly optimizing the tail-call in the
dispatch stubs. Platforms like x86 that pass parameters on the stack
seem to have even more difficulty optimizing these routines. All of the
dispatch routines are very short, and it is trivial to create optimal
assembly language versions. The amount of optimization provided by using
assembly stubs varies from platform to platform and application to
application. However, by using the assembly stubs, many platforms can
use an additional space optimization (see :ref:`below <fixedsize>`).

The biggest hurdle to creating assembly stubs is handling the various
ways that the dispatch table pointer can be accessed. There are four
different methods that can be used:

#. Using ``_glapi_Dispatch`` directly in builds for non-multithreaded
   environments.
#. Using ``_glapi_Dispatch`` and ``_glapi_get_dispatch`` in
   multithreaded environments.
#. Using ``_glapi_Dispatch`` and ``pthread_getspecific`` in
   multithreaded environments.
#. Using ``_glapi_tls_Dispatch`` directly in TLS enabled multithreaded
   environments.

People wishing to implement assembly stubs for new platforms should
focus on #4 if the new platform supports TLS. Otherwise, implement #2
followed by #3. Environments that do not support multithreading are
uncommon and not terribly relevant.

Selection of the dispatch table pointer access method is controlled by a
few preprocessor defines.

-  If ``USE_ELF_TLS`` is defined, method #3 is used.
-  If ``HAVE_PTHREAD`` is defined, method #2 is used.
-  If none of the preceding are defined, method #1 is used.

Two different techniques are used to handle the various different cases.
On x86 and SPARC, a macro called ``GL_STUB`` is used. In the preamble of
the assembly source file different implementations of the macro are
selected based on the defined preprocessor variables. The assembly code
then consists of a series of invocations of the macros such as:

.. code-block:: c
   :caption: SPARC Assembly Implementation of ``glColor3fv``

   GL_STUB(Color3fv, _gloffset_Color3fv)

The benefit of this technique is that changes to the calling pattern
(i.e., addition of a new dispatch table pointer access method) require
fewer changed lines in the assembly code.

However, this technique can only be used on platforms where the function
implementation does not change based on the parameters passed to the
function. For example, since x86 passes all parameters on the stack, no
additional code is needed to save and restore function parameters around
a call to ``pthread_getspecific``. Since x86-64 passes parameters in
registers, varying amounts of code needs to be inserted around the call
to ``pthread_getspecific`` to save and restore the GL function's
parameters.

The other technique, used by platforms like x86-64 that cannot use the
first technique, is to insert ``#ifdef`` within the assembly
implementation of each function. This makes the assembly file
considerably larger (e.g., 29,332 lines for ``glapi_x86-64.S`` versus
1,155 lines for ``glapi_x86.S``) and causes simple changes to the
function implementation to generate many lines of diffs. Since the
assembly files are typically generated by scripts, this isn't a
significant problem.

Once a new assembly file is created, it must be inserted in the build
system. There are two steps to this. The file must first be added to
``src/mesa/sources``. That gets the file built and linked. The second
step is to add the correct ``#ifdef`` magic to
``src/mesa/glapi/glapi_dispatch.c`` to prevent the C version of the
dispatch functions from being built.

.. _fixedsize:

3.4. Fixed-Length Dispatch Stubs
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

To implement ``glXGetProcAddress``, Mesa stores a table that associates
function names with pointers to those functions. This table is stored in
``src/mesa/glapi/glprocs.h``. For different reasons on different
platforms, storing all of those pointers is inefficient. On most
platforms, including all known platforms that support TLS, we can avoid
this added overhead.

If the assembly stubs are all the same size, the pointer need not be
stored for every function. The location of the function can instead be
calculated by multiplying the size of the dispatch stub by the offset of
the function in the table. This value is then added to the address of
the first dispatch stub.

This path is activated by adding the correct ``#ifdef`` magic to
``src/mesa/glapi/glapi.c`` just before ``glprocs.h`` is included.
