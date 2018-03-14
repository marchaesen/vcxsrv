/**************************************************************************
 * 
 * Copyright 2009 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/**
 * @file
 * Stack backtracing.
 *
 * @author Jose Fonseca <jfonseca@vmware.com>
 */

#include "u_debug.h"
#include "u_debug_symbol.h"
#include "u_debug_stack.h"

#if defined(HAVE_LIBUNWIND)

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

#include "os/os_thread.h"
#include "u_hash_table.h"

struct util_hash_table* symbols_hash;
static mtx_t symbols_mutex = _MTX_INITIALIZER_NP;

static unsigned hash_ptr(void* p)
{
   return (unsigned)(uintptr_t)p;
}

static int compare_ptr(void* a, void* b)
{
   if(a == b)
      return 0;
   else if(a < b)
      return -1;
   else
      return 1;
}

/* TODO with some refactoring we might be able to re-use debug_symbol_name_cached()
 * instead.. otoh if using libunwind I think u_debug_symbol could just be excluded
 * from build?
 */
static const char *
symbol_name_cached(unw_cursor_t *cursor, unw_proc_info_t *pip)
{
   void *addr = (void *)(uintptr_t)pip->start_ip;
   char *name;

   mtx_lock(&symbols_mutex);
   if(!symbols_hash)
      symbols_hash = util_hash_table_create(hash_ptr, compare_ptr);
   name = util_hash_table_get(symbols_hash, addr);
   if(!name)
   {
      char procname[256];
      unw_word_t off;
      int ret;

      ret = unw_get_proc_name(cursor, procname, sizeof(procname), &off);
      if (ret && ret != -UNW_ENOMEM) {
         procname[0] = '?';
         procname[1] = 0;
      }

      if (asprintf(&name, "%s%s", procname, ret == -UNW_ENOMEM ? "..." : "") == -1) 
         name = "??";
      util_hash_table_set(symbols_hash, addr, (void*)name);
   }
   mtx_unlock(&symbols_mutex);

   return name;
}

void
debug_backtrace_capture(struct debug_stack_frame *backtrace,
                        unsigned start_frame,
                        unsigned nr_frames)
{
   unw_cursor_t cursor;
   unw_context_t context;
   unw_proc_info_t pip;
   unsigned i = 0;

   pip.unwind_info = NULL;

   unw_getcontext(&context);
   unw_init_local(&cursor, &context);

   while ((start_frame > 0) && (unw_step(&cursor) > 0))
      start_frame--;

   while ((i < nr_frames) && (unw_step(&cursor) > 0)) {
      unw_word_t ip;

      unw_get_reg(&cursor, UNW_REG_IP, &ip);
      unw_get_proc_info(&cursor, &pip);

      backtrace[i].start_ip = pip.start_ip;
      backtrace[i].off      = ip - pip.start_ip;
      backtrace[i].procname = symbol_name_cached(&cursor, &pip);

      i++;
   }

   while (i < nr_frames) {
      backtrace[i].start_ip = 0;
      i++;
   }
}

static const void *
frame_ip(const struct debug_stack_frame *frame)
{
   return (void *)(uintptr_t)(frame->start_ip + frame->off);
}

static const char *
frame_info(const struct debug_stack_frame *frame, unsigned *offset)
{
   Dl_info dlinfo;
   const void *addr = frame_ip(frame);


   if (dladdr(addr, &dlinfo) && dlinfo.dli_fname &&
           *dlinfo.dli_fname) {
      *offset = (unsigned)((uintptr_t)addr - (uintptr_t)dlinfo.dli_fbase);
      return dlinfo.dli_fname;
   }

   *offset = 0;
   return "?";
}

void
debug_backtrace_dump(const struct debug_stack_frame *backtrace,
                     unsigned nr_frames)
{
   unsigned i, offset;
   const char *filename;

   for (i = 0; i < nr_frames; ++i) {
      if (!backtrace[i].start_ip)
         break;
      filename = frame_info(&backtrace[i], &offset);
      debug_printf("\t%s(+0x%x) (%s+0x%x) [%p]\n", filename, offset,
            backtrace[i].procname, backtrace[i].off,
            frame_ip(&backtrace[i]));
   }
}

