/*
 * Copyright © 2019 Google LLC
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#ifndef TU_CS_H
#define TU_CS_H

#include "tu_private.h"

#include "adreno_pm4.xml.h"

void
tu_cs_init(struct tu_cs *cs,
           struct tu_device *device,
           enum tu_cs_mode mode,
           uint32_t initial_size);

void
tu_cs_init_external(struct tu_cs *cs, uint32_t *start, uint32_t *end);

void
tu_cs_finish(struct tu_cs *cs);

void
tu_cs_begin(struct tu_cs *cs);

void
tu_cs_end(struct tu_cs *cs);

VkResult
tu_cs_begin_sub_stream(struct tu_cs *cs, uint32_t size, struct tu_cs *sub_cs);

VkResult
tu_cs_alloc(struct tu_cs *cs,
            uint32_t count,
            uint32_t size,
            struct tu_cs_memory *memory);

struct tu_cs_entry
tu_cs_end_sub_stream(struct tu_cs *cs, struct tu_cs *sub_cs);

static inline struct tu_draw_state
tu_cs_end_draw_state(struct tu_cs *cs, struct tu_cs *sub_cs)
{
   struct tu_cs_entry entry = tu_cs_end_sub_stream(cs, sub_cs);
   return (struct tu_draw_state) {
      .iova = entry.bo->iova + entry.offset,
      .size = entry.size / sizeof(uint32_t),
   };
}

VkResult
tu_cs_reserve_space(struct tu_cs *cs, uint32_t reserved_size);

static inline struct tu_draw_state
tu_cs_draw_state(struct tu_cs *sub_cs, struct tu_cs *cs, uint32_t size)
{
   struct tu_cs_memory memory;

   /* TODO: clean this up */
   tu_cs_alloc(sub_cs, size, 1, &memory);
   tu_cs_init_external(cs, memory.map, memory.map + size);
   tu_cs_begin(cs);
   tu_cs_reserve_space(cs, size);

   return (struct tu_draw_state) {
      .iova = memory.iova,
      .size = size,
   };
}

void
tu_cs_reset(struct tu_cs *cs);

VkResult
tu_cs_add_entries(struct tu_cs *cs, struct tu_cs *target);

/**
 * Get the size of the command packets emitted since the last call to
 * tu_cs_add_entry.
 */
static inline uint32_t
tu_cs_get_size(const struct tu_cs *cs)
{
   return cs->cur - cs->start;
}

/**
 * Return true if there is no command packet emitted since the last call to
 * tu_cs_add_entry.
 */
static inline uint32_t
tu_cs_is_empty(const struct tu_cs *cs)
{
   return tu_cs_get_size(cs) == 0;
}

/**
 * Discard all entries.  This allows \a cs to be reused while keeping the
 * existing BOs and command packets intact.
 */
static inline void
tu_cs_discard_entries(struct tu_cs *cs)
{
   assert(cs->mode == TU_CS_MODE_GROW);
   cs->entry_count = 0;
}

/**
 * Get the size needed for tu_cs_emit_call.
 */
static inline uint32_t
tu_cs_get_call_size(const struct tu_cs *cs)
{
   assert(cs->mode == TU_CS_MODE_GROW);
   /* each CP_INDIRECT_BUFFER needs 4 dwords */
   return cs->entry_count * 4;
}

/**
 * Assert that we did not exceed the reserved space.
 */
static inline void
tu_cs_sanity_check(const struct tu_cs *cs)
{
   assert(cs->start <= cs->cur);
   assert(cs->cur <= cs->reserved_end);
   assert(cs->reserved_end <= cs->end);
}

/**
 * Emit a uint32_t value into a command stream, without boundary checking.
 */
static inline void
tu_cs_emit(struct tu_cs *cs, uint32_t value)
{
   assert(cs->cur < cs->reserved_end);
   *cs->cur = value;
   ++cs->cur;
}

/**
 * Emit an array of uint32_t into a command stream, without boundary checking.
 */
static inline void
tu_cs_emit_array(struct tu_cs *cs, const uint32_t *values, uint32_t length)
{
   assert(cs->cur + length <= cs->reserved_end);
   memcpy(cs->cur, values, sizeof(uint32_t) * length);
   cs->cur += length;
}

