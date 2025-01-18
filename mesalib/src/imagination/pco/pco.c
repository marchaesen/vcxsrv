/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file pco.c
 *
 * \brief Main compiler interface.
 */

#include "compiler/glsl_types.h"
#include "pco.h"
#include "pco_internal.h"
#include "util/hash_table.h"
#include "util/list.h"
#include "util/macros.h"
#include "util/ralloc.h"

#include <assert.h>
#include <stdbool.h>

/**
 * \brief PCO compiler context destructor.
 *
 * \param[in,out] ptr PCO compiler context pointer.
 */
static void pco_ctx_destructor(UNUSED void *ptr)
{
   glsl_type_singleton_decref();
}

/**
 * \brief Allocates and sets up a PCO compiler context.
 *
 * \param[in] dev_info Device info.
 * \param[in] mem_ctx Ralloc memory allocation context.
 * \return The PCO compiler context, or NULL on failure.
 */
pco_ctx *pco_ctx_create(const struct pvr_device_info *dev_info, void *mem_ctx)
{
   pco_ctx *ctx = rzalloc_size(mem_ctx, sizeof(*ctx));

   ctx->dev_info = dev_info;

   pco_debug_init();

#ifndef NDEBUG
   /* Ensure NIR debug variables are processed. */
   nir_process_debug_variable();
#endif /* NDEBUG */

   pco_setup_spirv_options(dev_info, &ctx->spirv_options);
   pco_setup_nir_options(dev_info, &ctx->nir_options);

   glsl_type_singleton_init_or_ref();
   ralloc_set_destructor(ctx, pco_ctx_destructor);

   return ctx;
}

/**
 * \brief Returns the device/core-specific SPIR-V to NIR options for a PCO
 * compiler context.
 *
 * \param[in] ctx PCO compiler context.
 * \return The device/core-specific SPIR-V to NIR options.
 */
const struct spirv_to_nir_options *pco_spirv_options(pco_ctx *ctx)
{
   return &ctx->spirv_options;
}

/**
 * \brief Returns the device/core-specific NIR options for a PCO compiler
 * context.
 *
 * \param[in] ctx PCO compiler context.
 * \return The device/core-specific NIR options.
 */
const nir_shader_compiler_options *pco_nir_options(pco_ctx *ctx)
{
   return &ctx->nir_options;
}

/**
 * \brief Allocates and sets up a PCO shader from a NIR shader.
 *
 * \param[in] ctx PCO compiler context.
 * \param[in] nir The NIR shader.
 * \return The PCO shader, or NULL on failure.
 */
pco_shader *pco_shader_create(pco_ctx *ctx, nir_shader *nir, void *mem_ctx)
{
   pco_shader *shader = rzalloc_size(mem_ctx, sizeof(*shader));

   shader->ctx = ctx;
   shader->nir = nir;
   shader->stage = nir->info.stage;
   shader->name = ralloc_strdup(shader, nir->info.name);
   shader->is_internal = nir->info.internal;
   shader->is_grouped = false;
   list_inithead(&shader->funcs);

   return shader;
}

/**
 * \brief Sets up a PCO cf node.
 *
 * \param[in,out] cf_node PCO cf node.
 * \param[in] type CF node type.
 */
static inline void init_cf_node(pco_cf_node *cf_node,
                                enum pco_cf_node_type type)
{
   cf_node->type = type;
   cf_node->parent = NULL;
}

/**
 * \brief Allocates and sets up a PCO function.
 *
 * \param[in,out] shader PCO shader.
 * \param[in] type The function type.
 * \param[in] num_params The number of parameters.
 * \return The PCO function, or NULL on failure.
 */
