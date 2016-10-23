/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "util/mesa-sha1.h"
#include "radv_private.h"
#include "sid.h"

VkResult radv_CreateDescriptorSetLayout(
	VkDevice                                    _device,
	const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkDescriptorSetLayout*                      pSetLayout)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_descriptor_set_layout *set_layout;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);

	uint32_t max_binding = 0;
	uint32_t immutable_sampler_count = 0;
	for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
		max_binding = MAX2(max_binding, pCreateInfo->pBindings[j].binding);
		if (pCreateInfo->pBindings[j].pImmutableSamplers)
			immutable_sampler_count += pCreateInfo->pBindings[j].descriptorCount;
	}

	size_t size = sizeof(struct radv_descriptor_set_layout) +
		(max_binding + 1) * sizeof(set_layout->binding[0]) +
		immutable_sampler_count * sizeof(struct radv_sampler *);

	set_layout = vk_alloc2(&device->alloc, pAllocator, size, 8,
				 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!set_layout)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	/* We just allocate all the samplers at the end of the struct */
	struct radv_sampler **samplers =
		(struct radv_sampler **)&set_layout->binding[max_binding + 1];

	set_layout->binding_count = max_binding + 1;
	set_layout->shader_stages = 0;
	set_layout->size = 0;

	memset(set_layout->binding, 0, size - sizeof(struct radv_descriptor_set_layout));

	uint32_t buffer_count = 0;
	uint32_t dynamic_offset_count = 0;

	for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
		const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[j];
		uint32_t b = binding->binding;
		uint32_t alignment;

		switch (binding->descriptorType) {
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			set_layout->binding[b].dynamic_offset_count = 1;
			set_layout->dynamic_shader_stages |= binding->stageFlags;
			set_layout->binding[b].size = 0;
			set_layout->binding[b].buffer_count = 1;
			alignment = 1;
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			set_layout->binding[b].size = 16;
			set_layout->binding[b].buffer_count = 1;
			alignment = 16;
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			/* main descriptor + fmask descriptor */
			set_layout->binding[b].size = 64;
			set_layout->binding[b].buffer_count = 1;
			alignment = 32;
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			/* main descriptor + fmask descriptor + sampler */
			set_layout->binding[b].size = 96;
			set_layout->binding[b].buffer_count = 1;
			alignment = 32;
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			set_layout->binding[b].size = 16;
			alignment = 16;
			break;
		default:
			unreachable("unknown descriptor type\n");
			break;
		}

		set_layout->size = align(set_layout->size, alignment);
		assert(binding->descriptorCount > 0);
		set_layout->binding[b].type = binding->descriptorType;
		set_layout->binding[b].array_size = binding->descriptorCount;
		set_layout->binding[b].offset = set_layout->size;
		set_layout->binding[b].buffer_offset = buffer_count;
		set_layout->binding[b].dynamic_offset_offset = dynamic_offset_count;

		set_layout->size += binding->descriptorCount * set_layout->binding[b].size;
		buffer_count += binding->descriptorCount * set_layout->binding[b].buffer_count;
		dynamic_offset_count += binding->descriptorCount *
			set_layout->binding[b].dynamic_offset_count;


		if (binding->pImmutableSamplers) {
			set_layout->binding[b].immutable_samplers = samplers;
			samplers += binding->descriptorCount;

			for (uint32_t i = 0; i < binding->descriptorCount; i++)
				set_layout->binding[b].immutable_samplers[i] =
					radv_sampler_from_handle(binding->pImmutableSamplers[i]);
		} else {
			set_layout->binding[b].immutable_samplers = NULL;
		}

		set_layout->shader_stages |= binding->stageFlags;
	}

	set_layout->buffer_count = buffer_count;
	set_layout->dynamic_offset_count = dynamic_offset_count;

	*pSetLayout = radv_descriptor_set_layout_to_handle(set_layout);

	return VK_SUCCESS;
}

void radv_DestroyDescriptorSetLayout(
	VkDevice                                    _device,
	VkDescriptorSetLayout                       _set_layout,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_set_layout, set_layout, _set_layout);

	if (!set_layout)
		return;

	vk_free2(&device->alloc, pAllocator, set_layout);
}

/*
 * Pipeline layouts.  These have nothing to do with the pipeline.  They are
 * just muttiple descriptor set layouts pasted together
 */

