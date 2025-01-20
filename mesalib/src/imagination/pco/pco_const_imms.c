/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco_const_imms.c
 *
 * \brief PCO constant immediates lowering pass.
 */

#include "pco.h"
#include "pco_builder.h"
#include "pco_internal.h"
#include "util/macros.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

/** Mapping of constant register values and their indices. */
struct const_reg_def {
   uint32_t val;
   uint8_t idx;
   bool flr : 1;
   bool neg : 1;
};

/** Constant register values (sorted for bsearch). */
static const struct const_reg_def const_reg_defs[] = {
   { 0x00000000, 0, false, false },   { 0x00000001, 1, false, false },
   { 0x00000002, 2, false, false },   { 0x00000003, 3, false, false },
   { 0x00000004, 4, false, false },   { 0x00000005, 5, false, false },
   { 0x00000006, 6, false, false },   { 0x00000007, 7, false, false },
   { 0x00000008, 8, false, false },   { 0x00000009, 9, false, false },
   { 0x0000000a, 10, false, false },  { 0x0000000b, 11, false, false },
   { 0x0000000c, 12, false, false },  { 0x0000000d, 13, false, false },
   { 0x0000000e, 14, false, false },  { 0x0000000f, 15, false, false },
   { 0x00000010, 16, false, false },  { 0x00000011, 17, false, false },
   { 0x00000012, 18, false, false },  { 0x00000013, 19, false, false },
   { 0x00000014, 20, false, false },  { 0x00000015, 21, false, false },
   { 0x00000016, 22, false, false },  { 0x00000017, 23, false, false },
   { 0x00000018, 24, false, false },  { 0x00000019, 25, false, false },
   { 0x0000001a, 26, false, false },  { 0x0000001b, 27, false, false },
   { 0x0000001c, 28, false, false },  { 0x0000001d, 29, false, false },
   { 0x0000001e, 30, false, false },  { 0x0000001f, 31, false, false },
   { 0x0000007f, 147, false, false }, { 0x37800000, 134, false, false },
   { 0x38000000, 135, false, false }, { 0x38800000, 88, false, false },
   { 0x39000000, 87, false, false },  { 0x39800000, 86, false, false },
   { 0x3a000000, 85, false, false },  { 0x3a800000, 84, false, false },
   { 0x3b000000, 83, false, false },  { 0x3b4d2e1c, 136, false, false },
   { 0x3b800000, 82, false, false },  { 0x3c000000, 81, false, false },
   { 0x3c800000, 80, false, false },  { 0x3d000000, 79, false, false },
   { 0x3d25aee6, 156, false, false }, { 0x3d6147ae, 140, false, false },
   { 0x3d800000, 78, false, false },  { 0x3d9e8391, 157, false, false },
   { 0x3e000000, 77, false, false },  { 0x3e2aaaab, 153, false, false },
   { 0x3e800000, 76, false, false },  { 0x3e9a209b, 145, false, false },
   { 0x3ea2f983, 128, false, false }, { 0x3eaaaaab, 152, false, false },
   { 0x3ebc5ab2, 90, false, false },  { 0x3ed55555, 138, false, false },
   { 0x3f000000, 75, false, false },  { 0x3f22f983, 129, false, false },
   { 0x3f317218, 146, false, false }, { 0x3f3504f3, 92, false, false },
   { 0x3f490fdb, 93, false, false },  { 0x3f72a76f, 158, false, false },
   { 0x3f800000, 64, false, false },  { 0x3f860a92, 151, false, false },
   { 0x3f870a3d, 139, false, false }, { 0x3fa2f983, 130, false, false },
   { 0x3fb504f3, 91, false, false },  { 0x3fb8aa3b, 155, false, false },
   { 0x3fc90fdb, 94, false, false },  { 0x40000000, 65, false, false },
   { 0x4019999a, 159, false, false }, { 0x402df854, 89, false, false },
   { 0x40400000, 95, true, false },   { 0x40490fdb, 95, false, false },
   { 0x40549a78, 154, false, false }, { 0x40800000, 66, false, false },
   { 0x40c00000, 131, true, false },  { 0x40c90fdb, 131, false, false },
   { 0x41000000, 67, false, false },  { 0x41400000, 132, true, false },
   { 0x41490fdb, 132, false, false }, { 0x414eb852, 137, false, false },
   { 0x41800000, 68, false, false },  { 0x41c80000, 133, true, false },
   { 0x41c90fdb, 133, false, false }, { 0x42000000, 69, false, false },
   { 0x42800000, 70, false, false },  { 0x43000000, 71, false, false },
   { 0x43800000, 72, false, false },  { 0x44000000, 73, false, false },
   { 0x44800000, 74, false, false },  { 0x4b000000, 149, false, false },
   { 0x4b800000, 150, false, false }, { 0x7f7fffff, 148, false, false },
   { 0x7f800000, 142, false, false }, { 0x7fff7fff, 144, false, false },
   { 0x7fffffff, 143, false, true },  { 0x80000000, 141, false, false },
   { 0x80000001, 1, false, true },    { 0x80000002, 2, false, true },
   { 0x80000003, 3, false, true },    { 0x80000004, 4, false, true },
   { 0x80000005, 5, false, true },    { 0x80000006, 6, false, true },
   { 0x80000007, 7, false, true },    { 0x80000008, 8, false, true },
   { 0x80000009, 9, false, true },    { 0x8000000a, 10, false, true },
   { 0x8000000b, 11, false, true },   { 0x8000000c, 12, false, true },
   { 0x8000000d, 13, false, true },   { 0x8000000e, 14, false, true },
   { 0x8000000f, 15, false, true },   { 0x80000010, 16, false, true },
   { 0x80000011, 17, false, true },   { 0x80000012, 18, false, true },
   { 0x80000013, 19, false, true },   { 0x80000014, 20, false, true },
   { 0x80000015, 21, false, true },   { 0x80000016, 22, false, true },
   { 0x80000017, 23, false, true },   { 0x80000018, 24, false, true },
   { 0x80000019, 25, false, true },   { 0x8000001a, 26, false, true },
   { 0x8000001b, 27, false, true },   { 0x8000001c, 28, false, true },
   { 0x8000001d, 29, false, true },   { 0x8000001e, 30, false, true },
   { 0x8000001f, 31, false, true },   { 0x8000007f, 147, false, true },
   { 0xb7800000, 134, false, true },  { 0xb8000000, 135, false, true },
   { 0xb8800000, 88, false, true },   { 0xb9000000, 87, false, true },
   { 0xb9800000, 86, false, true },   { 0xba000000, 85, false, true },
   { 0xba800000, 84, false, true },   { 0xbb000000, 83, false, true },
   { 0xbb4d2e1c, 136, false, true },  { 0xbb800000, 82, false, true },
   { 0xbc000000, 81, false, true },   { 0xbc800000, 80, false, true },
   { 0xbd000000, 79, false, true },   { 0xbd25aee6, 156, false, true },
   { 0xbd6147ae, 140, false, true },  { 0xbd800000, 78, false, true },
   { 0xbd9e8391, 157, false, true },  { 0xbe000000, 77, false, true },
   { 0xbe2aaaab, 153, false, true },  { 0xbe800000, 76, false, true },
   { 0xbe9a209b, 145, false, true },  { 0xbea2f983, 128, false, true },
   { 0xbeaaaaab, 152, false, true },  { 0xbebc5ab2, 90, false, true },
   { 0xbed55555, 138, false, true },  { 0xbf000000, 75, false, true },
   { 0xbf22f983, 129, false, true },  { 0xbf317218, 146, false, true },
   { 0xbf3504f3, 92, false, true },   { 0xbf490fdb, 93, false, true },
   { 0xbf72a76f, 158, false, true },  { 0xbf800000, 64, false, true },
   { 0xbf860a92, 151, false, true },  { 0xbf870a3d, 139, false, true },
   { 0xbfa2f983, 130, false, true },  { 0xbfb504f3, 91, false, true },
   { 0xbfb8aa3b, 155, false, true },  { 0xbfc90fdb, 94, false, true },
   { 0xc0000000, 65, false, true },   { 0xc019999a, 159, false, true },
   { 0xc02df854, 89, false, true },   { 0xc0400000, 95, true, true },
   { 0xc0490fdb, 95, false, true },   { 0xc0549a78, 154, false, true },
   { 0xc0800000, 66, false, true },   { 0xc0c00000, 131, true, true },
   { 0xc0c90fdb, 131, false, true },  { 0xc1000000, 67, false, true },
   { 0xc1400000, 132, true, true },   { 0xc1490fdb, 132, false, true },
   { 0xc14eb852, 137, false, true },  { 0xc1800000, 68, false, true },
   { 0xc1c80000, 133, true, true },   { 0xc1c90fdb, 133, false, true },
   { 0xc2000000, 69, false, true },   { 0xc2800000, 70, false, true },
   { 0xc3000000, 71, false, true },   { 0xc3800000, 72, false, true },
   { 0xc4000000, 73, false, true },   { 0xc4800000, 74, false, true },
   { 0xcb000000, 149, false, true },  { 0xcb800000, 150, false, true },
   { 0xff7fffff, 148, false, true },  { 0xff800000, 142, false, true },
   { 0xffff7fff, 144, false, true },  { 0xffffffff, 143, false, false },
};

