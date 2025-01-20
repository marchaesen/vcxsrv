/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_INTERNAL_H
#define PCO_INTERNAL_H

/**
 * \file pco_internal.h
 *
 * \brief PCO internal header.
 */

#include "compiler/spirv/nir_spirv.h"
#include "hwdef/rogue_hw_utils.h"
#include "pco.h"
#include "pco_common.h"
#include "pco_data.h"
#include "pco_ops.h"
#include "spirv/nir_spirv.h"
#include "util/compiler.h"
#include "util/hash_table.h"
#include "util/macros.h"
#include "util/list.h"
#include "util/u_dynarray.h"
#include "util/u_math.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>

/** PCO compiler context. */
typedef struct _pco_ctx {
   /** Device information. */
   const struct pvr_device_info *dev_info;

   /** Device-specific NIR options. */
   nir_shader_compiler_options nir_options;

   /** Device-specific SPIR-V to NIR options. */
   struct spirv_to_nir_options spirv_options;
} pco_ctx;

void pco_setup_spirv_options(const struct pvr_device_info *dev_info,
                             struct spirv_to_nir_options *spirv_options);
void pco_setup_nir_options(const struct pvr_device_info *dev_info,
                           nir_shader_compiler_options *nir_options);

/* Debug. */
enum pco_debug {
   PCO_DEBUG_VAL_SKIP = BITFIELD64_BIT(0),
   PCO_DEBUG_REINDEX = BITFIELD64_BIT(1),
};

extern uint64_t pco_debug;

#define PCO_DEBUG(flag) unlikely(pco_debug &(PCO_DEBUG_##flag))

enum pco_debug_print {
   PCO_DEBUG_PRINT_VS = BITFIELD64_BIT(0),
   PCO_DEBUG_PRINT_FS = BITFIELD64_BIT(1),
   PCO_DEBUG_PRINT_CS = BITFIELD64_BIT(2),
   PCO_DEBUG_PRINT_ALL = PCO_DEBUG_PRINT_VS | PCO_DEBUG_PRINT_FS |
                         PCO_DEBUG_PRINT_CS,
   PCO_DEBUG_PRINT_INTERNAL = BITFIELD64_BIT(3),
   PCO_DEBUG_PRINT_PASSES = BITFIELD64_BIT(4),
   PCO_DEBUG_PRINT_NIR = BITFIELD64_BIT(5),
   PCO_DEBUG_PRINT_BINARY = BITFIELD64_BIT(6),
   PCO_DEBUG_PRINT_VERBOSE = BITFIELD64_BIT(7),
   PCO_DEBUG_PRINT_RA = BITFIELD64_BIT(8),
};

extern uint64_t pco_debug_print;

extern const char *pco_skip_passes;