VkResult radv_CreatePipelineLayout(
	VkDevice                                    _device,
	const VkPipelineLayoutCreateInfo*           pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkPipelineLayout*                           pPipelineLayout)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_pipeline_layout *layout;
	struct mesa_sha1 *ctx;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);

	layout = vk_alloc2(&device->alloc, pAllocator, sizeof(*layout), 8,
			     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (layout == NULL)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	layout->num_sets = pCreateInfo->setLayoutCount;

	unsigned dynamic_offset_count = 0;


	ctx = _mesa_sha1_init();
	for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
		RADV_FROM_HANDLE(radv_descriptor_set_layout, set_layout,
				 pCreateInfo->pSetLayouts[set]);
		layout->set[set].layout = set_layout;

		layout->set[set].dynamic_offset_start = dynamic_offset_count;
		for (uint32_t b = 0; b < set_layout->binding_count; b++) {
			dynamic_offset_count += set_layout->binding[b].array_size * set_layout->binding[b].dynamic_offset_count;
		}
		_mesa_sha1_update(ctx, set_layout->binding,
				  sizeof(set_layout->binding[0]) * set_layout->binding_count);
	}

	layout->dynamic_offset_count = dynamic_offset_count;
	layout->push_constant_size = 0;
	for (unsigned i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
		const VkPushConstantRange *range = pCreateInfo->pPushConstantRanges + i;
		layout->push_constant_size = MAX2(layout->push_constant_size,
						  range->offset + range->size);
	}

	layout->push_constant_size = align(layout->push_constant_size, 16);
	_mesa_sha1_update(ctx, &layout->push_constant_size,
			  sizeof(layout->push_constant_size));
	_mesa_sha1_final(ctx, layout->sha1);
	*pPipelineLayout = radv_pipeline_layout_to_handle(layout);

	return VK_SUCCESS;
}

void radv_DestroyPipelineLayout(
	VkDevice                                    _device,
	VkPipelineLayout                            _pipelineLayout,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, _pipelineLayout);

	if (!pipeline_layout)
		return;
	vk_free2(&device->alloc, pAllocator, pipeline_layout);
}

#define EMPTY 1