/**
 * \brief Comparison function for bsearch() to support rogue_const_reg_def.
 *
 * \param[in] lhs The left hand side of the comparison.
 * \param[in] rhs The right hand side of the comparison.
 * \return 0 if (lhs == rhs), -1 if (lhs < rhs), 1 if (lhs > rhs).
 */
static int constreg_cmp(const void *lhs, const void *rhs)
{
   const struct const_reg_def *l = lhs;
   const struct const_reg_def *r = rhs;

   if (l->val < r->val)
      return -1;
   else if (l->val > r->val)
      return 1;

   return 0;
}

/**
 * \brief Looks up an immediate in constant registers.
 *
 * \param[in] imm The immediate to lookup.
 * \return A pointer to the constant register definition,
 *         or NULL if no constant register was found.
 */
inline static const struct const_reg_def *constreg_lookup(uint32_t imm)
{
   return bsearch(&(struct const_reg_def){ .val = imm },
                  const_reg_defs,
                  ARRAY_SIZE(const_reg_defs),
                  sizeof(*const_reg_defs),
                  constreg_cmp);
}

/**
 * \brief Converts immediates into constant register lookups where possible.
 *
 * \param[in,out] shader PCO shader.
 * \return True if the pass made progress.
 */
bool pco_const_imms(pco_shader *shader)
{
   bool progress = false;

   pco_foreach_func_in_shader (func, shader) {
      pco_foreach_instr_in_func_safe (instr, func) {
         if (instr->op != PCO_OP_MOVI32)
            continue;

         const struct const_reg_def *const_reg_def =
            constreg_lookup(pco_ref_get_imm(instr->src[0]));

         if (!const_reg_def)
            continue;

         pco_builder b =
            pco_builder_create(func, pco_cursor_before_instr(instr));

         pco_ref dest = instr->dest[0];
         pco_ref const_reg =
            pco_ref_hwreg(const_reg_def->idx, PCO_REG_CLASS_CONST);

         if (!const_reg_def->flr && !const_reg_def->neg) {
            pco_mov(&b, dest, const_reg);
         } else if (!const_reg_def->flr && const_reg_def->neg) {
            pco_neg(&b, dest, const_reg);
         } else if (const_reg_def->flr && !const_reg_def->neg) {
            pco_flr(&b, dest, const_reg);
         } else {
            /* TODO: use floor and neg mods when support for > 1 is added. */
            const_reg = pco_ref_flr(const_reg);
            const_reg = pco_ref_neg(const_reg);

            pco_fadd(&b, dest, const_reg, pco_zero);
         }

         pco_instr_delete(instr);
         progress = true;
      }
   }

   return progress;
}
