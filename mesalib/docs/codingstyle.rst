Coding Style
============

Mesa is over 20 years old and the coding style has evolved over time.
Some old parts use a style that's a bit out of date. Different sections
of mesa can use different coding style as set in the local EditorConfig
(.editorconfig) and/or Emacs (.dir-locals.el) file. Alternatively the
following is applicable. If the guidelines below don't cover something,
try following the format of existing, neighboring code.

Basic formatting guidelines

-  3-space indentation, no tabs.
-  Limit lines to 78 or fewer characters. The idea is to prevent line
   wrapping in 80-column editors and terminals. There are exceptions,
   such as if you're defining a large, static table of information.
-  Opening braces go on the same line as the if/for/while statement. For
   example:

   .. code-block:: c

      if (condition) {
         foo;
      } else {
         bar;
      }

-  Put a space before/after operators. For example, ``a = b + c;`` and
   not ``a=b+c;``
-  This GNU indent command generally does the right thing for
   formatting:

   .. code-block:: console

      indent -br -i3 -npcs --no-tabs infile.c -o outfile.c

-  Use comments wherever you think it would be helpful for other
   developers. Several specific cases and style examples follow. Note
   that we roughly follow `Doxygen <http://www.doxygen.nl>`__
   conventions.

   Single-line comments:

   .. code-block:: c

      /* null-out pointer to prevent dangling reference below */
      bufferObj = NULL;

   Or,

   .. code-block:: c

      bufferObj = NULL;  /* prevent dangling reference below */

   Multi-line comment:

   .. code-block:: c

      /* If this is a new buffer object id, or one which was generated but
       * never used before, allocate a buffer object now.
       */

   We try to quote the OpenGL specification where prudent:

   .. code-block:: c

      /* Page 38 of the PDF of the OpenGL ES 3.0 spec says:
       *
       *     "An INVALID_OPERATION error is generated for any of the following
       *     conditions:
       *
       *     * <length> is zero."
       *
       * Additionally, page 94 of the PDF of the OpenGL 4.5 core spec
       * (30.10.2014) also says this, so it's no longer allowed for desktop GL,
       * either.
       */

   Function comment example:

   .. code-block:: c

      /**
       * Create and initialize a new buffer object.  Called via the
       * ctx->Driver.CreateObject() driver callback function.
       * \param  name  integer name of the object
       * \param  type  one of GL_FOO, GL_BAR, etc.
       * \return  pointer to new object or NULL if error
       */
      struct gl_object *
      _mesa_create_object(GLuint name, GLenum type)
      {
         /* function body */
      }

-  Put the function return type and qualifiers on one line and the
   function name and parameters on the next, as seen above. This makes
   it easy to use ``grep ^function_name dir/*`` to find function
   definitions. Also, the opening brace goes on the next line by itself
   (see above.)
-  Function names follow various conventions depending on the type of
   function:

   ::

      glFooBar()       - a public GL entry point (in glapi_dispatch.c)
      _mesa_FooBar()   - the internal immediate mode function
      save_FooBar()    - retained mode (display list) function in dlist.c
      foo_bar()        - a static (private) function
      _mesa_foo_bar()  - an internal non-static Mesa function

-  Constants, macros and enum names are ``ALL_UPPERCASE``, with \_
   between words.
-  Mesa usually uses camel case for local variables (Ex:
   ``localVarname``) while Gallium typically uses underscores (Ex:
   ``local_var_name``).
-  Global variables are almost never used because Mesa should be
   thread-safe.
-  Booleans. Places that are not directly visible to the GL API should
   prefer the use of ``bool``, ``true``, and ``false`` over
   ``GLboolean``, ``GL_TRUE``, and ``GL_FALSE``. In C code, this may
   mean that ``#include <stdbool.h>`` needs to be added. The
   ``try_emit_*`` methods in ``src/mesa/program/ir_to_mesa.cpp`` and
   ``src/mesa/state_tracker/st_glsl_to_tgsi.cpp`` can serve as examples.
