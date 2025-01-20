/*
 * Copyright © 2024 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PCO_DATA_H
#define PCO_DATA_H

/**
 * \file pco_data.h
 *
 * \brief PCO shader-specific data/compiler-driver interface.
 */

#include "compiler/shader_enums.h"
#include "util/format/u_format.h"

#include <stdbool.h>

/** Generic range struct. */
typedef struct _pco_range {
   unsigned start;
   unsigned count;
} pco_range;

/** PCO vertex shader-specific data. */
typedef struct _pco_vs_data {
   /** Attributes/input mappings. */
   pco_range attribs[VERT_ATTRIB_MAX];

   enum pipe_format attrib_formats[VERT_ATTRIB_MAX];

   /** Varyings/output mappings. */
   pco_range varyings[VARYING_SLOT_MAX];

   unsigned f32_smooth; /** Number of F32 linear varyings. */
   unsigned f32_flat; /** Number of F32 flat varyings. */
   unsigned f32_npc; /** Number of F32 NPC varyings. */

   unsigned f16_smooth; /** Number of F16 linear varyings. */
   unsigned f16_flat; /** Number of F16 flat varyings. */
   unsigned f16_npc; /** Number of F16 NPC varyings. */

   unsigned vtxouts; /** How many vertex outputs are written to. */
} pco_vs_data;

/** PCO fragment shader-specific data. */
typedef struct _pco_fs_data {
   /** Varyings/input mappings. */
   pco_range varyings[VARYING_SLOT_MAX];

   /** Results/output mappings. */
   pco_range outputs[FRAG_RESULT_MAX];

   /** If outputs are to be placed in pixout regs. */
   bool output_reg[FRAG_RESULT_MAX];

   /** Fragment output formats. */
   enum pipe_format output_formats[FRAG_RESULT_MAX];

   struct {
      bool w; /** Whether the shader uses pos.w. */
      bool z; /** Whether the shader uses pos.z */
      bool pntc; /** Whether the shader uses point coord. */
      bool phase_change; /** Whether the shader does a phase change. */
   } uses;
} pco_fs_data;

/** PCO compute shader-specific data. */
typedef struct _pco_cs_data {
   /**/
} pco_cs_data;

/** PCO common data. */
typedef struct _pco_common_data {
   /** System value mappings. */
   pco_range sys_vals[SYSTEM_VALUE_MAX];

   unsigned temps; /** Number of allocated temp registers. */
   unsigned vtxins; /** Number of allocated vertex input registers. */
   unsigned interns; /** Number of allocated internal registers. */

   unsigned coeffs; /** Number of allocated coefficient registers. */
   unsigned shareds; /** Number of allocated shared registers. */

   unsigned entry_offset; /** Offset of the shader entrypoint. */

   struct {
      bool atomics; /** Whether the shader uses atomics. */
      bool barriers; /** Whether the shader uses barriers. */
      bool side_effects; /** Whether the shader has side effects. */
      bool empty; /** Whether the shader is empty. */
   } uses;
} pco_common_data;

/** PCO shader data. */
typedef struct _pco_data {
   union {
      pco_vs_data vs;
      pco_fs_data fs;
      pco_cs_data cs;
   };

   pco_common_data common;
} pco_data;

#endif /* PCO_DATA_H */
