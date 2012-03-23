/*
 * Mesa 3-D graphics library
 * Version:  6.5
 *
 * Copyright (C) 1999-2006  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * This file manages the OpenGL API dispatch layer.  There are functions
 * to set/get the current dispatch table for the current thread and to
 * manage registration/dispatch of dynamically added extension functions.
 *
 * This code was originally general enough to be shared with Mesa, but
 * they diverged long ago, so this is now just enough support to make
 * indirect GLX work.
 */

#include <dix-config.h>
#include <X11/Xfuncproto.h>
#include <os.h>
#define PUBLIC _X_EXPORT

#include <stdlib.h>
#include <string.h>
#ifdef DEBUG
#include <assert.h>
#endif

#include "glapi.h"
#include "dispatch.h"
#include "glapitable.h"

#define FIRST_DYNAMIC_OFFSET (sizeof(struct _glapi_table) / sizeof(void *))

#if defined(PTHREADS) || defined(GLX_USE_TLS)
static void init_glapi_relocs(void);
#endif

/**
 * \name Current dispatch and current context control variables
 *
 * Depending on whether or not multithreading is support, and the type of
 * support available, several variables are used to store the current context
 * pointer and the current dispatch table pointer.  In the non-threaded case,
 * the variables \c _glapi_Dispatch and \c _glapi_Context are used for this
 * purpose.
 *
 * In the "normal" threaded case, the variables \c _glapi_Dispatch and
 * \c _glapi_Context will be \c NULL if an application is detected as being
 * multithreaded.  Single-threaded applications will use \c _glapi_Dispatch
 * and \c _glapi_Context just like the case without any threading support.
 * When \c _glapi_Dispatch and \c _glapi_Context are \c NULL, the thread state
 * data \c _gl_DispatchTSD and \c ContextTSD are used.  Drivers and the
 * static dispatch functions access these variables via \c _glapi_get_dispatch
 * and \c _glapi_get_context.
 *
 * In the TLS case, the variables \c _glapi_Dispatch and \c _glapi_Context are
 * hardcoded to \c NULL.  Instead the TLS variables \c _glapi_tls_Dispatch and
 * \c _glapi_tls_Context are used.  Having \c _glapi_Dispatch and
 * \c _glapi_Context be hardcoded to \c NULL maintains binary compatability
 * between TLS enabled loaders and non-TLS DRI drivers.
 */
/*@{*/
#if defined(GLX_USE_TLS)

PUBLIC TLS struct _glapi_table *_glapi_tls_Dispatch = NULL;

PUBLIC TLS void *_glapi_tls_Context;

PUBLIC const struct _glapi_table *_glapi_Dispatch = NULL;
PUBLIC const void *_glapi_Context = NULL;

#else

#if defined(THREADS)

_glthread_TSD _gl_DispatchTSD;           /**< Per-thread dispatch pointer */
static _glthread_TSD ContextTSD;         /**< Per-thread context pointer */

#if defined(WIN32_THREADS)
void FreeTSD(_glthread_TSD * p);
void
FreeAllTSD(void)
{
    FreeTSD(&_gl_DispatchTSD);
    FreeTSD(&ContextTSD);
}
#endif                          /* defined(WIN32_THREADS) */

#endif                          /* defined(THREADS) */

PUBLIC struct _glapi_table *_glapi_Dispatch = NULL;
PUBLIC void *_glapi_Context = NULL;

#endif                          /* defined(GLX_USE_TLS) */
/*@}*/

/*
 * xserver's gl is not multithreaded, we promise.
 */
PUBLIC void
_glapi_check_multithread(void)
{
}

/**
 * Set the current context pointer for this thread.
 * The context pointer is an opaque type which should be cast to
 * void from the real context pointer type.
 */
PUBLIC void
_glapi_set_context(void *context)
{
#if defined(GLX_USE_TLS)
    _glapi_tls_Context = context;
#elif defined(THREADS)
    _glthread_SetTSD(&ContextTSD, context);
    _glapi_Context = context;
#else
    _glapi_Context = context;
#endif
}

/**
 * Get the current context pointer for this thread.
 * The context pointer is an opaque type which should be cast from
 * void to the real context pointer type.
 */
PUBLIC void *
_glapi_get_context(void)
{
#if defined(GLX_USE_TLS)
    return _glapi_tls_Context;
#else
    return _glapi_Context;
#endif
}

/**
 * Set the global or per-thread dispatch table pointer.
 */
PUBLIC void
_glapi_set_dispatch(struct _glapi_table *dispatch)
{
#if defined(PTHREADS) || defined(GLX_USE_TLS)
    static pthread_once_t once_control = PTHREAD_ONCE_INIT;

    pthread_once(&once_control, init_glapi_relocs);
#endif

#if defined(GLX_USE_TLS)
    _glapi_tls_Dispatch = dispatch;
#elif defined(THREADS)
    _glthread_SetTSD(&_gl_DispatchTSD, (void *) dispatch);
    _glapi_Dispatch = dispatch;
#else /*THREADS*/
        _glapi_Dispatch = dispatch;
#endif /*THREADS*/
}

