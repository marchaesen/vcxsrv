/*
 * Copyright © 2017-2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef IR3_IMAGE_H_
#define IR3_IMAGE_H_

#include "ir3_context.h"

void ir3_ibo_mapping_init(struct ir3_ibo_mapping *mapping,
                          unsigned num_textures);
struct ir3_instruction *ir3_ssbo_to_ibo(struct ir3_context *ctx, nir_src src);
unsigned ir3_ssbo_to_tex(struct ir3_ibo_mapping *mapping, unsigned ssbo);
struct ir3_instruction *ir3_image_to_ibo(struct ir3_context *ctx, nir_src src);
unsigned ir3_image_to_tex(struct ir3_ibo_mapping *mapping, unsigned image);

unsigned ir3_get_image_coords(const nir_intrinsic_instr *instr,
                              unsigned *flagsp);
type_t ir3_get_type_for_image_intrinsic(const nir_intrinsic_instr *instr);
unsigned ir3_get_num_components_for_image_format(enum pipe_format);

#endif /* IR3_IMAGE_H_ */
