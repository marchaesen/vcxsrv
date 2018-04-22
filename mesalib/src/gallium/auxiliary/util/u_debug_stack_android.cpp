/*
 * Copyright (C) 2018 Stefan Schake <stschake@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <backtrace/Backtrace.h>

#include "u_debug.h"
#include "u_debug_stack.h"
#include "util/hash_table.h"
#include "os/os_thread.h"

static hash_table *backtrace_table;
static mtx_t table_mutex = _MTX_INITIALIZER_NP;

void
debug_backtrace_capture(debug_stack_frame *mesa_backtrace,
                        unsigned start_frame,
                        unsigned nr_frames)
{
   hash_entry *backtrace_entry;
   Backtrace *backtrace;
   pid_t tid = gettid();

   if (!nr_frames)
      return;

   /* We keep an Android Backtrace handler around for each thread */
   mtx_lock(&table_mutex);
   if (!backtrace_table)
      backtrace_table = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                                _mesa_key_pointer_equal);

   backtrace_entry = _mesa_hash_table_search(backtrace_table, (void*) tid);
   if (!backtrace_entry) {
      backtrace = Backtrace::Create(getpid(), tid);
      _mesa_hash_table_insert(backtrace_table, (void*) tid, backtrace);
   } else {
      backtrace = (Backtrace *) backtrace_entry->data;
   }
   mtx_unlock(&table_mutex);

   /* Add one to exclude this call. Unwind already ignores itself. */
   backtrace->Unwind(start_frame + 1);

   /* Store the Backtrace handler in the first mesa frame for reference.
    * Unwind will generally return less frames than nr_frames specified
    * but we have no good way of storing the real count otherwise.
    * The Backtrace handler only stores the results until the next Unwind,
    * but that is how u_debug_stack is used anyway.
    */
   mesa_backtrace->function = backtrace;
}

void
debug_backtrace_dump(const debug_stack_frame *mesa_backtrace,
                     unsigned nr_frames)
{
   Backtrace *backtrace = (Backtrace *) mesa_backtrace->function;
   size_t i;

   if (!nr_frames)
      return;

   if (nr_frames > backtrace->NumFrames())
      nr_frames = backtrace->NumFrames();
   for (i = 0; i < nr_frames; i++) {
      /* There is no prescribed format and this isn't interpreted further,
       * so we simply use the default Android format.
       */
      const std::string& frame_line = backtrace->FormatFrameData(i);
      debug_printf("%s\n", frame_line.c_str());
   }
}

void
debug_backtrace_print(FILE *f,
                      const debug_stack_frame *mesa_backtrace,
                      unsigned nr_frames)
{
   Backtrace *backtrace = (Backtrace *) mesa_backtrace->function;
   size_t i;

   if (!nr_frames)
      return;

   if (nr_frames > backtrace->NumFrames())
      nr_frames = backtrace->NumFrames();
   for (i = 0; i < nr_frames; i++) {
      const std::string& frame_line = backtrace->FormatFrameData(i);
      fprintf(f, "%s\n", frame_line.c_str());
   }
}