static inline unsigned
tu_odd_parity_bit(unsigned val)
{
   /* See: http://graphics.stanford.edu/~seander/bithacks.html#ParityParallel
    * note that we want odd parity so 0x6996 is inverted.
    */
   val ^= val >> 16;
   val ^= val >> 8;
   val ^= val >> 4;
   val &= 0xf;
   return (~0x6996 >> val) & 1;
}

/**
 * Get the size of the remaining space in the current BO.
 */
static inline uint32_t
tu_cs_get_space(const struct tu_cs *cs)
{
   return cs->end - cs->cur;
}

static inline void
tu_cs_reserve(struct tu_cs *cs, uint32_t reserved_size)
{
   if (cs->mode != TU_CS_MODE_GROW) {
      assert(tu_cs_get_space(cs) >= reserved_size);
      assert(cs->reserved_end == cs->end);
      return;
   }

   if (tu_cs_get_space(cs) >= reserved_size &&
       cs->entry_count < cs->entry_capacity) {
      cs->reserved_end = cs->cur + reserved_size;
      return;
   }

   VkResult result = tu_cs_reserve_space(cs, reserved_size);
   /* TODO: set this error in tu_cs and use it */
   assert(result == VK_SUCCESS);
}

/**
 * Emit a type-4 command packet header into a command stream.
 */
static inline void
tu_cs_emit_pkt4(struct tu_cs *cs, uint16_t regindx, uint16_t cnt)
{
   tu_cs_reserve(cs, cnt + 1);
   tu_cs_emit(cs, CP_TYPE4_PKT | cnt | (tu_odd_parity_bit(cnt) << 7) |
                     ((regindx & 0x3ffff) << 8) |
                     ((tu_odd_parity_bit(regindx) << 27)));
}

/**
 * Emit a type-7 command packet header into a command stream.
 */
static inline void
tu_cs_emit_pkt7(struct tu_cs *cs, uint8_t opcode, uint16_t cnt)
{
   tu_cs_reserve(cs, cnt + 1);
   tu_cs_emit(cs, CP_TYPE7_PKT | cnt | (tu_odd_parity_bit(cnt) << 15) |
                     ((opcode & 0x7f) << 16) |
                     ((tu_odd_parity_bit(opcode) << 23)));
}

static inline void
tu_cs_emit_wfi(struct tu_cs *cs)
{
   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_IDLE, 0);
}

static inline void
tu_cs_emit_qw(struct tu_cs *cs, uint64_t value)
{
   tu_cs_emit(cs, (uint32_t) value);
   tu_cs_emit(cs, (uint32_t) (value >> 32));
}

static inline void
tu_cs_emit_write_reg(struct tu_cs *cs, uint16_t reg, uint32_t value)
{
   tu_cs_emit_pkt4(cs, reg, 1);
   tu_cs_emit(cs, value);
}

/**
 * Emit a CP_INDIRECT_BUFFER command packet.
 */
static inline void
tu_cs_emit_ib(struct tu_cs *cs, const struct tu_cs_entry *entry)
{
   assert(entry->bo);
   assert(entry->size && entry->offset + entry->size <= entry->bo->size);
   assert(entry->size % sizeof(uint32_t) == 0);
   assert(entry->offset % sizeof(uint32_t) == 0);

   tu_cs_emit_pkt7(cs, CP_INDIRECT_BUFFER, 3);
   tu_cs_emit_qw(cs, entry->bo->iova + entry->offset);
   tu_cs_emit(cs, entry->size / sizeof(uint32_t));
}

/* for compute which isn't using SET_DRAW_STATE */
static inline void
tu_cs_emit_state_ib(struct tu_cs *cs, struct tu_draw_state state)
{
   if (state.size) {
      tu_cs_emit_pkt7(cs, CP_INDIRECT_BUFFER, 3);
      tu_cs_emit_qw(cs, state.iova);
      tu_cs_emit(cs, state.size);
   }
}

/**
 * Emit a CP_INDIRECT_BUFFER command packet for each entry in the target
 * command stream.
 */
static inline void
tu_cs_emit_call(struct tu_cs *cs, const struct tu_cs *target)
{
   assert(target->mode == TU_CS_MODE_GROW);
   for (uint32_t i = 0; i < target->entry_count; i++)
      tu_cs_emit_ib(cs, target->entries + i);
}

/* Helpers for bracketing a large sequence of commands of unknown size inside
 * a CP_COND_REG_EXEC packet.
 */
