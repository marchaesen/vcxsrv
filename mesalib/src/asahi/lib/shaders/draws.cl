/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */
#include "libagx.h"
#include "draws.h"

/*
 * To implement drawIndirectCount generically, we dispatch a kernel to
 * clone-and-patch the indirect buffer, predicating out draws as appropriate.
 */
void
libagx_predicate_indirect(constant struct libagx_predicate_indirect_push *push,
                          uint draw, bool indexed)
{
   uint words = indexed ? 5 : 4;
   global uint *out = &push->out[draw * words];
   constant uint *in = &push->in[draw * push->stride_el];
   bool enabled = draw < *(push->draw_count);

   /* Copy enabled draws, zero predicated draws. */
   for (uint i = 0; i < words; ++i) {
      out[i] = enabled ? in[i] : 0;
   }
}