static VkResult
radv_descriptor_set_create(struct radv_device *device,
			   struct radv_descriptor_pool *pool,
			   struct radv_cmd_buffer *cmd_buffer,
			   const struct radv_descriptor_set_layout *layout,
			   struct radv_descriptor_set **out_set)
{
	struct radv_descriptor_set *set;
	unsigned mem_size = sizeof(struct radv_descriptor_set) +
		sizeof(struct radeon_winsys_bo *) * layout->buffer_count;
	set = vk_alloc2(&device->alloc, NULL, mem_size, 8,
			  VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

	if (!set)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(set, 0, mem_size);

	if (layout->dynamic_offset_count) {
		unsigned size = sizeof(struct radv_descriptor_range) *
		                layout->dynamic_offset_count;
		set->dynamic_descriptors = vk_alloc2(&device->alloc, NULL, size, 8,
			                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

		if (!set->dynamic_descriptors) {
			vk_free2(&device->alloc, NULL, set);
			return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
		}
	}

	set->layout = layout;
	if (layout->size) {
		uint32_t layout_size = align_u32(layout->size, 32);
		set->size = layout->size;
		if (!cmd_buffer) {
			if (pool->current_offset + layout_size <= pool->size) {
				set->bo = pool->bo;
				set->mapped_ptr = (uint32_t*)(pool->mapped_ptr + pool->current_offset);
				set->va = device->ws->buffer_get_va(set->bo) + pool->current_offset;
				pool->current_offset += layout_size;

			} else {
				int entry = pool->free_list, prev_entry = -1;
				uint32_t offset;
				while (entry >= 0) {
					if (pool->free_nodes[entry].size >= layout_size) {
						if (prev_entry >= 0)
							pool->free_nodes[prev_entry].next = pool->free_nodes[entry].next;
						else
							pool->free_list = pool->free_nodes[entry].next;
						break;
					}
					prev_entry = entry;
					entry = pool->free_nodes[entry].next;
				}

				if (entry < 0) {
					vk_free2(&device->alloc, NULL, set);
					return vk_error(VK_ERROR_OUT_OF_DEVICE_MEMORY);
				}
				offset = pool->free_nodes[entry].offset;
				pool->free_nodes[entry].next = pool->full_list;
				pool->full_list = entry;

				set->bo = pool->bo;
				set->mapped_ptr = (uint32_t*)(pool->mapped_ptr + offset);
				set->va = device->ws->buffer_get_va(set->bo) + offset;
			}
		} else {
			unsigned bo_offset;
			if (!radv_cmd_buffer_upload_alloc(cmd_buffer, set->size, 32,
							  &bo_offset,
							  (void**)&set->mapped_ptr)) {
				vk_free2(&device->alloc, NULL, set->dynamic_descriptors);
				vk_free2(&device->alloc, NULL, set);
				return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);
			}

			set->va = device->ws->buffer_get_va(cmd_buffer->upload.upload_bo);
			set->va += bo_offset;
		}
	}

	if (pool)
		list_add(&set->descriptor_pool, &pool->descriptor_sets);
	else
		list_inithead(&set->descriptor_pool);

	for (unsigned i = 0; i < layout->binding_count; ++i) {
		if (!layout->binding[i].immutable_samplers)
			continue;

		unsigned offset = layout->binding[i].offset / 4;
		if (layout->binding[i].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			offset += 16;

		for (unsigned j = 0; j < layout->binding[i].array_size; ++j) {
			struct radv_sampler* sampler = layout->binding[i].immutable_samplers[j];

			memcpy(set->mapped_ptr + offset, &sampler->state, 16);
			offset += layout->binding[i].size / 4;
		}

	}
	*out_set = set;
	return VK_SUCCESS;
}

static void
radv_descriptor_set_destroy(struct radv_device *device,
			    struct radv_descriptor_pool *pool,
			    struct radv_descriptor_set *set,
			    bool free_bo)
{
	if (free_bo && set->size) {
		assert(pool->full_list >= 0);
		int next = pool->free_nodes[pool->full_list].next;
		pool->free_nodes[pool->full_list].next = pool->free_list;
		pool->free_nodes[pool->full_list].offset = (uint8_t*)set->mapped_ptr - pool->mapped_ptr;
		pool->free_nodes[pool->full_list].size = align_u32(set->size, 32);
		pool->free_list = pool->full_list;
		pool->full_list = next;
	}
	if (set->dynamic_descriptors)
		vk_free2(&device->alloc, NULL, set->dynamic_descriptors);
	if (!list_empty(&set->descriptor_pool))
		list_del(&set->descriptor_pool);
	vk_free2(&device->alloc, NULL, set);
}

VkResult
radv_temp_descriptor_set_create(struct radv_device *device,
				struct radv_cmd_buffer *cmd_buffer,
				VkDescriptorSetLayout _layout,
				VkDescriptorSet *_set)
{
	RADV_FROM_HANDLE(radv_descriptor_set_layout, layout, _layout);
	struct radv_descriptor_set *set;
	VkResult ret;

	ret = radv_descriptor_set_create(device, NULL, cmd_buffer, layout, &set);
	*_set = radv_descriptor_set_to_handle(set);
	return ret;
}

void
radv_temp_descriptor_set_destroy(struct radv_device *device,
				 VkDescriptorSet _set)
{
	RADV_FROM_HANDLE(radv_descriptor_set, set, _set);

	radv_descriptor_set_destroy(device, NULL, set, false);
}

VkResult radv_CreateDescriptorPool(
	VkDevice                                    _device,
	const VkDescriptorPoolCreateInfo*           pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkDescriptorPool*                           pDescriptorPool)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_descriptor_pool *pool;
	unsigned max_sets = pCreateInfo->maxSets * 2;
	int size = sizeof(struct radv_descriptor_pool) +
	           max_sets * sizeof(struct radv_descriptor_pool_free_node);
	uint64_t bo_size = 0;
	pool = vk_alloc2(&device->alloc, pAllocator, size, 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!pool)
		return vk_error(VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(pool, 0, sizeof(*pool));

	pool->free_list = -1;
	pool->full_list = 0;
	pool->free_nodes[max_sets - 1].next = -1;
	pool->max_sets = max_sets;

	for (int i = 0; i  + 1 < max_sets; ++i)
		pool->free_nodes[i].next = i + 1;

	for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
		switch(pCreateInfo->pPoolSizes[i].type) {
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			/* 32 as we may need to align for images */
			bo_size += 32 * pCreateInfo->pPoolSizes[i].descriptorCount;
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			bo_size += 64 * pCreateInfo->pPoolSizes[i].descriptorCount;
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			bo_size += 96 * pCreateInfo->pPoolSizes[i].descriptorCount;
			break;
		default:
			unreachable("unknown descriptor type\n");
			break;
		}
	}

	if (bo_size) {
		pool->bo = device->ws->buffer_create(device->ws, bo_size,
							32, RADEON_DOMAIN_VRAM, 0);
		pool->mapped_ptr = (uint8_t*)device->ws->buffer_map(pool->bo);
	}
	pool->size = bo_size;

	list_inithead(&pool->descriptor_sets);
	*pDescriptorPool = radv_descriptor_pool_to_handle(pool);
	return VK_SUCCESS;
}

void radv_DestroyDescriptorPool(
	VkDevice                                    _device,
	VkDescriptorPool                            _pool,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_pool, pool, _pool);