static inline void
tu_cond_exec_start(struct tu_cs *cs, uint32_t cond_flags)
{
   assert(cs->mode == TU_CS_MODE_GROW);
   assert(!cs->cond_flags && cond_flags);

   tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
   tu_cs_emit(cs, cond_flags);

   cs->cond_flags = cond_flags;
   cs->cond_dwords = cs->cur;

   /* Emit dummy DWORD field here */
   tu_cs_emit(cs, CP_COND_REG_EXEC_1_DWORDS(0));
}
#define CP_COND_EXEC_0_RENDER_MODE_GMEM \
   (CP_COND_REG_EXEC_0_MODE(RENDER_MODE) | CP_COND_REG_EXEC_0_GMEM)
#define CP_COND_EXEC_0_RENDER_MODE_SYSMEM \
   (CP_COND_REG_EXEC_0_MODE(RENDER_MODE) | CP_COND_REG_EXEC_0_SYSMEM)

static inline void
tu_cond_exec_end(struct tu_cs *cs)
{
   assert(cs->cond_flags);

   cs->cond_flags = 0;
   /* Subtract one here to account for the DWORD field itself. */
   *cs->cond_dwords = cs->cur - cs->cond_dwords - 1;
}

#define fd_reg_pair tu_reg_value
#define __bo_type struct tu_bo *

#include "a6xx.xml.h"
#include "a6xx-pack.xml.h"

#define __assert_eq(a, b)                                               \
   do {                                                                 \
      if ((a) != (b)) {                                                 \
         fprintf(stderr, "assert failed: " #a " (0x%x) != " #b " (0x%x)\n", a, b); \
         assert((a) == (b));                                            \
      }                                                                 \
   } while (0)

#define __ONE_REG(i, regs)                                      \
   do {                                                         \
      if (i < ARRAY_SIZE(regs) && regs[i].reg > 0) {            \
         __assert_eq(regs[0].reg + i, regs[i].reg);             \
         if (regs[i].bo) {                                      \
            uint64_t v = regs[i].bo->iova + regs[i].bo_offset;  \
            v >>= regs[i].bo_shift;                             \
            v |= regs[i].value;                                 \
                                                                \
            *p++ = v;                                           \
            *p++ = v >> 32;                                     \
         } else {                                               \
            *p++ = regs[i].value;                               \
            if (regs[i].is_address)                             \
               *p++ = regs[i].value >> 32;                      \
         }                                                      \
      }                                                         \
   } while (0)

/* Emits a sequence of register writes in order using a pkt4.  This will check
 * (at runtime on a !NDEBUG build) that the registers were actually set up in
 * order in the code.
 *
 * Note that references to buffers aren't automatically added to the CS,
 * unlike in freedreno.  We are clever in various places to avoid duplicating
 * the reference add work.
 *
 * Also, 64-bit address registers don't have a way (currently) to set a 64-bit
 * address without having a reference to a BO, since the .dword field in the
 * register's struct is only 32-bit wide.  We should fix this in the pack
 * codegen later.
 */
#define tu_cs_emit_regs(cs, ...) do {                   \
   const struct fd_reg_pair regs[] = { __VA_ARGS__ };   \
   unsigned count = ARRAY_SIZE(regs);                   \
                                                        \
   STATIC_ASSERT(count > 0);                            \
   STATIC_ASSERT(count <= 16);                          \
                                                        \
   tu_cs_emit_pkt4((cs), regs[0].reg, count);             \
   uint32_t *p = (cs)->cur;                               \
   __ONE_REG( 0, regs);                                 \
   __ONE_REG( 1, regs);                                 \
   __ONE_REG( 2, regs);                                 \
   __ONE_REG( 3, regs);                                 \
   __ONE_REG( 4, regs);                                 \
   __ONE_REG( 5, regs);                                 \
   __ONE_REG( 6, regs);                                 \
   __ONE_REG( 7, regs);                                 \
   __ONE_REG( 8, regs);                                 \
   __ONE_REG( 9, regs);                                 \
   __ONE_REG(10, regs);                                 \
   __ONE_REG(11, regs);                                 \
   __ONE_REG(12, regs);                                 \
   __ONE_REG(13, regs);                                 \
   __ONE_REG(14, regs);                                 \
   __ONE_REG(15, regs);                                 \
   (cs)->cur = p;                                         \
   } while (0)

#endif /* TU_CS_H */