#define PCO_DEBUG_PRINT(flag) \
   unlikely(pco_debug_print &(PCO_DEBUG_PRINT_##flag))

extern bool pco_color;

void pco_debug_init(void);

typedef struct _pco_cf_node pco_cf_node;
typedef struct _pco_func pco_func;
typedef struct _pco_block pco_block;
typedef struct _pco_instr pco_instr;

#define PCO_REF_VAL_BITS (32U)

#define PCO_REF_IDX_NUM_BITS (2U)
#define PCO_REF_IDX_OFFSET_BITS (8U)
#define PCO_REF_IDX_PAD_BITS \
   (PCO_REF_VAL_BITS - (PCO_REF_IDX_NUM_BITS + PCO_REF_IDX_OFFSET_BITS))

/** PCO reference index. */
typedef struct PACKED _pco_ref {
   /** Reference value. */
   union PACKED {
      unsigned val : PCO_REF_VAL_BITS;

      struct PACKED {
         unsigned num : PCO_REF_IDX_NUM_BITS; /** Index register number. */
         unsigned offset : PCO_REF_IDX_OFFSET_BITS; /** Offset. */
         unsigned _pad : PCO_REF_IDX_PAD_BITS;
      } idx_reg;
   };

   /** Source/destination modifiers. */
   bool oneminus : 1;
   bool clamp : 1;
   bool flr : 1;
   bool abs : 1;
   bool neg : 1;
   enum pco_elem elem : 4; /** .e0.e1.e2.e3 */

   enum pco_dtype dtype : 2; /** Reference data-type. */
   unsigned chans : 10; /** Number of channels (1-1024). */
   enum pco_bits bits : 3; /** Bit width. */
   enum pco_ref_type type : 3; /** Reference type. */
   enum pco_reg_class reg_class : 4; /** Register class. */

   unsigned _pad : 1;
} pco_ref;
static_assert(sizeof(pco_ref) == 8, "sizeof(pco_ref) != 8");

/** PCO phi source. */
typedef struct _pco_phi_src {
   struct list_head link; /** Link in pco_instr::phi_srcs. */

   pco_block *pred; /** Predecessor block. */
   pco_ref ref; /** Source reference. */
} pco_phi_src;

/** PCO instruction group. */
typedef struct _pco_igrp {
   struct list_head link; /** Link in pco_block::instrs. */
   pco_block *parent_block; /** Basic block containing the igrp. */
   pco_func *parent_func; /** Parent function. */

   pco_instr *instrs[_PCO_OP_PHASE_COUNT]; /** Instruction/group list. */

   /** Instruction group header. */
   struct {
      unsigned da;
      unsigned length;
      union {
         enum pco_oporg oporg;
         enum pco_opcnt opcnt;
      };
      bool olchk;
      bool w1p;
      bool w0p;
      enum pco_cc cc;
      enum pco_alutype alutype;
      union {
         struct {
            bool end;
            bool atom;
            unsigned rpt;
         };
         struct {
            unsigned miscctl;
            enum pco_ctrlop ctrlop;
         };
      };
   } hdr;

   struct {
      pco_ref s[ROGUE_MAX_ALU_INPUTS];
   } srcs;

   struct {
      pco_ref is[ROGUE_MAX_ALU_INTERNAL_SOURCES];
   } iss;

   struct {
      pco_ref w[ROGUE_MAX_ALU_OUTPUTS];
   } dests;

   struct {
      enum pco_igrp_hdr_variant hdr;
      union {
         enum pco_main_variant main;
         enum pco_backend_variant backend;
         enum pco_bitwise_variant bitwise;
         enum pco_ctrl_variant ctrl;
      } instr[_PCO_OP_PHASE_COUNT];
      enum pco_src_variant lower_src;
      enum pco_src_variant upper_src;
      enum pco_iss_variant iss;
      enum pco_dst_variant dest;
   } variant;

   struct {
      struct {
         unsigned hdr;
         unsigned lower_srcs;
         unsigned upper_srcs;
         unsigned iss;
         unsigned dests;
         unsigned instrs[_PCO_OP_PHASE_COUNT];
         unsigned word_padding;
         unsigned align_padding;
         unsigned total;
      } len;

      unsigned offset;
   } enc;

   unsigned index; /** Igrp index. */
   char *comment; /** Comment string. */

} pco_igrp;

/** PCO instruction. */
typedef struct _pco_instr {
   union {
      struct {
         struct list_head link; /** Link in pco_block::instrs. */
         pco_block *parent_block; /** Basic block containing the instruction. */
      };

      pco_igrp *parent_igrp; /** Igrp containing the instruction. */
   };

   pco_func *parent_func; /** Parent function. */

   enum pco_op op;

   unsigned num_dests;
   pco_ref *dest;
   unsigned num_srcs;
   pco_ref *src;

   union {
      struct list_head phi_srcs;
      pco_cf_node *target_cf_node;
   };

   /** Instruction flags/modifiers. */
   uint32_t mod[_PCO_OP_MAX_MODS];

   unsigned index; /** Instruction index. */
   char *comment; /** Comment string. */
} pco_instr;

/** PCO control-flow node type. */
enum pco_cf_node_type {
   PCO_CF_NODE_TYPE_BLOCK,
   PCO_CF_NODE_TYPE_IF,
   PCO_CF_NODE_TYPE_LOOP,
   PCO_CF_NODE_TYPE_FUNC,
};

/** PCO control-flow node. */
typedef struct _pco_cf_node {
   struct list_head link; /** Link in lists of pco_cf_nodes. */
   enum pco_cf_node_type type; /** CF node type. */
   struct _pco_cf_node *parent; /** Parent cf node. */
   bool flag; /** Implementation-defined flag. */
} pco_cf_node;

/** PCO basic block. */
typedef struct _pco_block {
   pco_cf_node cf_node; /** Control flow node. */
   pco_func *parent_func; /** Parent function. */
   struct list_head instrs; /** Instruction/group list. */
   unsigned index; /** Block index. */
} pco_block;

/** PCO if cf construct. */
typedef struct _pco_if {
   pco_cf_node cf_node; /** CF node. */
   pco_func *parent_func; /** Parent function. */
   pco_ref cond; /** If condition. */
   struct list_head then_body; /** List of pco_cf_nodes for if body. */
   struct list_head else_body; /** List of pco_cf_nodes for else body. */
   unsigned index; /** If index. */
} pco_if;

/** PCO loop cf construct. */
typedef struct _pco_loop {
   pco_cf_node cf_node; /** CF node. */
   pco_func *parent_func; /** Parent function. */
   struct list_head body; /** List of pco_cf_nodes for loop body. */
   unsigned index; /** Loop index. */
} pco_loop;

#define VEC_USER_MULTI ((void *)(~0ULL))

/** PCO vector information. */
typedef struct _pco_vec_info {
   pco_instr *instr; /** Vector producer. */
   pco_instr **comps; /** Array of vector components. */
   pco_instr *vec_user; /** Vector user, or none, or multi. */
} pco_vec_info;

/** PCO function. */
typedef struct _pco_func {
   struct list_head link; /** Link in pco_shader::funcs. */
   pco_cf_node cf_node; /** Control flow node. */

   pco_shader *parent_shader; /** Shader containing the function. */

   enum pco_func_type type; /** Function type. */
   unsigned index; /** Function index. */
   const char *name; /** Function name. */

   struct list_head body; /** List of pco_cf_nodes for function body. */

   unsigned num_params;
   pco_ref *params;

   struct hash_table_u64 *vec_infos;

   unsigned next_ssa; /** Next SSA node index. */
   unsigned next_instr; /** Next instruction index. */
   unsigned next_igrp; /** Next igrp index. */
   unsigned next_block; /** Next block index. */
   unsigned next_if; /** Next if index. */
   unsigned next_loop; /** Next loop index. */

   unsigned temps; /** Number of temps allocated. */

   unsigned enc_offset; /** Encoding offset. */
} pco_func;

/** PCO shader. */
typedef struct _pco_shader {
   pco_ctx *ctx; /** Compiler context. */
   nir_shader *nir; /** Source NIR shader. */

   gl_shader_stage stage; /** Shader stage. */
   const char *name; /** Shader name. */
   bool is_internal; /** Whether this is an internal shader. */
   bool is_grouped; /** Whether the shader uses igrps. */

   struct list_head funcs; /** List of functions. */
   unsigned next_func; /** Next function index. */

   pco_data data; /** Shader data. */

   struct {
      struct util_dynarray buf; /** Shader binary. */

      /** Binary patch info. */
      unsigned num_patches;
      struct {
         unsigned offset;
      } * patch;
   } binary;
} pco_shader;

/** Op info. */
struct pco_op_info {
   const char *str; /** Op name string. */
   unsigned num_dests; /** Number of dests. */
   unsigned num_srcs; /** Number of sources. */
   uint64_t mods; /** Supported mods. */
   uint8_t mod_map[_PCO_OP_MOD_COUNT]; /** Index into pco_instr::mod. */
   uint64_t dest_mods[_PCO_OP_MAX_DESTS]; /** Supported dest mods. */
   uint64_t src_mods[_PCO_OP_MAX_SRCS]; /** Supported source mods. */
   enum pco_op_type type; /** Op type. */
   bool has_target_cf_node; /** Set if op has a cf-node as a target. */
};
extern const struct pco_op_info pco_op_info[_PCO_OP_COUNT];

/** Op mod info. */
struct pco_op_mod_info {
   bool print_early : 1; /** Set if printed before the op. */
   bool is_bitset : 1; /** Set if type is an enum bitset. */
   enum pco_mod_type type; /** Datatype. */
   union {
      const char *str; /** Mod name. */
      const char **strs; /** Mod names (enums). */
   };
   uint32_t nzdefault; /** Default value if non-zero. */
};
extern const struct pco_op_mod_info pco_op_mod_info[_PCO_OP_MOD_COUNT];

/** Reference mod info. */
struct pco_ref_mod_info {
   bool is_bitset : 1; /** Set if type is an enum bitset. */
   enum pco_mod_type type; /** Datatype. */
   union {
      const char *str; /** Mod name. */
      const char **strs; /** Mod names (enums). */
   };
};
extern const struct pco_ref_mod_info pco_ref_mod_info[_PCO_REF_MOD_COUNT];

pco_shader *pco_shader_create(pco_ctx *ctx, nir_shader *nir, void *mem_ctx);
pco_func *pco_func_create(pco_shader *shader,
                          enum pco_func_type type,
                          unsigned num_params);
pco_block *pco_block_create(pco_func *func);
pco_if *pco_if_create(pco_func *func);
pco_loop *pco_loop_create(pco_func *func);
pco_instr *pco_instr_create(pco_func *func,
                            enum pco_op op,
                            unsigned num_dests,
                            unsigned num_srcs);
pco_igrp *pco_igrp_create(pco_func *func);

void pco_instr_delete(pco_instr *instr);

/* Cast helpers. */

/* CF nodes. */
#define PCO_DEFINE_CAST(name, in_type, out_type, field, type_field, type_value) \
   static inline out_type *name(const in_type *parent)                          \
   {                                                                            \
      assert(parent && parent->type_field == type_value);                       \
      return list_entry(parent, out_type, field);                               \
   }

PCO_DEFINE_CAST(pco_cf_node_as_block,
                pco_cf_node,
                pco_block,
                cf_node,
                type,
                PCO_CF_NODE_TYPE_BLOCK)
PCO_DEFINE_CAST(pco_cf_node_as_if,
                pco_cf_node,
                pco_if,
                cf_node,
                type,
                PCO_CF_NODE_TYPE_IF)
PCO_DEFINE_CAST(pco_cf_node_as_loop,
                pco_cf_node,
                pco_loop,
                cf_node,
                type,
                PCO_CF_NODE_TYPE_LOOP)
PCO_DEFINE_CAST(pco_cf_node_as_func,
                pco_cf_node,
                pco_func,
                cf_node,
                type,
                PCO_CF_NODE_TYPE_FUNC)

/* Iterators. */
#define pco_foreach_func_in_shader(func, shader) \
   list_for_each_entry (pco_func, func, &(shader)->funcs, link)

#define pco_foreach_func_in_shader_rev(func, shader) \
   list_for_each_entry_rev (pco_func, func, &(shader)->funcs, link)

#define pco_foreach_cf_node_in_if_then(cf_node, _if) \
   list_for_each_entry (pco_cf_node, cf_node, &(_if)->then_body, link)

#define pco_foreach_cf_node_in_if_else(cf_node, _if) \
   list_for_each_entry (pco_cf_node, cf_node, &(_if)->else_body, link)

#define pco_foreach_cf_node_in_loop(cf_node, loop) \
   list_for_each_entry (pco_cf_node, cf_node, &(loop)->body, link)

#define pco_foreach_cf_node_in_func(cf_node, func) \
   list_for_each_entry (pco_cf_node, cf_node, &(func)->body, link)

#define pco_foreach_block_in_func(block, func)                        \
   for (pco_block *block = pco_func_first_block(func); block != NULL; \
        block = pco_next_block(block))

#define pco_foreach_block_in_func_from(block, from) \
   for (pco_block *block = from; block != NULL; block = pco_next_block(block))

#define pco_foreach_block_in_func_from_rev(block, from) \
   for (pco_block *block = from; block != NULL; block = pco_prev_block(block))

#define pco_foreach_block_in_func_rev(block, func)                   \
   for (pco_block *block = pco_func_last_block(func); block != NULL; \
        block = pco_prev_block(block))

#define pco_foreach_instr_in_block(instr, block)           \
   assert(!block->parent_func->parent_shader->is_grouped); \
   list_for_each_entry (pco_instr, instr, &(block)->instrs, link)

#define pco_foreach_instr_in_block_safe(instr, block)      \
   assert(!block->parent_func->parent_shader->is_grouped); \
   list_for_each_entry_safe (pco_instr, instr, &(block)->instrs, link)

#define pco_foreach_instr_in_block_rev(instr, block)       \
   assert(!block->parent_func->parent_shader->is_grouped); \
   list_for_each_entry_rev (pco_instr, instr, &(block)->instrs, link)

#define pco_foreach_instr_in_block_safe_rev(instr, block)  \
   assert(!block->parent_func->parent_shader->is_grouped); \
   list_for_each_entry_safe_rev (pco_instr, instr, &(block)->instrs, link)

#define pco_foreach_igrp_in_block(igrp, block)            \
   assert(block->parent_func->parent_shader->is_grouped); \
   list_for_each_entry (pco_igrp, igrp, &(block)->instrs, link)

#define pco_foreach_phi_src_in_instr(phi_src, instr) \
   list_for_each_entry (pco_phi_src, phi_src, &(instr)->phi_srcs, link)

#define pco_foreach_instr_in_func(instr, func) \
   assert(!func->parent_shader->is_grouped);   \
   pco_foreach_block_in_func (block, func)     \
      list_for_each_entry (pco_instr, instr, &(block)->instrs, link)

#define pco_foreach_instr_in_func_from(instr, from)           \
   assert(!from->parent_func->parent_shader->is_grouped);     \
   pco_foreach_block_in_func_from (block, from->parent_block) \
      list_for_each_entry_from (pco_instr, instr, from, &(block)->instrs, link)

#define pco_foreach_instr_in_func_from_rev(instr, from)           \
   assert(!from->parent_func->parent_shader->is_grouped);         \
   pco_foreach_block_in_func_from_rev (block, from->parent_block) \
      list_for_each_entry_from_rev (pco_instr,                    \
                                    instr,                        \
                                    from,                         \
                                    &(block)->instrs,             \
                                    link)

#define pco_foreach_instr_in_func_safe(instr, func) \
   assert(!func->parent_shader->is_grouped);        \
   pco_foreach_block_in_func (block, func)          \
      list_for_each_entry_safe (pco_instr, instr, &(block)->instrs, link)

#define pco_foreach_instr_in_func_rev(instr, func) \
   assert(!func->parent_shader->is_grouped);       \
   pco_foreach_block_in_func_rev (block, func)     \
      list_for_each_entry_rev (pco_instr, instr, &(block)->instrs, link)

#define pco_foreach_instr_in_func_safe_rev(instr, func) \
   assert(!func->parent_shader->is_grouped);            \
   pco_foreach_block_in_func_rev (block, func)          \
      list_for_each_entry_safe_rev (pco_instr, instr, &(block)->instrs, link)

#define pco_foreach_instr_dest(pdest, instr)    \
   for (pco_ref *pdest = &instr->dest[0];       \
        pdest < &instr->dest[instr->num_dests]; \
        ++pdest)

#define pco_foreach_instr_src(psrc, instr)                                   \
   for (pco_ref *psrc = &instr->src[0]; psrc < &instr->src[instr->num_srcs]; \
        ++psrc)

#define pco_foreach_instr_dest_ssa(pdest, instr) \
   pco_foreach_instr_dest (pdest, instr)         \
      if (pco_ref_is_ssa(*pdest))

#define pco_foreach_instr_src_ssa(psrc, instr) \
   pco_foreach_instr_src (psrc, instr)         \
      if (pco_ref_is_ssa(*psrc))

#define pco_first_cf_node(body) list_first_entry(body, pco_cf_node, link)
#define pco_last_cf_node(body) list_last_entry(body, pco_cf_node, link)
#define pco_next_cf_node(cf_node) \
   list_entry((cf_node)->link.next, pco_cf_node, link)
#define pco_prev_cf_node(cf_node) \
   list_entry((cf_node)->link.prev, pco_cf_node, link)

/**
 * \brief Returns whether the current cf node is (directly) in an else body.
 *
 * \param[in] cf_node The cf node.
 * \return True if in the else body, else false if in the then body.
 */
static inline bool pco_cf_node_in_if_else(pco_cf_node *cf_node)
{
   assert(cf_node->parent->type == PCO_CF_NODE_TYPE_IF);
   return cf_node->flag;
}

/**
 * \brief Returns the preamble function of a PCO shader.
 *
 * \param[in] shader The PCO shader.
 * \return The preamble function, or NULL if the shader has no preamble.
 */
static inline pco_func *pco_preamble(pco_shader *shader)
{
   if (list_is_empty(&shader->funcs))
      return NULL;

   pco_func *func = list_first_entry(&shader->funcs, pco_func, link);
   if (func->type == PCO_FUNC_TYPE_PREAMBLE)
      return func;

   return NULL;
}

/**
 * \brief Returns the entrypoint function of a PCO shader.
 *
 * \param[in] shader The PCO shader.
 * \return The entrypoint function, or NULL if the shader has no entrypoint.
 */
static inline pco_func *pco_entrypoint(pco_shader *shader)
{
   if (list_is_empty(&shader->funcs))
      return NULL;

   /* Entrypoint will either be the first or second function in the shader,
    * depending on whether or not there is a preamble.
    */
   pco_func *preamble = pco_preamble(shader);
   pco_func *func = !preamble ? list_first_entry(&shader->funcs, pco_func, link)
                              : list_entry(preamble->link.next, pco_func, link);

   if (func->type == PCO_FUNC_TYPE_ENTRYPOINT)
      return func;

   return NULL;
}

/**
 * \brief Returns the variant of an instruction in an instruction group.
 *
 * \param[in] igrp The instruction group.
 * \param[in] phase The instruction phase.
 * \return The instruction variant.
 */
static inline unsigned pco_igrp_variant(const pco_igrp *igrp,
                                        enum pco_op_phase phase)
{
   switch (igrp->hdr.alutype) {
   case PCO_ALUTYPE_MAIN:
      return phase == PCO_OP_PHASE_BACKEND ? igrp->variant.instr[phase].backend
                                           : igrp->variant.instr[phase].main;

   case PCO_ALUTYPE_BITWISE:
      return igrp->variant.instr[phase].bitwise;

   case PCO_ALUTYPE_CONTROL:
      return igrp->variant.instr[phase].ctrl;

   default:
      break;
   }

   unreachable();
}

/* Motions. */
/**
 * \brief Returns the first block in an if then body.
 *
 * \param[in] pif The if.
 * \return The first block.
 */
static inline pco_block *pco_if_then_first_block(pco_if *pif)
{
   assert(!list_is_empty(&pif->then_body));

   pco_cf_node *cf_node = pco_first_cf_node(&pif->then_body);
   return pco_cf_node_as_block(cf_node);
}

/**
 * \brief Returns the last block in an if then body.
 *
 * \param[in] pif The if.
 * \return The last block.
 */
static inline pco_block *pco_if_then_last_block(pco_if *pif)
{
   assert(!list_is_empty(&pif->then_body));

   pco_cf_node *cf_node = pco_last_cf_node(&pif->then_body);
   return pco_cf_node_as_block(cf_node);
}

/**
 * \brief Returns the first block in an else body.
 *
 * \param[in] pif The if.
 * \return The first block.
 */
static inline pco_block *pco_if_else_first_block(pco_if *pif)
{
   assert(!list_is_empty(&pif->else_body));

   pco_cf_node *cf_node = pco_first_cf_node(&pif->else_body);
   return pco_cf_node_as_block(cf_node);
}

/**
 * \brief Returns the last block in an else body.
 *
 * \param[in] pif The if.
 * \return The last block.
 */
static inline pco_block *pco_if_else_last_block(pco_if *pif)
{
   assert(!list_is_empty(&pif->else_body));

   pco_cf_node *cf_node = pco_last_cf_node(&pif->else_body);
   return pco_cf_node_as_block(cf_node);
}

/**
 * \brief Returns the first block in a loop.
 *
 * \param[in] loop The loop.
 * \return The first block.
 */
static inline pco_block *pco_loop_first_block(pco_loop *loop)
{
   assert(!list_is_empty(&loop->body));

   pco_cf_node *cf_node = pco_first_cf_node(&loop->body);
   return pco_cf_node_as_block(cf_node);
}

/**
 * \brief Returns the last block in a loop.
 *
 * \param[in] loop The loop.
 * \return The last block.
 */
static inline pco_block *pco_loop_last_block(pco_loop *loop)
{
   assert(!list_is_empty(&loop->body));

   pco_cf_node *cf_node = pco_last_cf_node(&loop->body);
   return pco_cf_node_as_block(cf_node);
}

/**
 * \brief Returns the first block in a function.
 *
 * \param[in] func The function.
 * \return The first block.
 */
static inline pco_block *pco_func_first_block(pco_func *func)
{
   assert(!list_is_empty(&func->body));

   pco_cf_node *cf_node = pco_first_cf_node(&func->body);
   return pco_cf_node_as_block(cf_node);
}

/**
 * \brief Returns the last block in a function.
 *
 * \param[in] func The function.
 * \return The last block.
 */
static inline pco_block *pco_func_last_block(pco_func *func)
{
   assert(!list_is_empty(&func->body));

   pco_cf_node *cf_node = pco_last_cf_node(&func->body);
   return pco_cf_node_as_block(cf_node);
}

/**
 * \brief Returns the first block in a control-flow node.
 *
 * \param[in] cf_node The control-flow node.
 * \return The first block in the control-flow node.
 */
static inline pco_block *pco_cf_node_first_block(pco_cf_node *cf_node)
{
   switch (cf_node->type) {
   case PCO_CF_NODE_TYPE_BLOCK:
      return pco_cf_node_as_block(cf_node);

   case PCO_CF_NODE_TYPE_IF:
      return pco_if_then_first_block(pco_cf_node_as_if(cf_node));

   case PCO_CF_NODE_TYPE_LOOP:
      return pco_loop_first_block(pco_cf_node_as_loop(cf_node));

   case PCO_CF_NODE_TYPE_FUNC:
      return pco_func_first_block(pco_cf_node_as_func(cf_node));

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the last block in a control-flow node.
 *
 * \param[in] cf_node The control-flow node.
 * \return The last block in the control-flow node.
 */
static inline pco_block *pco_cf_node_last_block(pco_cf_node *cf_node)
{
   switch (cf_node->type) {
   case PCO_CF_NODE_TYPE_BLOCK:
      return pco_cf_node_as_block(cf_node);

   case PCO_CF_NODE_TYPE_IF:
      return pco_if_else_last_block(pco_cf_node_as_if(cf_node));

   case PCO_CF_NODE_TYPE_LOOP:
      return pco_loop_last_block(pco_cf_node_as_loop(cf_node));

   case PCO_CF_NODE_TYPE_FUNC:
      return pco_func_last_block(pco_cf_node_as_func(cf_node));

   default:
      break;
   }

   unreachable();
}

static inline struct list_head *pco_parent_cf_node_body(pco_cf_node *cf_node)
{
   pco_cf_node *parent_cf_node = cf_node->parent;

   switch (parent_cf_node->type) {
   case PCO_CF_NODE_TYPE_IF:
      return pco_cf_node_in_if_else(cf_node)
                ? &pco_cf_node_as_if(parent_cf_node)->else_body
                : &pco_cf_node_as_if(parent_cf_node)->then_body;

   case PCO_CF_NODE_TYPE_LOOP:
      return &pco_cf_node_as_loop(parent_cf_node)->body;

   case PCO_CF_NODE_TYPE_FUNC:
      return &pco_cf_node_as_func(parent_cf_node)->body;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the next block in the function.
 *
 * \param[in] block The current block.
 * \return The next block, or NULL if we've reached the end of the function.
 */
static inline pco_block *pco_next_block(pco_block *block)
{
   if (!block)
      return NULL;

   pco_cf_node *cf_node = &block->cf_node;
   pco_cf_node *last_cf_node =
      pco_last_cf_node(pco_parent_cf_node_body(cf_node));

   /* Not yet reached the end of the body, return the next cf node. */
   if (cf_node != last_cf_node)
      return pco_cf_node_first_block(pco_next_cf_node(cf_node));

   /* Reached the end; go to the next block from the parent cf node. */
   pco_cf_node *parent_cf_node = cf_node->parent;
   switch (parent_cf_node->type) {
   case PCO_CF_NODE_TYPE_IF:
      /* If we're in the then body, go to the else body. */
      if (!pco_cf_node_in_if_else(cf_node))
         return pco_if_else_first_block(pco_cf_node_as_if(parent_cf_node));

      /* Otherwise go to the next block from the parent's parent cf node. */
      FALLTHROUGH;

   case PCO_CF_NODE_TYPE_LOOP:
      return pco_cf_node_first_block(pco_next_cf_node(parent_cf_node));

   /* End of the function; return NULL. */
   case PCO_CF_NODE_TYPE_FUNC:
      return NULL;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the previous block in the function.
 *
 * \param[in] block The current block.
 * \return The previous block, or NULL if we've reached the start of the
 * function.
 */
static inline pco_block *pco_prev_block(pco_block *block)
{
   if (!block)
      return NULL;

   pco_cf_node *cf_node = &block->cf_node;
   pco_cf_node *first_cf_node =
      pco_first_cf_node(pco_parent_cf_node_body(cf_node));

   /* Not yet reached the start of the body, return the previous cf node. */
   if (cf_node != first_cf_node)
      return pco_cf_node_last_block(pco_prev_cf_node(cf_node));

   /* Reached the start; go to the previous block from the parent cf node. */
   pco_cf_node *parent_cf_node = cf_node->parent;
   switch (parent_cf_node->type) {
   case PCO_CF_NODE_TYPE_IF:
      /* If we're in the else body, go to the then body. */
      if (pco_cf_node_in_if_else(cf_node))
         return pco_if_then_last_block(pco_cf_node_as_if(parent_cf_node));

      /* Otherwise go to the previous block from the parent's parent cf node. */
      FALLTHROUGH;

   case PCO_CF_NODE_TYPE_LOOP:
      return pco_cf_node_last_block(pco_prev_cf_node(parent_cf_node));

   /* Start of the function; return NULL. */
   case PCO_CF_NODE_TYPE_FUNC:
      return NULL;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the first instruction in a block.
 *
 * \param[in] block The block.
 * \return The first instruction, or NULL if the block is empty.
 */
static inline pco_instr *pco_first_instr(pco_block *block)
{
   assert(!block->parent_func->parent_shader->is_grouped);
   if (list_is_empty(&block->instrs))
      return NULL;

   return list_first_entry(&block->instrs, pco_instr, link);
}

/**
 * \brief Returns the last instruction in a block.
 *
 * \param[in] block The block.
 * \return The last instruction, or NULL if the block is empty.
 */
static inline pco_instr *pco_last_instr(pco_block *block)
{
   assert(!block->parent_func->parent_shader->is_grouped);
   if (list_is_empty(&block->instrs))
      return NULL;

   return list_last_entry(&block->instrs, pco_instr, link);
}

/**
 * \brief Returns the next instruction.
 *
 * \param[in] instr The current instruction.
 * \return The next instruction, or NULL if the end of the block has been
 *         reached.
 */
static inline pco_instr *pco_next_instr(pco_instr *instr)
{
   assert(!instr->parent_func->parent_shader->is_grouped);
   if (!instr || instr == pco_last_instr(instr->parent_block))
      return NULL;

   return list_entry(instr->link.next, pco_instr, link);
}

/**
 * \brief Returns the previous instruction.
 *
 * \param[in] instr The current instruction.
 * \return The previous instruction, or NULL if the start of the block has been
 *         reached.
 */
static inline pco_instr *pco_prev_instr(pco_instr *instr)
{
   assert(!instr->parent_func->parent_shader->is_grouped);
   if (!instr || instr == pco_first_instr(instr->parent_block))
      return NULL;

   return list_entry(instr->link.prev, pco_instr, link);
}

/**
 * \brief Returns the first instruction group in a block.
 *
 * \param[in] block The block.
 * \return The first instruction group, or NULL if the block is empty.
 */
static inline pco_igrp *pco_first_igrp(pco_block *block)
{
   assert(block->parent_func->parent_shader->is_grouped);
   if (list_is_empty(&block->instrs))
      return NULL;

   return list_first_entry(&block->instrs, pco_igrp, link);
}

/**
 * \brief Returns the last instruction group in a block.
 *
 * \param[in] block The block.
 * \return The last instruction group, or NULL if the block is empty.
 */
static inline pco_igrp *pco_last_igrp(pco_block *block)
{
   assert(block->parent_func->parent_shader->is_grouped);
   if (list_is_empty(&block->instrs))
      return NULL;

   return list_last_entry(&block->instrs, pco_igrp, link);
}

/**
 * \brief Returns the next instruction group.
 *
 * \param[in] igrp The current instruction group.
 * \return The next instruction group, or NULL if the end of the block has been
 *         reached.
 */
static inline pco_igrp *pco_next_igrp(pco_igrp *igrp)
{
   assert(igrp->parent_func->parent_shader->is_grouped);
   if (!igrp || igrp == pco_last_igrp(igrp->parent_block))
      return NULL;

   return list_entry(igrp->link.next, pco_igrp, link);
}

/**
 * \brief Returns the previous instruction group.
 *
 * \param[in] igrp The current instruction group.
 * \return The previous instruction group, or NULL if the start of the block has
 *         been reached.
 */
static inline pco_igrp *pco_prev_igrp(pco_igrp *igrp)
{
   assert(igrp->parent_func->parent_shader->is_grouped);
   if (!igrp || igrp == pco_first_igrp(igrp->parent_block))
      return NULL;

   return list_entry(igrp->link.prev, pco_igrp, link);
}

/* Debug printing helpers. */
static inline bool pco_should_print_nir(nir_shader *nir)
{
   if (!PCO_DEBUG_PRINT(NIR))
      return false;

   if (nir->info.internal && !PCO_DEBUG_PRINT(INTERNAL))
      return false;

   if (nir->info.stage == MESA_SHADER_VERTEX && !PCO_DEBUG_PRINT(VS))
      return false;
   else if (nir->info.stage == MESA_SHADER_FRAGMENT && !PCO_DEBUG_PRINT(FS))
      return false;
   else if (nir->info.stage == MESA_SHADER_COMPUTE && !PCO_DEBUG_PRINT(CS))
      return false;

   return true;
}

static inline bool pco_should_print_shader(pco_shader *shader)
{
   if (shader->is_internal && !PCO_DEBUG_PRINT(INTERNAL))
      return false;

   if (shader->stage == MESA_SHADER_VERTEX && !PCO_DEBUG_PRINT(VS))
      return false;
   else if (shader->stage == MESA_SHADER_FRAGMENT && !PCO_DEBUG_PRINT(FS))
      return false;
   else if (shader->stage == MESA_SHADER_COMPUTE && !PCO_DEBUG_PRINT(CS))
      return false;

   return true;
}

static inline bool pco_should_print_shader_pass(pco_shader *shader)
{
   if (!PCO_DEBUG_PRINT(PASSES))
      return false;

   if (shader->is_internal && !PCO_DEBUG_PRINT(INTERNAL))
      return false;

   if (shader->stage == MESA_SHADER_VERTEX && !PCO_DEBUG_PRINT(VS))
      return false;
   else if (shader->stage == MESA_SHADER_FRAGMENT && !PCO_DEBUG_PRINT(FS))
      return false;
   else if (shader->stage == MESA_SHADER_COMPUTE && !PCO_DEBUG_PRINT(CS))
      return false;

   return true;
}

static inline bool pco_should_print_binary(pco_shader *shader)
{
   if (!PCO_DEBUG_PRINT(BINARY))
      return false;

   if (shader->is_internal && !PCO_DEBUG_PRINT(INTERNAL))
      return false;

   if (shader->stage == MESA_SHADER_VERTEX && !PCO_DEBUG_PRINT(VS))
      return false;
   else if (shader->stage == MESA_SHADER_FRAGMENT && !PCO_DEBUG_PRINT(FS))
      return false;
   else if (shader->stage == MESA_SHADER_COMPUTE && !PCO_DEBUG_PRINT(CS))
      return false;

   return true;
}

/* PCO IR passes. */
bool pco_const_imms(pco_shader *shader);
bool pco_dce(pco_shader *shader);
bool pco_end(pco_shader *shader);
bool pco_group_instrs(pco_shader *shader);
bool pco_index(pco_shader *shader, bool skip_ssa);
bool pco_nir_pfo(nir_shader *nir, pco_fs_data *fs);
bool pco_nir_pvi(nir_shader *nir, pco_vs_data *vs);
bool pco_opt(pco_shader *shader);
bool pco_ra(pco_shader *shader);
bool pco_schedule(pco_shader *shader);

/**
 * \brief Returns the PCO bits for a bit size.
 *
 * \param[in] bits Reference.
 * \return PCO bits.
 */
static inline enum pco_bits pco_bits(unsigned bits)
{
   switch (bits) {
   case 1:
      return PCO_BITS_1;

   case 8:
      return PCO_BITS_8;

   case 16:
      return PCO_BITS_16;

   case 32:
      return PCO_BITS_32;

   case 64:
      return PCO_BITS_64;

   default:
      break;
   }

   unreachable();
}

/* PCO ref checkers. */
/**
 * \brief Returns whether a reference is null.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is null.
 */
static inline bool pco_ref_is_null(pco_ref ref)
{
   return ref.type == PCO_REF_TYPE_NULL;
}

/**
 * \brief Returns whether a reference is an SSA variable.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is an SSA variable.
 */
static inline bool pco_ref_is_ssa(pco_ref ref)
{
   return ref.type == PCO_REF_TYPE_SSA;
}

/**
 * \brief Returns whether a reference is a register.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is a register.
 */
static inline bool pco_ref_is_reg(pco_ref ref)
{
   return ref.type == PCO_REF_TYPE_REG;
}

/**
 * \brief Returns whether a reference is an index register.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is an index register.
 */
static inline bool pco_ref_is_idx_reg(pco_ref ref)
{
   return ref.type == PCO_REF_TYPE_IDX_REG;
}

/**
 * \brief Returns whether a reference is an immediate.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is an immediate.
 */
static inline bool pco_ref_is_imm(pco_ref ref)
{
   return ref.type == PCO_REF_TYPE_IMM;
}

/**
 * \brief Returns whether a reference is I/O.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is I/O.
 */
static inline bool pco_ref_is_io(pco_ref ref)
{
   return ref.type == PCO_REF_TYPE_IO;
}

/**
 * \brief Returns whether a reference is a predicate.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is a predicate.
 */
static inline bool pco_ref_is_pred(pco_ref ref)
{
   return ref.type == PCO_REF_TYPE_PRED;
}

/**
 * \brief Returns whether a reference is a dependant read counter.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is a dependant read counter.
 */
static inline bool pco_ref_is_drc(pco_ref ref)
{
   return ref.type == PCO_REF_TYPE_DRC;
}

/**
 * \brief Returns whether a reference is scalar.
 *
 * \param[in] ref PCO reference.
 * \return True if the reference is scalar.
 */
static inline bool pco_ref_is_scalar(pco_ref ref)
{
   return !ref.chans;
}

/* PCO ref getters. */
/**
 * \brief Returns the pointee component of an indexed register reference.
 *
 * \param[in] ref Indexed register reference.
 * \return Pointee component of the indexed register reference.
 */
static inline pco_ref pco_ref_get_idx_pointee(pco_ref ref)
{
   assert(pco_ref_is_idx_reg(ref));

   pco_ref pointee = ref;
   pointee.val = ref.idx_reg.offset;
   pointee.type = PCO_REF_TYPE_REG;

   return pointee;
}

/**
 * \brief Returns the data type of a reference.
 *
 * \param[in] ref Reference.
 * \return Datatype.
 */
static inline enum pco_dtype pco_ref_get_dtype(pco_ref ref)
{
   return ref.dtype;
}

/**
 * \brief Returns the number of channels for a reference type.
 *
 * \param[in] ref Reference.
 * \return Number of channels.
 */
static inline unsigned pco_ref_get_chans(pco_ref ref)
{
   return ref.chans + 1;
}

/**
 * \brief Returns the number of bits for a reference type.
 *
 * \param[in] ref Reference.
 * \return Number of bits.
 */
static inline unsigned pco_ref_get_bits(pco_ref ref)
{
   switch (ref.bits) {
   case PCO_BITS_1:
      return 1;

   case PCO_BITS_8:
      return 8;

   case PCO_BITS_16:
      return 16;

   case PCO_BITS_32:
      return 32;

   case PCO_BITS_64:
      return 64;

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the bit-sized value in an immediate reference.
 *
 * \param[in] ref Reference.
 * \return Immediate value.
 */
static inline uint64_t pco_ref_get_imm(pco_ref ref)
{
   assert(pco_ref_is_imm(ref));

   unsigned num_bits = pco_ref_get_bits(ref);

   switch (ref.dtype) {
   case PCO_DTYPE_FLOAT:
      assert(num_bits == 32);
      FALLTHROUGH;
   case PCO_DTYPE_ANY:
      FALLTHROUGH;
   case PCO_DTYPE_UNSIGNED:
      return ref.val & BITFIELD_MASK(num_bits);

   case PCO_DTYPE_SIGNED:
      return util_sign_extend(ref.val, num_bits);

   default:
      break;
   }

   unreachable();
}

/**
 * \brief Returns the register class of a reference type.
 *
 * \param[in] ref Reference.
 * \return Register class.
 */
static inline enum pco_reg_class pco_ref_get_reg_class(pco_ref ref)
{
   assert(pco_ref_is_reg(ref) || pco_ref_is_idx_reg(ref));
   return ref.reg_class;
}

/**
 * \brief Returns the register index of a reference type.
 *
 * \param[in] ref Reference.
 * \return Register index.
 */
static inline unsigned pco_ref_get_reg_index(pco_ref ref)
{
   assert(pco_ref_is_reg(ref) || pco_ref_is_idx_reg(ref));

   unsigned index = pco_ref_is_idx_reg(ref) ? ref.idx_reg.offset : ref.val;
   assert(index < 256);

   return index;
}

/**
 * \brief Returns the register index control of a reference type.
 *
 * \param[in] ref Reference.
 * \return Register index control.
 */
static inline enum pco_idx_ctrl pco_ref_get_reg_idx_ctrl(pco_ref ref)
{
   assert(pco_ref_is_reg(ref) || pco_ref_is_idx_reg(ref));

   if (pco_ref_is_reg(ref))
      return PCO_IDX_CTRL_NONE;

   return PCO_IDX_CTRL_IDX0 + ref.idx_reg.num;
}

/**
 * \brief Returns the temp register index from its reference type.
 *
 * \param[in] ref Reference.
 * \return Temp index.
 */
static inline unsigned pco_ref_get_temp(pco_ref ref)
{
   assert(pco_ref_is_reg(ref));
   assert(pco_ref_get_reg_class(ref) == PCO_REG_CLASS_TEMP);

   return pco_ref_get_reg_index(ref);
}

/**
 * \brief Returns the coefficient register index from its reference type.
 *
 * \param[in] ref Reference.
 * \return Coefficient index.
 */
static inline unsigned pco_ref_get_coeff(pco_ref ref)
{
   assert(pco_ref_is_reg(ref));
   assert(pco_ref_get_reg_class(ref) == PCO_REG_CLASS_COEFF);

   return pco_ref_get_reg_index(ref);
}

/**
 * \brief Returns the I/O from its reference type.
 *
 * \param[in] ref Reference.
 * \return I/O.
 */
static inline enum pco_io pco_ref_get_io(pco_ref ref)
{
   assert(pco_ref_is_io(ref));
   assert(ref.val < _PCO_IO_COUNT);
   return ref.val;
}

/**
 * \brief Returns the predicate from its reference type.
 *
 * \param[in] ref Reference.
 * \return Predicate.
 */
static inline enum pco_pred pco_ref_get_pred(pco_ref ref)
{
   assert(pco_ref_is_pred(ref));
   assert(ref.val < _PCO_PRED_COUNT);
   return ref.val;
}

/**
 * \brief Returns the dependent read counter from its reference type.
 *
 * \param[in] ref Reference.
 * \return Dependent read counter.
 */
static inline enum pco_drc pco_ref_get_drc(pco_ref ref)
{
   assert(pco_ref_is_drc(ref));
   assert(ref.val < _PCO_DRC_COUNT);
   return ref.val;
}

/**
 * \brief Returns whether the reference has any mods set.
 *
 * \param[in] ref Reference.
 * \return True if any mods are set.
 */
static inline bool pco_ref_has_mods_set(pco_ref ref)
{
   return ref.oneminus || ref.clamp || ref.abs || ref.neg || ref.flr ||
          (ref.elem != 0);
}

/* PCO ref builders. */
/**
 * \brief Builds and returns a null reference.
 *
 * \return NULL reference.
 */
static inline pco_ref pco_ref_null(void)
{
   return (pco_ref){
      .type = PCO_REF_TYPE_NULL,
   };
}

/**
 * \brief Builds and returns an SSA reference.
 *
 * \return SSA reference.
 */
static inline pco_ref pco_ref_ssa(unsigned index, unsigned bits, unsigned chans)
{
   return (pco_ref){
      .val = index,
      .chans = chans - 1,
      .bits = pco_bits(bits),
      .type = PCO_REF_TYPE_SSA,
   };
}

/**
 * \brief Builds and returns a new SSA reference.
 *
 * \param[in,out] func The function.
 * \param[in] bits Number of bits.
 * \param[in] chans Number of channels.
 * \return SSA reference.
 */
static inline pco_ref
pco_ref_new_ssa(pco_func *func, unsigned bits, unsigned chans)
{
   return (pco_ref){
      .val = func->next_ssa++,
      .chans = chans - 1,
      .bits = pco_bits(bits),
      .type = PCO_REF_TYPE_SSA,
   };
}

/**
 * \brief Builds and returns a new 32x1 SSA reference.
 *
 * \param[in,out] func The function.
 * \return SSA reference.
 */
static inline pco_ref pco_ref_new_ssa32(pco_func *func)
{
   return pco_ref_new_ssa(func, 32, 1);
}

/**
 * \brief Builds and returns a virtual register reference.
 *
 * \param[in] index Virtual register index.
 * \return Virtual register reference.
 */
static inline pco_ref pco_ref_vreg(unsigned index)
{
   return (pco_ref){
      .val = index,
      .bits = PCO_BITS_32,
      .type = PCO_REF_TYPE_REG,
      .reg_class = PCO_REG_CLASS_VIRT,
   };
}

/**
 * \brief Builds and returns a scalar hardware register reference.
 *
 * \param[in] index Register index.
 * \param[in] reg_class Register class.
 * \return Hardware register reference.
 */
static inline pco_ref pco_ref_hwreg(unsigned index,
                                    enum pco_reg_class reg_class)
{
   assert(index < 256);
   assert(reg_class != PCO_REG_CLASS_VIRT);

   return (pco_ref){
      .val = index,
      .bits = PCO_BITS_32,
      .type = PCO_REF_TYPE_REG,
      .reg_class = reg_class,
   };
}

/**
 * \brief Builds and returns a vector hardware register reference.
 *
 * \param[in] index Register index.
 * \param[in] reg_class Register class.
 * \param[in] chans Number of channels.
 * \return Hardware register reference.
 */
static inline pco_ref
pco_ref_hwreg_vec(unsigned index, enum pco_reg_class reg_class, unsigned chans)
{
   assert(index < 256);
   assert(reg_class != PCO_REG_CLASS_VIRT);

   return (pco_ref){
      .val = index,
      .chans = chans - 1,
      .bits = PCO_BITS_32,
      .type = PCO_REF_TYPE_REG,
      .reg_class = reg_class,
   };
}

/**
 * \brief Builds and returns an immediate reference.
 *
 * \param[in] val Immediate value.
 * \param[in] bits Immediate bit size.
 * \param[in] dtype Immediate datatype.
 * \return Immediate reference.
 */
static inline pco_ref
pco_ref_imm(uint32_t val, enum pco_bits bits, enum pco_dtype dtype)
{
   return (pco_ref){
      .val = val,
      .dtype = dtype,
      .bits = bits,
      .type = PCO_REF_TYPE_IMM,
   };
}

/**
 * \brief Builds and returns an 8-bit immediate reference.
 *
 * \param[in] val 8-bit immediate.
 * \return Immediate reference.
 */
static inline pco_ref pco_ref_imm8(uint8_t val)
{
   return pco_ref_imm(val, PCO_BITS_8, PCO_DTYPE_UNSIGNED);
}

/**
 * \brief Builds and returns a 16-bit immediate reference.
 *
 * \param[in] val 16-bit immediate.
 * \return Immediate reference.
 */
static inline pco_ref pco_ref_imm16(uint16_t val)
{
   return pco_ref_imm(val, PCO_BITS_16, PCO_DTYPE_UNSIGNED);
}

/**
 * \brief Builds and returns a 32-bit immediate reference.
 *
 * \param[in] val 32-bit immediate.
 * \return Immediate reference.
 */
static inline pco_ref pco_ref_imm32(uint32_t val)
{
   return pco_ref_imm(val, PCO_BITS_32, PCO_DTYPE_UNSIGNED);
}

/**
 * \brief Builds and returns an untyped 8-bit immediate reference.
 *
 * \param[in] val 8-bit immediate.
 * \return Immediate reference.
 */
static inline pco_ref pco_ref_val8(uint8_t val)
{
   return pco_ref_imm(val, PCO_BITS_8, PCO_DTYPE_ANY);
}

/**
 * \brief Builds and returns an untyped 16-bit immediate reference.
 *
 * \param[in] val 16-bit immediate.
 * \return Immediate reference.
 */
static inline pco_ref pco_ref_val16(uint16_t val)
{
   return pco_ref_imm(val, PCO_BITS_16, PCO_DTYPE_ANY);
}

/**
 * \brief Builds and returns an untyped 32-bit immediate reference.
 *
 * \param[in] val 32-bit immediate.
 * \return Immediate reference.
 */
static inline pco_ref pco_ref_val32(uint32_t val)
{
   return pco_ref_imm(val, PCO_BITS_32, PCO_DTYPE_UNSIGNED);
}

/**
 * \brief Builds and returns an I/O reference.
 *
 * \param[in] io I/O.
 * \return I/O reference.
 */
static inline pco_ref pco_ref_io(enum pco_io io)
{
   return (pco_ref){
      .val = io,
      .type = PCO_REF_TYPE_IO,
   };
}

/**
 * \brief Builds and returns a predicate reference.
 *
 * \param[in] pred predicate.
 * \return Predicate reference.
 */
static inline pco_ref pco_ref_pred(enum pco_pred pred)
{
   return (pco_ref){
      .val = pred,
      .type = PCO_REF_TYPE_PRED,
   };
}

/**
 * \brief Builds and returns a dependent read counter reference.
 *
 * \param[in] drc Dependent read counter.
 * \return Dependent read counter reference.
 */
static inline pco_ref pco_ref_drc(enum pco_drc drc)
{
   return (pco_ref){
      .val = drc,
      .type = PCO_REF_TYPE_DRC,
   };
}

/* PCO ref utils. */
/**
 * \brief Transfers reference mods, optionally resetting them.
 *
 * \param[in,out] dest Reference to transfer mods to.
 * \param[in,out] source Reference to transfer mods from.
 * \param[in] reset Whether to reset the source mods.
 * \return I/O reference.
 */
static inline void pco_ref_xfer_mods(pco_ref *dest, pco_ref *source, bool reset)
{
   dest->oneminus = source->oneminus;
   dest->clamp = source->clamp;
   dest->flr = source->flr;
   dest->abs = source->abs;
   dest->neg = source->neg;
   dest->elem = source->elem;

   if (reset) {
      source->oneminus = false;
      source->clamp = false;
      source->flr = false;
      source->abs = false;
      source->neg = false;
      source->elem = 0;
   }
}

/**
 * \brief Updates a reference to set the oneminus modifier.
 *
 * \param[in] ref Base reference.
 * \return Updated reference.
 */
static inline pco_ref pco_ref_oneminus(pco_ref ref)
{
   ref.oneminus = true;
   return ref;
}

/**
 * \brief Updates a reference to set the clamp modifier.
 *
 * \param[in] ref Base reference.
 * \return Updated reference.
 */
static inline pco_ref pco_ref_clamp(pco_ref ref)
{
   ref.clamp = true;
   return ref;
}

/**
 * \brief Updates a reference to set the floor modifier.
 *
 * \param[in] ref Base reference.
 * \return Updated reference.
 */
static inline pco_ref pco_ref_flr(pco_ref ref)
{
   ref.flr = true;
   ref.abs = false;
   ref.neg = false;
   return ref;
}

/**
 * \brief Updates a reference to set the abs modifier.
 *
 * \param[in] ref Base reference.
 * \return Updated reference.
 */
static inline pco_ref pco_ref_abs(pco_ref ref)
{
   ref.abs = true;
   ref.neg = false;
   return ref;
}

/**
 * \brief Updates a reference to set the negate modifier.
 *
 * \param[in] ref Base reference.
 * \return Updated reference.
 */
static inline pco_ref pco_ref_neg(pco_ref ref)
{
   ref.neg = !ref.neg;
   return ref;
}

/**
 * \brief Updates a reference to set the element modifier.
 *
 * \param[in] ref Base reference.
 * \param[in] elem New element modifier.
 * \return Updated reference.
 */
static inline pco_ref pco_ref_elem(pco_ref ref, enum pco_elem elem)
{
   ref.elem = elem;
   return ref;
}

/**
 * \brief Updates a reference to set the number of channels.
 *
 * \param[in] ref Base reference.
 * \param[in] chans New number of channels.
 * \return Updated reference.
 */
static inline pco_ref pco_ref_chans(pco_ref ref, unsigned chans)
{
   ref.chans = chans - 1;
   return ref;
}

/**
 * \brief Updates a reference value with the provided offset.
 *
 * \param[in] ref Base reference.
 * \param[in] offset Offset to apply.
 * \return Updated reference.
 */
static inline pco_ref pco_ref_offset(pco_ref ref, signed offset)
{
   int64_t val = pco_ref_is_idx_reg(ref) ? ref.idx_reg.offset : ref.val;
   val += offset;

   if (pco_ref_is_idx_reg(ref)) {
      assert(util_last_bit64(val) <= PCO_REF_IDX_OFFSET_BITS);
      ref.idx_reg.offset = val;
   } else {
      assert(util_last_bit64(val) <= PCO_REF_VAL_BITS);
      ref.val = val;
   }

   return ref;
}

/**
 * \brief Checks whether two reference modifiers are the same.
 *
 * \param[in] ref0 First reference.
 * \param[in] ref1 Second reference.
 * \return True if both reference modifiers are the same.
 */
static inline bool pco_ref_mods_are_equal(pco_ref ref0, pco_ref ref1)
{
   return (ref0.oneminus == ref1.oneminus) && (ref0.clamp == ref1.clamp) &&
          (ref0.flr == ref1.flr) && (ref0.abs == ref1.abs) &&
          (ref0.neg == ref1.neg) && (ref0.elem == ref1.elem);
}

/**
 * \brief Checks whether two references are the same.
 *
 * \param[in] ref0 First reference.
 * \param[in] ref1 Second reference.
 * \return True if both references are the same.
 */
/* TODO: can this be simplified? */
static inline bool pco_refs_are_equal(pco_ref ref0, pco_ref ref1)
{
   if (ref0.type != ref1.type)
      return false;

   if (pco_ref_is_idx_reg(ref0)) {
      if ((ref0.idx_reg.num != ref1.idx_reg.num) ||
          (ref0.idx_reg.offset != ref1.idx_reg.offset)) {
         return false;
      }
   } else if (ref0.val != ref1.val) {
      return false;
   }

   if (pco_ref_is_idx_reg(ref0) || pco_ref_is_reg(ref0))
      if (ref0.reg_class != ref1.reg_class)
         return false;

   if (!pco_ref_mods_are_equal(ref0, ref1))
      return false;

   if (ref0.chans != ref1.chans)
      return false;

   if (pco_ref_get_dtype(ref0) != pco_ref_get_dtype(ref1))
      return false;

   if (pco_ref_get_bits(ref0) != pco_ref_get_bits(ref1))
      return false;

   return true;
}

/**
 * \brief Returns whether none of the lower/upper sources in an instruction
 *        group are set.
 *
 * \param[in] igrp The instruction group.
 * \param[in] upper True if checking the upper sources.
 * \return True if none of the lower/upper sources are set.
 */
static inline bool pco_igrp_srcs_unset(pco_igrp *igrp, bool upper)
{
   unsigned offset = upper ? ROGUE_ALU_INPUT_GROUP_SIZE : 0;

   for (unsigned u = 0; u < ROGUE_ALU_INPUT_GROUP_SIZE; ++u)
      if (!pco_ref_is_null(igrp->srcs.s[u + offset]))
         return false;

   return true;
}

/**
 * \brief Returns whether none of the internal source selectors in an
 *        instruction group are set.
 *
 * \param[in] igrp The instruction group.
 * \return True if none of the internal source selectors are set.
 */
static inline bool pco_igrp_iss_unset(pco_igrp *igrp)
{
   for (unsigned u = 0; u < ARRAY_SIZE(igrp->iss.is); ++u)
      if (!pco_ref_is_null(igrp->iss.is[u]))
         return false;

   return true;
}

/**
 * \brief Returns whether none of the destinations in an instruction group are
 *        set.
 *
 * \param[in] igrp The instruction group.
 * \return True if none of the destinations are set.
 */
static inline bool pco_igrp_dests_unset(pco_igrp *igrp)
{
   for (unsigned u = 0; u < ARRAY_SIZE(igrp->dests.w); ++u)
      if (!pco_ref_is_null(igrp->dests.w[u]))
         return false;

   return true;
}

/**
 * \brief Iterates backwards to find the parent instruction of a source.
 *
 * \param[in] src The source whose parent is to be found.
 * \param[in] from The instruction to start iterating back from.
 * \return The parent instruction if found, else NULL.
 */
static inline pco_instr *find_parent_instr_from(pco_ref src, pco_instr *from)
{
   pco_foreach_instr_in_func_from_rev (instr, from) {
      pco_foreach_instr_dest_ssa (pdest, instr) {
         if (pco_refs_are_equal(*pdest, src))
            return instr;
      }
   }

   return NULL;
}

/* Common hw constants. */

/** Integer/float zero. */
#define pco_zero pco_ref_hwreg(0, PCO_REG_CLASS_CONST)

/** Integer one. */
#define pco_one pco_ref_hwreg(1, PCO_REG_CLASS_CONST)

/** Integer -1/true/0xffffffff. */
#define pco_true pco_ref_hwreg(143, PCO_REG_CLASS_CONST)

/** Float 1. */
#define pco_fone pco_ref_hwreg(64, PCO_REG_CLASS_CONST)

/* Printing. */
void pco_print_ref(pco_shader *shader, pco_ref ref);
void pco_print_instr(pco_shader *shader, pco_instr *instr);
void pco_print_igrp(pco_shader *shader, pco_igrp *igrp);
void pco_print_cf_node_name(pco_shader *shader, pco_cf_node *cf_node);
void pco_print_shader_info(pco_shader *shader);

#endif /* PCO_INTERNAL_H */
