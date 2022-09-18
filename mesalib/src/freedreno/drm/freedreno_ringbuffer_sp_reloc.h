/*
 * Copyright © 2021 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef X
#undef X
#endif

#if PTRSZ == 32
#define X(n) n##_32
#else
#define X(n) n##_64
#endif

static void X(emit_reloc_common)(struct fd_ringbuffer *ring,
                                 const struct fd_reloc *reloc)
{
   (*ring->cur++) = (uint32_t)reloc->iova;
#if PTRSZ == 64
   (*ring->cur++) = (uint32_t)(reloc->iova >> 32);
#endif
}

static void X(fd_ringbuffer_sp_emit_reloc_nonobj)(struct fd_ringbuffer *ring,
                                                  const struct fd_reloc *reloc)
{
   X(emit_reloc_common)(ring, reloc);

   assert(!(ring->flags & _FD_RINGBUFFER_OBJECT));

   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);

   struct fd_submit_sp *fd_submit = to_fd_submit_sp(fd_ring->u.submit);

   fd_submit_append_bo(fd_submit, reloc->bo);
}

static void X(fd_ringbuffer_sp_emit_reloc_obj)(struct fd_ringbuffer *ring,
                                               const struct fd_reloc *reloc)
{
   X(emit_reloc_common)(ring, reloc);

   assert(ring->flags & _FD_RINGBUFFER_OBJECT);

   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);

   /* Avoid emitting duplicate BO references into the list.  Ringbuffer
    * objects are long-lived, so this saves ongoing work at draw time in
    * exchange for a bit at context setup/first draw.  And the number of
    * relocs per ringbuffer object is fairly small, so the O(n^2) doesn't
    * hurt much.
    */
   if (!fd_ringbuffer_references_bo(ring, reloc->bo)) {
      APPEND(&fd_ring->u, reloc_bos, fd_bo_ref(reloc->bo));
   }
}

static uint32_t X(fd_ringbuffer_sp_emit_reloc_ring)(
   struct fd_ringbuffer *ring, struct fd_ringbuffer *target, uint32_t cmd_idx)
{
   struct fd_ringbuffer_sp *fd_target = to_fd_ringbuffer_sp(target);
   struct fd_bo *bo;
   uint32_t size;

   if ((target->flags & FD_RINGBUFFER_GROWABLE) &&
       (cmd_idx < fd_target->u.nr_cmds)) {
      bo = fd_target->u.cmds[cmd_idx].ring_bo;
      size = fd_target->u.cmds[cmd_idx].size;
   } else {
      bo = fd_target->ring_bo;
      size = offset_bytes(target->cur, target->start);
   }

   if (ring->flags & _FD_RINGBUFFER_OBJECT) {
      X(fd_ringbuffer_sp_emit_reloc_obj)(ring, &(struct fd_reloc){
                .bo = bo,
                .iova = bo->iova + fd_target->offset,
                .offset = fd_target->offset,
             });
   } else {
      X(fd_ringbuffer_sp_emit_reloc_nonobj)(ring, &(struct fd_reloc){
                .bo = bo,
                .iova = bo->iova + fd_target->offset,
                .offset = fd_target->offset,
             });
   }

   if (!(target->flags & _FD_RINGBUFFER_OBJECT))
      return size;

   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);

   if (ring->flags & _FD_RINGBUFFER_OBJECT) {
      for (unsigned i = 0; i < fd_target->u.nr_reloc_bos; i++) {
         struct fd_bo *target_bo = fd_target->u.reloc_bos[i];
         if (!fd_ringbuffer_references_bo(ring, target_bo))
            APPEND(&fd_ring->u, reloc_bos, fd_bo_ref(target_bo));
      }
   } else {
      // TODO it would be nice to know whether we have already
      // seen this target before.  But hopefully we hit the
      // append_bo() fast path enough for this to not matter:
      struct fd_submit_sp *fd_submit = to_fd_submit_sp(fd_ring->u.submit);

      for (unsigned i = 0; i < fd_target->u.nr_reloc_bos; i++) {
         fd_submit_append_bo(fd_submit, fd_target->u.reloc_bos[i]);
      }
   }

   return size;
}