/**
 * Return pointer to current dispatch table for calling thread.
 */
PUBLIC struct _glapi_table *
_glapi_get_dispatch(void)
{
    struct _glapi_table *api;

#if defined(GLX_USE_TLS)
    api = _glapi_tls_Dispatch;
#else
    api = _glapi_Dispatch;
#endif
    return api;
}

/***
 *** The rest of this file is pretty much concerned with GetProcAddress
 *** functionality.
 ***/

#if defined(USE_X64_64_ASM) && defined(GLX_USE_TLS)
#define DISPATCH_FUNCTION_SIZE  16
#elif defined(USE_X86_ASM)
#if defined(THREADS) && !defined(GLX_USE_TLS)
#define DISPATCH_FUNCTION_SIZE  32
#else
#define DISPATCH_FUNCTION_SIZE  16
#endif
#endif

/* The code in this file is auto-generated with Python */
#include "glprocs.h"

/**
 * Search the table of static entrypoint functions for the named function
 * and return the corresponding glprocs_table_t entry.
 */
static const glprocs_table_t *
find_entry(const char *n)
{
    GLuint i;

    for (i = 0; static_functions[i].Name_offset >= 0; i++) {
        const char *testName =
            gl_string_table + static_functions[i].Name_offset;
        if (strcmp(testName, n) == 0) {
            return &static_functions[i];
        }
    }
    return NULL;
}

/**
 * Return dispatch table offset of the named static (built-in) function.
 * Return -1 if function not found.
 */
static GLint
get_static_proc_offset(const char *funcName)
{
    const glprocs_table_t *const f = find_entry(funcName);

    if (f) {
        return f->Offset;
    }
    return -1;
}

/**********************************************************************
 * Extension function management.
 */

/*
 * Number of extension functions which we can dynamically add at runtime.
 */
#define MAX_EXTENSION_FUNCS 300

/*
 * The dispatch table size (number of entries) is the size of the
 * _glapi_table struct plus the number of dynamic entries we can add.
 * The extra slots can be filled in by DRI drivers that register new extension
 * functions.
 */
#define DISPATCH_TABLE_SIZE (sizeof(struct _glapi_table) / sizeof(void *) + MAX_EXTENSION_FUNCS)

/**
 * Track information about a function added to the GL API.
 */
struct _glapi_function {
   /**
    * Name of the function.
    */
    const char *name;

   /**
    * Text string that describes the types of the parameters passed to the
    * named function.   Parameter types are converted to characters using the
    * following rules:
    *   - 'i' for \c GLint, \c GLuint, and \c GLenum
    *   - 'p' for any pointer type
    *   - 'f' for \c GLfloat and \c GLclampf
    *   - 'd' for \c GLdouble and \c GLclampd
    */
    const char *parameter_signature;

   /**
    * Offset in the dispatch table where the pointer to the real function is
    * located.  If the driver has not requested that the named function be
    * added to the dispatch table, this will have the value ~0.
    */
    unsigned dispatch_offset;
};

static struct _glapi_function ExtEntryTable[MAX_EXTENSION_FUNCS];
static GLuint NumExtEntryPoints = 0;

/**
 * Generate new entrypoint
 *
 * Use a temporary dispatch offset of ~0 (i.e. -1).  Later, when the driver
 * calls \c _glapi_add_dispatch we'll put in the proper offset.  If that
 * never happens, and the user calls this function, he'll segfault.  That's
 * what you get when you try calling a GL function that doesn't really exist.
 * 
 * \param funcName  Name of the function to create an entry-point for.
 * 
 * \sa _glapi_add_entrypoint
 */

static struct _glapi_function *
add_function_name(const char *funcName)
{
    struct _glapi_function *entry = NULL;

    if (NumExtEntryPoints < MAX_EXTENSION_FUNCS) {
        entry = &ExtEntryTable[NumExtEntryPoints];

        ExtEntryTable[NumExtEntryPoints].name = strdup(funcName);
        ExtEntryTable[NumExtEntryPoints].parameter_signature = NULL;
        ExtEntryTable[NumExtEntryPoints].dispatch_offset = ~0;
        NumExtEntryPoints++;
    }

    return entry;
}

/**
 * Fill-in the dispatch stub for the named function.
 * 
 * This function is intended to be called by a hardware driver.  When called,
 * a dispatch stub may be created created for the function.  A pointer to this
 * dispatch function will be returned by glXGetProcAddress.
 *
 * \param function_names       Array of pointers to function names that should
 *                             share a common dispatch offset.
 * \param parameter_signature  String representing the types of the parameters
 *                             passed to the named function.  Parameter types
 *                             are converted to characters using the following
 *                             rules:
 *                               - 'i' for \c GLint, \c GLuint, and \c GLenum
 *                               - 'p' for any pointer type
 *                               - 'f' for \c GLfloat and \c GLclampf
 *                               - 'd' for \c GLdouble and \c GLclampd
 *
 * \returns
 * The offset in the dispatch table of the named function.  A pointer to the
 * driver's implementation of the named function should be stored at
 * \c dispatch_table[\c offset].
 *
 * \sa glXGetProcAddress
 *
 * \warning
 * This function can only handle up to 8 names at a time.  As far as I know,
 * the maximum number of names ever associated with an existing GL function is
 * 4 (\c glPointParameterfSGIS, \c glPointParameterfEXT,
 * \c glPointParameterfARB, and \c glPointParameterf), so this should not be
 * too painful of a limitation.
 *
 * \todo
 * Determine whether or not \c parameter_signature should be allowed to be
 * \c NULL.  It doesn't seem like much of a hardship for drivers to have to
 * pass in an empty string.
 *
 * \todo
 * Determine if code should be added to reject function names that start with
 * 'glX'.
 * 
 * \bug
 * Add code to compare \c parameter_signature with the parameter signature of
 * a static function.  In order to do that, we need to find a way to \b get
 * the parameter signature of a static function.
 */

