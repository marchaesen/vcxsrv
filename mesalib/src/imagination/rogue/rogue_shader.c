/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

#include "rogue_shader.h"
#include "rogue_instr.h"
#include "rogue_regalloc.h"
#include "rogue_util.h"
#include "util/ralloc.h"

/**
 * \file rogue_shader.c
 *
 * \brief Contains functions to manipulate Rogue shaders.
 */

/**
 * \brief Counts how many times an instruction is used in a shader.
 *
 * \param[in] shader The shader containing instructions to count.
 * \param[in] opcode The opcode of the instruction to be counted.
 * \return The number of times "opcode" is present, or 0 on error.
 */
size_t rogue_shader_instr_count_type(const struct rogue_shader *shader,
                                     enum rogue_opcode opcode)
{
   size_t count = 0U;

   ASSERT_OPCODE_RANGE(opcode);

   foreach_instr (instr, &shader->instr_list)
      if (instr->opcode == opcode)
         ++count;

   return count;
}

/**
 * \brief Allocates and sets up a Rogue shader.
 *
 * \param[in] stage The shader stage.
 * \return A rogue_shader* if successful, or NULL if unsuccessful.
 */
struct rogue_shader *rogue_shader_create(struct rogue_build_ctx *ctx,
                                         gl_shader_stage stage)
{
   struct rogue_shader *shader;

   if (!ctx)
      return NULL;

   shader = rzalloc_size(ctx, sizeof(*shader));
   if (!shader)
      return NULL;

   shader->stage = stage;

   list_inithead(&shader->instr_list);

   shader->ctx = ctx;
   shader->ra = rogue_ra_init(shader);
   if (!shader->ra) {
      ralloc_free(shader);
      return NULL;
   }

   return shader;
}

/**
 * \brief Creates an instruction and appends it to a Rogue shader.
 *
 * \param[in] shader The shader.
 * \param[in] opcode The instruction opcode.
 * \return A rogue_instr* if successful, or NULL if unsuccessful.
 */
struct rogue_instr *rogue_shader_insert(struct rogue_shader *shader,
                                        enum rogue_opcode opcode)
{
   struct rogue_instr *instr = rogue_instr_create(shader, opcode);
   if (!instr)
      return NULL;

   list_addtail(&instr->node, &shader->instr_list);

   return instr;
}

size_t rogue_acquire_drc(struct rogue_shader *shader)
{
   size_t drc;

   /* If both DRCs are in use, we have a problem. */
   if (shader->drc_used[0] && shader->drc_used[1])
      return SIZE_MAX;

   drc = !shader->drc_used[0] ? 0 : 1;
   shader->drc_used[drc] = true;

   return drc;
}

void rogue_release_drc(struct rogue_shader *shader, size_t drc)
{
   assert(drc < ROGUE_NUM_DRCS);
   assert(shader->drc_used[drc]);

   shader->drc_used[drc] = false;
}
