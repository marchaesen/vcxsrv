/*
 * Copyright (C) 2017-2018 Rob Clark <robclark@freedesktop.org>
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
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "ir3_image.h"


/*
 * SSBO/Image to/from IBO/tex hw mapping table:
 */

void
ir3_ibo_mapping_init(struct ir3_ibo_mapping *mapping, unsigned num_textures)
{
	memset(mapping, IBO_INVALID, sizeof(*mapping));
	mapping->num_ibo = 0;
	mapping->num_tex = 0;
	mapping->tex_base = num_textures;
}

unsigned
ir3_ssbo_to_ibo(struct ir3_ibo_mapping *mapping, unsigned ssbo)
{
	if (mapping->ssbo_to_ibo[ssbo] == IBO_INVALID) {
		unsigned ibo = mapping->num_ibo++;
		mapping->ssbo_to_ibo[ssbo] = ibo;
		mapping->ibo_to_image[ibo] = IBO_SSBO | ssbo;
	}
	return mapping->ssbo_to_ibo[ssbo];
}

unsigned
ir3_ssbo_to_tex(struct ir3_ibo_mapping *mapping, unsigned ssbo)
{
	if (mapping->ssbo_to_tex[ssbo] == IBO_INVALID) {
		unsigned tex = mapping->num_tex++;
		mapping->ssbo_to_tex[ssbo] = tex;
		mapping->tex_to_image[tex] = IBO_SSBO | ssbo;
	}
	return mapping->ssbo_to_tex[ssbo] + mapping->tex_base;
}

unsigned
ir3_image_to_ibo(struct ir3_ibo_mapping *mapping, unsigned image)
{
	if (mapping->image_to_ibo[image] == IBO_INVALID) {
		unsigned ibo = mapping->num_ibo++;
		mapping->image_to_ibo[image] = ibo;
		mapping->ibo_to_image[ibo] = image;
	}
	return mapping->image_to_ibo[image];
}

unsigned
ir3_image_to_tex(struct ir3_ibo_mapping *mapping, unsigned image)
{
	if (mapping->image_to_tex[image] == IBO_INVALID) {
		unsigned tex = mapping->num_tex++;
		mapping->image_to_tex[image] = tex;
		mapping->tex_to_image[tex] = image;
	}
	return mapping->image_to_tex[image] + mapping->tex_base;
}

/* Helper to parse the deref for an image to get image slot.  This should be
 * mapped to tex or ibo idx using ir3_image_to_tex() or ir3_image_to_ibo().
 */
unsigned
ir3_get_image_slot(nir_deref_instr *deref)
{
	unsigned int loc = 0;
	unsigned inner_size = 1;

	while (deref->deref_type != nir_deref_type_var) {
		assert(deref->deref_type == nir_deref_type_array);
		nir_const_value *const_index = nir_src_as_const_value(deref->arr.index);
		assert(const_index);

		/* Go to the next instruction */
		deref = nir_deref_instr_parent(deref);

		assert(glsl_type_is_array(deref->type));
		const unsigned array_len = glsl_get_length(deref->type);
		loc += MIN2(const_index->u32[0], array_len - 1) * inner_size;

		/* Update the inner size */
		inner_size *= array_len;
	}

	loc += deref->var->data.driver_location;

	return loc;
}

/* see tex_info() for equiv logic for texture instructions.. it would be
 * nice if this could be better unified..
 */
unsigned
ir3_get_image_coords(const nir_variable *var, unsigned *flagsp)
{
	const struct glsl_type *type = glsl_without_array(var->type);
	unsigned coords, flags = 0;

	switch (glsl_get_sampler_dim(type)) {
	case GLSL_SAMPLER_DIM_1D:
	case GLSL_SAMPLER_DIM_BUF:
		coords = 1;
		break;
	case GLSL_SAMPLER_DIM_2D:
	case GLSL_SAMPLER_DIM_RECT:
	case GLSL_SAMPLER_DIM_EXTERNAL:
	case GLSL_SAMPLER_DIM_MS:
		coords = 2;
		break;
	case GLSL_SAMPLER_DIM_3D:
	case GLSL_SAMPLER_DIM_CUBE:
		flags |= IR3_INSTR_3D;
		coords = 3;
		break;
	default:
		unreachable("bad sampler dim");
		return 0;
	}

	if (glsl_sampler_type_is_array(type)) {
		/* note: unlike tex_info(), adjust # of coords to include array idx: */
		coords++;
		flags |= IR3_INSTR_A;
	}

	if (flagsp)
		*flagsp = flags;

	return coords;
}

type_t
ir3_get_image_type(const nir_variable *var)
{
	switch (glsl_get_sampler_result_type(glsl_without_array(var->type))) {
	case GLSL_TYPE_UINT:
		return TYPE_U32;
	case GLSL_TYPE_INT:
		return TYPE_S32;
	case GLSL_TYPE_FLOAT:
		return TYPE_F32;
	default:
		unreachable("bad sampler type.");
		return 0;
	}
}

/* Returns the number of components for the different image formats
 * supported by the GLES 3.1 spec, plus those added by the
 * GL_NV_image_formats extension.
 */
unsigned
ir3_get_num_components_for_glformat(GLuint format)
{
	switch (format) {
	case GL_R32F:
	case GL_R32I:
	case GL_R32UI:
	case GL_R16F:
	case GL_R16I:
	case GL_R16UI:
	case GL_R16:
	case GL_R16_SNORM:
	case GL_R8I:
	case GL_R8UI:
	case GL_R8:
	case GL_R8_SNORM:
		return 1;

	case GL_RG32F:
	case GL_RG32I:
	case GL_RG32UI:
	case GL_RG16F:
	case GL_RG16I:
	case GL_RG16UI:
	case GL_RG16:
	case GL_RG16_SNORM:
	case GL_RG8I:
	case GL_RG8UI:
	case GL_RG8:
	case GL_RG8_SNORM:
		return 2;

	case GL_R11F_G11F_B10F:
		return 3;

	case GL_RGBA32F:
	case GL_RGBA32I:
	case GL_RGBA32UI:
	case GL_RGBA16F:
	case GL_RGBA16I:
	case GL_RGBA16UI:
	case GL_RGBA16:
	case GL_RGBA16_SNORM:
	case GL_RGBA8I:
	case GL_RGBA8UI:
	case GL_RGBA8:
	case GL_RGBA8_SNORM:
	case GL_RGB10_A2UI:
	case GL_RGB10_A2:
		return 4;

	case GL_NONE:
		/* Omitting the image format qualifier is allowed on desktop GL
		 * profiles. Assuming 4 components is always safe.
		 */
		return 4;

	default:
		/* Return 4 components also for all other formats we don't know
		 * about. The format should have been validated already by
		 * the higher level API, but drop a debug message just in case.
		 */
		debug_printf("Unhandled GL format %u while emitting imageStore()\n",
					 format);
		return 4;
	}
}