void
debug_backtrace_print(FILE *f,
                      const struct debug_stack_frame *backtrace,
                      unsigned nr_frames)
{
   unsigned i, offset;
   const char *filename;

   for (i = 0; i < nr_frames; ++i) {
      if (!backtrace[i].start_ip)
         break;
      filename = frame_info(&backtrace[i], &offset);
      fprintf(f, "\t%s(+0x%x) (%s+0x%x) [%p]\n", filename, offset,
            backtrace[i].procname, backtrace[i].off,
            frame_ip(&backtrace[i]));
   }
}

#else /* ! HAVE_LIBUNWIND */

#if defined(PIPE_OS_WINDOWS)
#include <windows.h>
#endif


/**
 * Capture stack backtrace.
 *
 * NOTE: The implementation of this function is quite big, but it is important
 * not to break it down in smaller functions to avoid adding new frames to the
 * calling stack.
 */
void
debug_backtrace_capture(struct debug_stack_frame *backtrace,
                        unsigned start_frame,
                        unsigned nr_frames)
{
   const void **frame_pointer = NULL;
   unsigned i = 0;

   if (!nr_frames) {
      return;
   }

   /*
    * On Windows try obtaining the stack backtrace via CaptureStackBackTrace.
    *
    * It works reliably both for x86 for x86_64.
    */
#if defined(PIPE_OS_WINDOWS)
   {
      typedef USHORT (WINAPI *PFNCAPTURESTACKBACKTRACE)(ULONG, ULONG,
                                                        PVOID *, PULONG);
      static PFNCAPTURESTACKBACKTRACE pfnCaptureStackBackTrace = NULL;

      if (!pfnCaptureStackBackTrace) {
         static HMODULE hModule = NULL;
         if (!hModule) {
            hModule = LoadLibraryA("kernel32");
            assert(hModule);
         }
         if (hModule) {
            pfnCaptureStackBackTrace =
               (PFNCAPTURESTACKBACKTRACE)GetProcAddress(hModule,
                                                "RtlCaptureStackBackTrace");
         }
      }
      if (pfnCaptureStackBackTrace) {
         /*
          * Skip this (debug_backtrace_capture) function's frame.
          */

         start_frame += 1;

         assert(start_frame + nr_frames < 63);
         i = pfnCaptureStackBackTrace(start_frame, nr_frames,
                                      (PVOID *) &backtrace->function, NULL);

         /* Pad remaing requested frames with NULL */
         while (i < nr_frames) {
            backtrace[i++].function = NULL;
         }

         return;
      }
   }
#endif

#if defined(PIPE_CC_GCC)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wframe-address"
   frame_pointer = ((const void **)__builtin_frame_address(1));
#pragma GCC diagnostic pop
#elif defined(PIPE_CC_MSVC) && defined(PIPE_ARCH_X86)
   __asm {
      mov frame_pointer, ebp
   }
   frame_pointer = (const void **)frame_pointer[0];
#else
   frame_pointer = NULL;
#endif

#ifdef PIPE_ARCH_X86
   while (nr_frames) {
      const void **next_frame_pointer;

      if (!frame_pointer)
         break;

      if (start_frame)
         --start_frame;
      else {
         backtrace[i++].function = frame_pointer[1];
         --nr_frames;
      }

      next_frame_pointer = (const void **)frame_pointer[0];

      /* Limit the stack walk to avoid referencing undefined memory */
      if ((uintptr_t)next_frame_pointer <= (uintptr_t)frame_pointer ||
          (uintptr_t)next_frame_pointer > (uintptr_t)frame_pointer + 64*1024)
         break;

      frame_pointer = next_frame_pointer;
   }
#else
   (void) frame_pointer;
#endif

   while (nr_frames) {
      backtrace[i++].function = NULL;
      --nr_frames;
   }
}


void
debug_backtrace_dump(const struct debug_stack_frame *backtrace,
                     unsigned nr_frames)
{
   unsigned i;

   for (i = 0; i < nr_frames; ++i) {
      if (!backtrace[i].function)
         break;
      debug_symbol_print(backtrace[i].function);
   }
}


void
debug_backtrace_print(FILE *f,
                      const struct debug_stack_frame *backtrace,
                      unsigned nr_frames)
{
   unsigned i;

   for (i = 0; i < nr_frames; ++i) {
      const char *symbol;
      if (!backtrace[i].function)
         break;
      symbol = debug_symbol_name_cached(backtrace[i].function);
      if (symbol)
         fprintf(f, "%s\n", symbol);
   }
}

#endif /* HAVE_LIBUNWIND */