	if (!pool)
		return;

	list_for_each_entry_safe(struct radv_descriptor_set, set,
				 &pool->descriptor_sets, descriptor_pool) {
		radv_descriptor_set_destroy(device, pool, set, false);
	}

	if (pool->bo)
		device->ws->buffer_destroy(pool->bo);
	vk_free2(&device->alloc, pAllocator, pool);
}

VkResult radv_ResetDescriptorPool(
	VkDevice                                    _device,
	VkDescriptorPool                            descriptorPool,
	VkDescriptorPoolResetFlags                  flags)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_pool, pool, descriptorPool);

	list_for_each_entry_safe(struct radv_descriptor_set, set,
				 &pool->descriptor_sets, descriptor_pool) {
		radv_descriptor_set_destroy(device, pool, set, false);
	}

	pool->current_offset = 0;
	pool->free_list = -1;
	pool->full_list = 0;
	pool->free_nodes[pool->max_sets - 1].next = -1;

	for (int i = 0; i  + 1 < pool->max_sets; ++i)
		pool->free_nodes[i].next = i + 1;

	return VK_SUCCESS;
}

VkResult radv_AllocateDescriptorSets(
	VkDevice                                    _device,
	const VkDescriptorSetAllocateInfo*          pAllocateInfo,
	VkDescriptorSet*                            pDescriptorSets)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_pool, pool, pAllocateInfo->descriptorPool);

	VkResult result = VK_SUCCESS;
	uint32_t i;
	struct radv_descriptor_set *set;

	/* allocate a set of buffers for each shader to contain descriptors */
	for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
		RADV_FROM_HANDLE(radv_descriptor_set_layout, layout,
				 pAllocateInfo->pSetLayouts[i]);

		result = radv_descriptor_set_create(device, pool, NULL, layout, &set);
		if (result != VK_SUCCESS)
			break;

		pDescriptorSets[i] = radv_descriptor_set_to_handle(set);
	}

	if (result != VK_SUCCESS)
		radv_FreeDescriptorSets(_device, pAllocateInfo->descriptorPool,
					i, pDescriptorSets);
	return result;
}

VkResult radv_FreeDescriptorSets(
	VkDevice                                    _device,
	VkDescriptorPool                            descriptorPool,
	uint32_t                                    count,
	const VkDescriptorSet*                      pDescriptorSets)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_pool, pool, descriptorPool);

	for (uint32_t i = 0; i < count; i++) {
		RADV_FROM_HANDLE(radv_descriptor_set, set, pDescriptorSets[i]);

		if (set)
			radv_descriptor_set_destroy(device, pool, set, true);
	}
	return VK_SUCCESS;
}

static void write_texel_buffer_descriptor(struct radv_device *device,
					  unsigned *dst,
					  struct radeon_winsys_bo **buffer_list,
					  const VkBufferView _buffer_view)
{
	RADV_FROM_HANDLE(radv_buffer_view, buffer_view, _buffer_view);

	memcpy(dst, buffer_view->state, 4 * 4);
	*buffer_list = buffer_view->bo;
}

static void write_buffer_descriptor(struct radv_device *device,
                                    unsigned *dst,
                                    struct radeon_winsys_bo **buffer_list,
                                    const VkDescriptorBufferInfo *buffer_info)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, buffer_info->buffer);
	uint64_t va = device->ws->buffer_get_va(buffer->bo);
	uint32_t range = buffer_info->range;

	if (buffer_info->range == VK_WHOLE_SIZE)
		range = buffer->size - buffer_info->offset;

	va += buffer_info->offset + buffer->offset;
	dst[0] = va;
	dst[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
	dst[2] = range;
	dst[3] = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
		S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
		S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
		S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W) |
		S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
		S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);

	*buffer_list = buffer->bo;
}