PUBLIC int
_glapi_add_dispatch(const char *const *function_names,
                    const char *parameter_signature)
{
    static int next_dynamic_offset = FIRST_DYNAMIC_OFFSET;
    const char *const real_sig = (parameter_signature != NULL)
        ? parameter_signature : "";
    struct _glapi_function *entry[8];
    GLboolean is_static[8];
    unsigned i;
    unsigned j;
    int offset = ~0;
    int new_offset;

    (void) memset(is_static, 0, sizeof(is_static));
    (void) memset(entry, 0, sizeof(entry));

    for (i = 0; function_names[i] != NULL; i++) {
        /* Do some trivial validation on the name of the function. */

        if (function_names[i][0] != 'g' || function_names[i][1] != 'l')
            return GL_FALSE;

        /* Determine if the named function already exists.  If the function does
         * exist, it must have the same parameter signature as the function
         * being added.
         */

        new_offset = get_static_proc_offset(function_names[i]);
        if (new_offset >= 0) {
            /* FIXME: Make sure the parameter signatures match!  How do we get
             * FIXME: the parameter signature for static functions?
             */

            if ((offset != ~0) && (new_offset != offset)) {
                return -1;
            }

            is_static[i] = GL_TRUE;
            offset = new_offset;
        }

        for (j = 0; j < NumExtEntryPoints; j++) {
            if (strcmp(ExtEntryTable[j].name, function_names[i]) == 0) {
                /* The offset may be ~0 if the function name was added by
                 * glXGetProcAddress but never filled in by the driver.
                 */

                if (ExtEntryTable[j].dispatch_offset != ~0) {
                    if (strcmp(real_sig, ExtEntryTable[j].parameter_signature)
                        != 0)
                        return -1;

                    if ((offset != ~0) &&
                        (ExtEntryTable[j].dispatch_offset != offset)) {
                        return -1;
                    }

                    offset = ExtEntryTable[j].dispatch_offset;
                }

                entry[i] = &ExtEntryTable[j];
                break;
            }
        }
    }

    if (offset == ~0) {
        offset = next_dynamic_offset;
        next_dynamic_offset++;
    }

    for (i = 0; function_names[i] != NULL; i++) {
        if (!is_static[i]) {
            if (entry[i] == NULL) {
                entry[i] = add_function_name(function_names[i]);
                if (entry[i] == NULL)
                    return -1;
            }

            entry[i]->parameter_signature = strdup(real_sig);
            entry[i]->dispatch_offset = offset;
        }
    }

    return offset;
}

/*
 * glXGetProcAddress doesn't exist in the protocol, the drivers never call
 * this themselves, and neither does the server.  warn if it happens though.
 */
PUBLIC _glapi_proc
_glapi_get_proc_address(const char *funcName)
{
    ErrorF("_glapi_get_proc_address called!\n");
    return NULL;
}

/**
 * Return size of dispatch table struct as number of functions (or
 * slots).
 */
PUBLIC GLuint
_glapi_get_dispatch_table_size(void)
{
    return DISPATCH_TABLE_SIZE;
}

#if defined(PTHREADS) || defined(GLX_USE_TLS)
/**
 * Perform platform-specific GL API entry-point fixups.
 */
static void
init_glapi_relocs(void)
{
#if defined(USE_X86_ASM) && defined(GLX_USE_TLS) && !defined(GLX_X86_READONLY_TEXT)
    extern unsigned long _x86_get_dispatch(void);

    char run_time_patch[] = {
        0x65, 0xa1, 0, 0, 0, 0  /* movl %gs:0,%eax */
    };
    GLuint *offset = (GLuint *) & run_time_patch[2];    /* 32-bits for x86/32 */
    const GLubyte *const get_disp = (const GLubyte *) run_time_patch;
    GLubyte *curr_func = (GLubyte *) gl_dispatch_functions_start;

    *offset = _x86_get_dispatch();
    while (curr_func != (GLubyte *) gl_dispatch_functions_end) {
        (void) memcpy(curr_func, get_disp, sizeof(run_time_patch));
        curr_func += DISPATCH_FUNCTION_SIZE;
    }
#endif
}
#endif                          /* defined(PTHREADS) || defined(GLX_USE_TLS) */