pco_func *pco_func_create(pco_shader *shader,
                          enum pco_func_type type,
                          unsigned num_params)
{
   pco_func *func = rzalloc_size(shader, sizeof(*func));
   pco_func *preamble = pco_preamble(shader);

   /* Add the function to the shader; preamble goes first, then entrypoint.
    * The rest of the functions will get appended.
    */
   if (type == PCO_FUNC_TYPE_PREAMBLE) {
      assert(!preamble);
      list_add(&func->link, &shader->funcs);
   } else if (type == PCO_FUNC_TYPE_ENTRYPOINT) {
      assert(!pco_entrypoint(shader));
      list_add(&func->link, !preamble ? &shader->funcs : &preamble->link);
   } else {
      list_addtail(&func->link, &shader->funcs);
   }

   init_cf_node(&func->cf_node, PCO_CF_NODE_TYPE_FUNC);
   func->parent_shader = shader;
   func->type = type;
   func->index = shader->next_func++;

   list_inithead(&func->body);

   func->num_params = num_params;
   if (num_params) {
      func->params =
         rzalloc_array_size(func, sizeof(*func->params), num_params);
   }

   func->vec_infos = _mesa_hash_table_u64_create(func);

   func->enc_offset = ~0U;

   return func;
}

/**
 * \brief Allocates and sets up a PCO block.
 *
 * \param[in,out] func Parent function.
 * \return The PCO block, or NULL on failure.
 */
pco_block *pco_block_create(pco_func *func)
{
   pco_block *block = rzalloc_size(func, sizeof(*block));

   init_cf_node(&block->cf_node, PCO_CF_NODE_TYPE_BLOCK);
   block->parent_func = func;
   list_inithead(&block->instrs);
   block->index = func->next_block++;

   return block;
}

/**
 * \brief Allocates and sets up a PCO if construct.
 *
 * \param[in,out] func Parent function.
 * \return The PCO if construct, or NULL on failure.
 */
pco_if *pco_if_create(pco_func *func)
{
   pco_if *pif = rzalloc_size(func, sizeof(*pif));

   init_cf_node(&pif->cf_node, PCO_CF_NODE_TYPE_IF);
   pif->parent_func = func;
   list_inithead(&pif->then_body);
   list_inithead(&pif->else_body);
   pif->index = func->next_if++;

   return pif;
}

/**
 * \brief Allocates and sets up a PCO loop.
 *
 * \param[in,out] func Parent function.
 * \return The PCO loop, or NULL on failure.
 */
pco_loop *pco_loop_create(pco_func *func)
{
   pco_loop *loop = rzalloc_size(func, sizeof(*loop));

   init_cf_node(&loop->cf_node, PCO_CF_NODE_TYPE_LOOP);
   loop->parent_func = func;
   list_inithead(&loop->body);
   loop->index = func->next_loop++;

   return loop;
}

/**
 * \brief Allocates and sets up a PCO instruction.
 *
 * \param[in,out] func Parent function.
 * \param[in] op Instruction op.
 * \param[in] num_dests Number of destinations.
 * \param[in] num_srcs Number of sources.
 * \return The PCO instruction, or NULL on failure.
 */
pco_instr *pco_instr_create(pco_func *func,
                            enum pco_op op,
                            unsigned num_dests,
                            unsigned num_srcs)
{
   pco_instr *instr;
   unsigned size = sizeof(*instr);
   size += num_dests * sizeof(*instr->dest);
   size += num_srcs * sizeof(*instr->src);

   instr = rzalloc_size(func, size);

   instr->parent_func = func;

   instr->op = op;

   instr->num_dests = num_dests;
   instr->dest = (pco_ref *)(instr + 1);

   instr->num_srcs = num_srcs;
   instr->src = instr->dest + num_dests;

   list_inithead(&instr->phi_srcs);

   instr->index = func->next_instr++;

   return instr;
}

/**
 * \brief Allocates and sets up a PCO instruction group.
 *
 * \param[in,out] func Parent function.
 * \return The PCO instruction group, or NULL on failure.
 */
pco_igrp *pco_igrp_create(pco_func *func)
{
   pco_igrp *igrp = rzalloc_size(func, sizeof(*igrp));

   igrp->parent_func = func;
   igrp->index = func->next_igrp++;

   return igrp;
}

/**
 * \brief Deletes a PCO instruction.
 *
 * \param[in,out] instr PCO instruction.
 */
void pco_instr_delete(pco_instr *instr)
{
   list_del(&instr->link);
   ralloc_free(instr);
}

/**
 * \brief Returns the shader data.
 *
 * \param[in] shader PCO shader.
 */
pco_data *pco_shader_data(pco_shader *shader)
{
   return &shader->data;
}