static void write_dynamic_buffer_descriptor(struct radv_device *device,
                                            struct radv_descriptor_range *range,
                                            struct radeon_winsys_bo **buffer_list,
                                            const VkDescriptorBufferInfo *buffer_info)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, buffer_info->buffer);
	uint64_t va = device->ws->buffer_get_va(buffer->bo);
	unsigned size = buffer_info->range;

	if (buffer_info->range == VK_WHOLE_SIZE)
		size = buffer->size - buffer_info->offset;

	va += buffer_info->offset + buffer->offset;
	range->va = va;
	range->size = size;

	*buffer_list = buffer->bo;
}

static void
write_image_descriptor(struct radv_device *device,
		       unsigned *dst,
		       struct radeon_winsys_bo **buffer_list,
		       const VkDescriptorImageInfo *image_info)
{
	RADV_FROM_HANDLE(radv_image_view, iview, image_info->imageView);
	memcpy(dst, iview->descriptor, 8 * 4);
	memcpy(dst + 8, iview->fmask_descriptor, 8 * 4);
	*buffer_list = iview->bo;
}

static void
write_combined_image_sampler_descriptor(struct radv_device *device,
					unsigned *dst,
					struct radeon_winsys_bo **buffer_list,
					const VkDescriptorImageInfo *image_info,
					bool has_sampler)
{
	RADV_FROM_HANDLE(radv_sampler, sampler, image_info->sampler);

	write_image_descriptor(device, dst, buffer_list, image_info);
	/* copy over sampler state */
	if (has_sampler)
		memcpy(dst + 16, sampler->state, 16);
}

static void
write_sampler_descriptor(struct radv_device *device,
					unsigned *dst,
					const VkDescriptorImageInfo *image_info)
{
	RADV_FROM_HANDLE(radv_sampler, sampler, image_info->sampler);

	memcpy(dst, sampler->state, 16);
}

void radv_UpdateDescriptorSets(
	VkDevice                                    _device,
	uint32_t                                    descriptorWriteCount,
	const VkWriteDescriptorSet*                 pDescriptorWrites,
	uint32_t                                    descriptorCopyCount,
	const VkCopyDescriptorSet*                  pDescriptorCopies)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	uint32_t i, j;
	for (i = 0; i < descriptorWriteCount; i++) {
		const VkWriteDescriptorSet *writeset = &pDescriptorWrites[i];
		RADV_FROM_HANDLE(radv_descriptor_set, set, writeset->dstSet);
		const struct radv_descriptor_set_binding_layout *binding_layout =
			set->layout->binding + writeset->dstBinding;
		uint32_t *ptr = set->mapped_ptr;
		struct radeon_winsys_bo **buffer_list =  set->descriptors;

		ptr += binding_layout->offset / 4;
		ptr += binding_layout->size * writeset->dstArrayElement / 4;
		buffer_list += binding_layout->buffer_offset;
		buffer_list += binding_layout->buffer_count * writeset->dstArrayElement;
		for (j = 0; j < writeset->descriptorCount; ++j) {
			switch(writeset->descriptorType) {
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
				unsigned idx = writeset->dstArrayElement + j;
				idx += binding_layout->dynamic_offset_offset;
				write_dynamic_buffer_descriptor(device, set->dynamic_descriptors + idx,
								buffer_list, writeset->pBufferInfo + j);
				break;
			}
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				write_buffer_descriptor(device, ptr, buffer_list,
							writeset->pBufferInfo + j);
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				write_texel_buffer_descriptor(device, ptr, buffer_list,
							      writeset->pTexelBufferView[j]);
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
				write_image_descriptor(device, ptr, buffer_list,
						       writeset->pImageInfo + j);
				break;
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				write_combined_image_sampler_descriptor(device, ptr, buffer_list,
									writeset->pImageInfo + j,
									!binding_layout->immutable_samplers);
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				assert(!binding_layout->immutable_samplers);
				write_sampler_descriptor(device, ptr,
							 writeset->pImageInfo + j);
				break;
			default:
				unreachable("unimplemented descriptor type");
				break;
			}
			ptr += binding_layout->size / 4;
			buffer_list += binding_layout->buffer_count;
		}

	}
	if (descriptorCopyCount)
		radv_finishme("copy descriptors");
}
