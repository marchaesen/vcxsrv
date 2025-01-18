/*
 * Copyright 2023 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "compiler/libcl/libcl.h"

uint32_t nir_interleave_agx(uint16_t x, uint16_t y);
void nir_doorbell_agx(uint8_t value);
void nir_stack_map_agx(uint16_t index, uint32_t address);
uint32_t nir_stack_unmap_agx(uint16_t index);
uint32_t nir_load_core_id_agx(void);
uint32_t nir_load_helper_op_id_agx(void);
uint32_t nir_load_helper_arg_lo_agx(void);
uint32_t nir_load_helper_arg_hi_agx(void);
void nir_fence_helper_exit_agx(void);

uint4 nir_bindless_image_load(uint2 handle, int4 coord, uint sample, uint lod,
                              uint image_dim, uint image_array, uint format,
                              uint access, uint dest_type);

void nir_bindless_image_store(uint2 handle, int4 coord, uint sample,
                              uint4 datum, uint lod, uint image_dim,
                              uint image_array, uint format, uint access,
                              uint src_type);
