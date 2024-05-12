/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */


#ifndef AC_NIR_HELPERS_H
#define AC_NIR_HELPERS_H

#include "ac_hw_stage.h"
#include "ac_shader_args.h"
#include "ac_shader_util.h"
#include "nir.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AC_NIR_STORE_IO(b, store_val, const_offset, write_mask, hi_16bit, func, ...) \
   do { \
      if ((store_val)->bit_size >= 32) { \
         const unsigned store_write_mask = (write_mask); \
         const unsigned store_const_offset = (const_offset); \
         func((b), (store_val), __VA_ARGS__); \
      } else { \
         u_foreach_bit(c, (write_mask)) { \
            const unsigned store_write_mask = 1; \
            const unsigned store_const_offset = (const_offset) + c * 4 + ((hi_16bit) ? 2 : 0); \
            nir_def *store_component = nir_channel(b, (store_val), c); \
            func((b), store_component, __VA_ARGS__); \
         } \
      } \
   } while (0)

#define AC_NIR_LOAD_IO(load, b, num_components, bit_size, hi_16bit, func, ...) \
   do { \
      const unsigned load_bit_size = MAX2(32, (bit_size)); \
      (load) = func((b), (num_components), load_bit_size, __VA_ARGS__); \
      if ((bit_size) < load_bit_size) { \
         if ((hi_16bit)) { \
            (load) = nir_unpack_32_2x16_split_y(b, load); \
         } else { \
            (load) = nir_unpack_32_2x16_split_x(b, load); \
         } \
      } \
   } while (0)

typedef struct
{
   /* GS output stream index, 2 bit per component */
   uint8_t stream;
   /* Bitmask of components used: 4 bits per slot, 1 bit per component. */
   uint8_t components_mask : 4;
} ac_nir_prerast_per_output_info;

typedef struct
{
   nir_def *outputs[VARYING_SLOT_MAX][4];
   nir_def *outputs_16bit_lo[16][4];
   nir_def *outputs_16bit_hi[16][4];

   nir_alu_type types[VARYING_SLOT_MAX][4];
   nir_alu_type types_16bit_lo[16][4];
   nir_alu_type types_16bit_hi[16][4];

   ac_nir_prerast_per_output_info infos[VARYING_SLOT_MAX];
   ac_nir_prerast_per_output_info infos_16bit_lo[16];
   ac_nir_prerast_per_output_info infos_16bit_hi[16];
} ac_nir_prerast_out;

/* Maps I/O semantics to the actual location used by the lowering pass. */
typedef unsigned (*ac_nir_map_io_driver_location)(unsigned semantic);

/* Forward declaration of nir_builder so we don't have to include nir_builder.h here */
struct nir_builder;
typedef struct nir_builder nir_builder;

/* Executed by ac_nir_cull when the current primitive is accepted. */
typedef void (*ac_nir_cull_accepted)(nir_builder *b, void *state);

void
ac_nir_store_var_components(nir_builder *b, nir_variable *var, nir_def *value,
                            unsigned component, unsigned writemask);

void
ac_nir_gather_prerast_store_output_info(nir_builder *b,
                                        nir_intrinsic_instr *intrin,
                                        ac_nir_prerast_out *out);

void
ac_nir_export_primitive(nir_builder *b, nir_def *prim, nir_def *row);

void
ac_nir_export_position(nir_builder *b,
                       enum amd_gfx_level gfx_level,
                       uint32_t clip_cull_mask,
                       bool no_param_export,
                       bool force_vrs,
                       bool done,
                       uint64_t outputs_written,
                       nir_def *(*outputs)[4],
                       nir_def *row);

void
ac_nir_export_parameters(nir_builder *b,
                         const uint8_t *param_offsets,
                         uint64_t outputs_written,
                         uint16_t outputs_written_16bit,
                         nir_def *(*outputs)[4],
                         nir_def *(*outputs_16bit_lo)[4],
                         nir_def *(*outputs_16bit_hi)[4]);

nir_def *
ac_nir_calc_io_offset(nir_builder *b,
                      nir_intrinsic_instr *intrin,
                      nir_def *base_stride,
                      unsigned component_stride,
                      ac_nir_map_io_driver_location map_io);

nir_def *
ac_nir_calc_io_offset_mapped(nir_builder *b,
                             nir_intrinsic_instr *intrin,
                             nir_def *base_stride,
                             unsigned component_stride,
                             unsigned mapped_location);

nir_def *
ac_nir_cull_primitive(nir_builder *b,
                      nir_def *initially_accepted,
                      nir_def *pos[3][4],
                      unsigned num_vertices,
                      ac_nir_cull_accepted accept_func,
                      void *state);

void
ac_nir_sleep(nir_builder *b, unsigned num_cycles);

#ifdef __cplusplus
}
#endif

#endif /* AC_NIR_HELPERS_H */
