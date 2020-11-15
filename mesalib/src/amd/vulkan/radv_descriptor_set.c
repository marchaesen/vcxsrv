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
#include "vk_format.h"
#include "vk_util.h"


static bool has_equal_immutable_samplers(const VkSampler *samplers, uint32_t count)
{
	if (!samplers)
		return false;
	for(uint32_t i = 1; i < count; ++i) {
		if (memcmp(radv_sampler_from_handle(samplers[0])->state,
		           radv_sampler_from_handle(samplers[i])->state, 16)) {
			return false;
		}
	}
	return true;
}

static int binding_compare(const void* av, const void *bv)
{
	const VkDescriptorSetLayoutBinding *a = (const VkDescriptorSetLayoutBinding*)av;
	const VkDescriptorSetLayoutBinding *b = (const VkDescriptorSetLayoutBinding*)bv;

	return (a->binding < b->binding) ? -1 : (a->binding > b->binding) ? 1 : 0;
}

static VkDescriptorSetLayoutBinding *
create_sorted_bindings(const VkDescriptorSetLayoutBinding *bindings, unsigned count) {
	VkDescriptorSetLayoutBinding *sorted_bindings = malloc(MAX2(count * sizeof(VkDescriptorSetLayoutBinding), 1));
	if (!sorted_bindings)
		return NULL;

	if (count) {
		memcpy(sorted_bindings, bindings, count * sizeof(VkDescriptorSetLayoutBinding));
		qsort(sorted_bindings, count, sizeof(VkDescriptorSetLayoutBinding), binding_compare);
	}

	return sorted_bindings;
}

VkResult radv_CreateDescriptorSetLayout(
	VkDevice                                    _device,
	const VkDescriptorSetLayoutCreateInfo*      pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkDescriptorSetLayout*                      pSetLayout)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_descriptor_set_layout *set_layout;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
	const VkDescriptorSetLayoutBindingFlagsCreateInfo *variable_flags =
		vk_find_struct_const(pCreateInfo->pNext, DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);

	uint32_t num_bindings = 0;
	uint32_t immutable_sampler_count = 0;
	uint32_t ycbcr_sampler_count = 0;
	for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
		num_bindings = MAX2(num_bindings, pCreateInfo->pBindings[j].binding + 1);
		if ((pCreateInfo->pBindings[j].descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
		     pCreateInfo->pBindings[j].descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) &&
		     pCreateInfo->pBindings[j].pImmutableSamplers) {
			immutable_sampler_count += pCreateInfo->pBindings[j].descriptorCount;

			bool has_ycbcr_sampler = false;
			for (unsigned i = 0; i < pCreateInfo->pBindings[j].descriptorCount; ++i) {
				if (radv_sampler_from_handle(pCreateInfo->pBindings[j].pImmutableSamplers[i])->ycbcr_sampler)
					has_ycbcr_sampler = true;
			}

			if (has_ycbcr_sampler)
				ycbcr_sampler_count += pCreateInfo->pBindings[j].descriptorCount;
		}
	}

	uint32_t samplers_offset =
			offsetof(struct radv_descriptor_set_layout, binding[num_bindings]);
	size_t size = samplers_offset + immutable_sampler_count * 4 * sizeof(uint32_t);
	if (ycbcr_sampler_count > 0) {
		/* Store block of offsets first, followed by the conversion descriptors (padded to the struct alignment) */
		size += num_bindings * sizeof(uint32_t);
		size = ALIGN(size, alignof(struct radv_sampler_ycbcr_conversion));
		size += ycbcr_sampler_count * sizeof(struct radv_sampler_ycbcr_conversion);
	}

	set_layout = vk_zalloc2(&device->vk.alloc, pAllocator, size, 8,
	                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!set_layout)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	vk_object_base_init(&device->vk, &set_layout->base,
			    VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT);

	set_layout->flags = pCreateInfo->flags;
	set_layout->layout_size = size;

	/* We just allocate all the samplers at the end of the struct */
	uint32_t *samplers = (uint32_t*)&set_layout->binding[num_bindings];
	struct radv_sampler_ycbcr_conversion *ycbcr_samplers = NULL;
	uint32_t *ycbcr_sampler_offsets = NULL;

	if (ycbcr_sampler_count > 0) {
		ycbcr_sampler_offsets = samplers + 4 * immutable_sampler_count;
		set_layout->ycbcr_sampler_offsets_offset = (char*)ycbcr_sampler_offsets - (char*)set_layout;

		uintptr_t first_ycbcr_sampler_offset = (uintptr_t)ycbcr_sampler_offsets + sizeof(uint32_t) * num_bindings;
		first_ycbcr_sampler_offset = ALIGN(first_ycbcr_sampler_offset, alignof(struct radv_sampler_ycbcr_conversion));
		ycbcr_samplers = (struct radv_sampler_ycbcr_conversion *)first_ycbcr_sampler_offset;
	} else
		set_layout->ycbcr_sampler_offsets_offset = 0;

	VkDescriptorSetLayoutBinding *bindings = create_sorted_bindings(pCreateInfo->pBindings,
	                                                                pCreateInfo->bindingCount);
	if (!bindings) {
		vk_object_base_finish(&set_layout->base);
		vk_free2(&device->vk.alloc, pAllocator, set_layout);
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
	}

	set_layout->binding_count = num_bindings;
	set_layout->shader_stages = 0;
	set_layout->dynamic_shader_stages = 0;
	set_layout->has_immutable_samplers = false;
	set_layout->size = 0;

	uint32_t buffer_count = 0;
	uint32_t dynamic_offset_count = 0;

	for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
		const VkDescriptorSetLayoutBinding *binding = bindings + j;
		uint32_t b = binding->binding;
		uint32_t alignment = 0;
		unsigned binding_buffer_count = 0;
		uint32_t descriptor_count = binding->descriptorCount;
		bool has_ycbcr_sampler = false;

		/* main image + fmask */
		uint32_t max_sampled_image_descriptors = 2;

		if (binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
		    binding->pImmutableSamplers) {
			for (unsigned i = 0; i < binding->descriptorCount; ++i) {
				struct radv_sampler_ycbcr_conversion *conversion =
					radv_sampler_from_handle(binding->pImmutableSamplers[i])->ycbcr_sampler;

				if (conversion) {
					has_ycbcr_sampler = true;
					max_sampled_image_descriptors = MAX2(max_sampled_image_descriptors,
					                                     vk_format_get_plane_count(conversion->format));
				}
			}
		}

		switch (binding->descriptorType) {
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			assert(!(pCreateInfo->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
			set_layout->binding[b].dynamic_offset_count = 1;
			set_layout->dynamic_shader_stages |= binding->stageFlags;
			set_layout->binding[b].size = 0;
			binding_buffer_count = 1;
			alignment = 1;
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			set_layout->binding[b].size = 16;
			binding_buffer_count = 1;
			alignment = 16;
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			/* main descriptor + fmask descriptor */
			set_layout->binding[b].size = 64;
			binding_buffer_count = 1;
			alignment = 32;
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			/* main descriptor + fmask descriptor + sampler */
			set_layout->binding[b].size = 96;
			binding_buffer_count = 1;
			alignment = 32;
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			set_layout->binding[b].size = 16;
			alignment = 16;
			break;
		case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
			alignment = 16;
			set_layout->binding[b].size = descriptor_count;
			descriptor_count = 1;
			break;
		default:
			break;
		}

		set_layout->size = align(set_layout->size, alignment);
		set_layout->binding[b].type = binding->descriptorType;
		set_layout->binding[b].array_size = descriptor_count;
		set_layout->binding[b].offset = set_layout->size;
		set_layout->binding[b].buffer_offset = buffer_count;
		set_layout->binding[b].dynamic_offset_offset = dynamic_offset_count;

		if (variable_flags && binding->binding < variable_flags->bindingCount &&
		    (variable_flags->pBindingFlags[binding->binding] & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT)) {
			assert(!binding->pImmutableSamplers); /* Terribly ill defined  how many samplers are valid */
			assert(binding->binding == num_bindings - 1);

			set_layout->has_variable_descriptors = true;
		}

		if ((binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER ||
		     binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) &&
		    binding->pImmutableSamplers) {
			set_layout->binding[b].immutable_samplers_offset = samplers_offset;
			set_layout->binding[b].immutable_samplers_equal =
				has_equal_immutable_samplers(binding->pImmutableSamplers, binding->descriptorCount);
			set_layout->has_immutable_samplers = true;


			for (uint32_t i = 0; i < binding->descriptorCount; i++)
				memcpy(samplers + 4 * i, &radv_sampler_from_handle(binding->pImmutableSamplers[i])->state, 16);

			/* Don't reserve space for the samplers if they're not accessed. */
			if (set_layout->binding[b].immutable_samplers_equal) {
				if (binding->descriptorType == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
				    max_sampled_image_descriptors <= 2)
					set_layout->binding[b].size -= 32;
				else if (binding->descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER)
					set_layout->binding[b].size -= 16;
			}
			samplers += 4 * binding->descriptorCount;
			samplers_offset += 4 * sizeof(uint32_t) * binding->descriptorCount;

			if (has_ycbcr_sampler) {
				ycbcr_sampler_offsets[b] = (const char*)ycbcr_samplers - (const char*)set_layout;
				for (uint32_t i = 0; i < binding->descriptorCount; i++) {
					if (radv_sampler_from_handle(binding->pImmutableSamplers[i])->ycbcr_sampler)
						ycbcr_samplers[i] = *radv_sampler_from_handle(binding->pImmutableSamplers[i])->ycbcr_sampler;
					else
						ycbcr_samplers[i].format = VK_FORMAT_UNDEFINED;
				}
				ycbcr_samplers += binding->descriptorCount;
			}
		}

		set_layout->size += descriptor_count * set_layout->binding[b].size;
		buffer_count += descriptor_count * binding_buffer_count;
		dynamic_offset_count += descriptor_count *
			set_layout->binding[b].dynamic_offset_count;
		set_layout->shader_stages |= binding->stageFlags;
	}

	free(bindings);

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

	vk_object_base_finish(&set_layout->base);
	vk_free2(&device->vk.alloc, pAllocator, set_layout);
}

void radv_GetDescriptorSetLayoutSupport(VkDevice device,
                                        const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
                                        VkDescriptorSetLayoutSupport* pSupport)
{
	VkDescriptorSetLayoutBinding *bindings = create_sorted_bindings(pCreateInfo->pBindings,
	                                                                pCreateInfo->bindingCount);
	if (!bindings) {
		pSupport->supported = false;
		return;
	}

	const VkDescriptorSetLayoutBindingFlagsCreateInfo *variable_flags =
		vk_find_struct_const(pCreateInfo->pNext, DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);
	VkDescriptorSetVariableDescriptorCountLayoutSupport *variable_count =
		vk_find_struct((void*)pCreateInfo->pNext, DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_LAYOUT_SUPPORT);
	if (variable_count) {
		variable_count->maxVariableDescriptorCount = 0;
	}

	bool supported = true;
	uint64_t size = 0;
	for (uint32_t i = 0; i < pCreateInfo->bindingCount; i++) {
		const VkDescriptorSetLayoutBinding *binding = bindings + i;

		uint64_t descriptor_size = 0;
		uint64_t descriptor_alignment = 1;
		uint32_t descriptor_count = binding->descriptorCount;
		switch (binding->descriptorType) {
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			break;
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
		case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
			descriptor_size = 16;
			descriptor_alignment = 16;
			break;
		case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
		case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
		case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			descriptor_size = 64;
			descriptor_alignment = 32;
			break;
		case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			if (!has_equal_immutable_samplers(binding->pImmutableSamplers, descriptor_count)) {
				descriptor_size = 64;
			} else {
				descriptor_size = 96;
			}
			descriptor_alignment = 32;
			break;
		case VK_DESCRIPTOR_TYPE_SAMPLER:
			if (!has_equal_immutable_samplers(binding->pImmutableSamplers, descriptor_count)) {
				descriptor_size = 16;
				descriptor_alignment = 16;
			}
			break;
		case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
			descriptor_alignment = 16;
			descriptor_size = descriptor_count;
			descriptor_count = 1;
			break;
		default:
			break;
		}

		if (size && !align_u64(size, descriptor_alignment)) {
			supported = false;
		}
		size = align_u64(size, descriptor_alignment);

		uint64_t max_count = INT32_MAX;
		if (binding->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT)
			max_count = INT32_MAX - size;
		else if (descriptor_size)
			max_count = (INT32_MAX - size) / descriptor_size;

		if (max_count < descriptor_count) {
			supported = false;
		}
		if (variable_flags && binding->binding <variable_flags->bindingCount && variable_count &&
		    (variable_flags->pBindingFlags[binding->binding] & VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT)) {
			variable_count->maxVariableDescriptorCount = MIN2(UINT32_MAX, max_count);
		}
		size += descriptor_count * descriptor_size;
	}

	free(bindings);

	pSupport->supported = supported;
}

/*
 * Pipeline layouts.  These have nothing to do with the pipeline.  They are
 * just multiple descriptor set layouts pasted together.
 */

VkResult radv_CreatePipelineLayout(
	VkDevice                                    _device,
	const VkPipelineLayoutCreateInfo*           pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkPipelineLayout*                           pPipelineLayout)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_pipeline_layout *layout;
	struct mesa_sha1 ctx;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);

	layout = vk_alloc2(&device->vk.alloc, pAllocator, sizeof(*layout), 8,
			     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (layout == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	vk_object_base_init(&device->vk, &layout->base,
			    VK_OBJECT_TYPE_PIPELINE_LAYOUT);

	layout->num_sets = pCreateInfo->setLayoutCount;

	unsigned dynamic_offset_count = 0;
	uint16_t dynamic_shader_stages = 0;


	_mesa_sha1_init(&ctx);
	for (uint32_t set = 0; set < pCreateInfo->setLayoutCount; set++) {
		RADV_FROM_HANDLE(radv_descriptor_set_layout, set_layout,
				 pCreateInfo->pSetLayouts[set]);
		layout->set[set].layout = set_layout;

		layout->set[set].dynamic_offset_start = dynamic_offset_count;
		layout->set[set].dynamic_offset_count = 0;
		layout->set[set].dynamic_offset_stages = 0;

		for (uint32_t b = 0; b < set_layout->binding_count; b++) {
			layout->set[set].dynamic_offset_count +=
				set_layout->binding[b].array_size * set_layout->binding[b].dynamic_offset_count;
			layout->set[set].dynamic_offset_stages |= set_layout->dynamic_shader_stages;
		}
		dynamic_offset_count += layout->set[set].dynamic_offset_count;
		dynamic_shader_stages |= layout->set[set].dynamic_offset_stages;
		_mesa_sha1_update(&ctx, set_layout, set_layout->layout_size);
	}

	layout->dynamic_offset_count = dynamic_offset_count;
	layout->dynamic_shader_stages = dynamic_shader_stages;
	layout->push_constant_size = 0;

	for (unsigned i = 0; i < pCreateInfo->pushConstantRangeCount; ++i) {
		const VkPushConstantRange *range = pCreateInfo->pPushConstantRanges + i;
		layout->push_constant_size = MAX2(layout->push_constant_size,
						  range->offset + range->size);
	}

	layout->push_constant_size = align(layout->push_constant_size, 16);
	_mesa_sha1_update(&ctx, &layout->push_constant_size,
			  sizeof(layout->push_constant_size));
	_mesa_sha1_final(&ctx, layout->sha1);
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

	vk_object_base_finish(&pipeline_layout->base);
	vk_free2(&device->vk.alloc, pAllocator, pipeline_layout);
}

#define EMPTY 1

static VkResult
radv_descriptor_set_create(struct radv_device *device,
			   struct radv_descriptor_pool *pool,
			   const struct radv_descriptor_set_layout *layout,
			   const uint32_t *variable_count,
			   struct radv_descriptor_set **out_set)
{
	struct radv_descriptor_set *set;
	uint32_t buffer_count = layout->buffer_count;
	if (variable_count) {
		unsigned stride = 1;
		if (layout->binding[layout->binding_count - 1].type == VK_DESCRIPTOR_TYPE_SAMPLER ||
		    layout->binding[layout->binding_count - 1].type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT)
			stride = 0;
		buffer_count = layout->binding[layout->binding_count - 1].buffer_offset +
		               *variable_count * stride;
	}
	unsigned range_offset = sizeof(struct radv_descriptor_set) +
		sizeof(struct radeon_winsys_bo *) * buffer_count;
	unsigned mem_size = range_offset +
		sizeof(struct radv_descriptor_range) * layout->dynamic_offset_count;

	if (pool->host_memory_base) {
		if (pool->host_memory_end - pool->host_memory_ptr < mem_size)
			return vk_error(device->instance, VK_ERROR_OUT_OF_POOL_MEMORY);

		set = (struct radv_descriptor_set*)pool->host_memory_ptr;
		pool->host_memory_ptr += mem_size;
	} else {
		set = vk_alloc2(&device->vk.alloc, NULL, mem_size, 8,
		                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

		if (!set)
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
	}

	memset(set, 0, mem_size);

	vk_object_base_init(&device->vk, &set->base,
			    VK_OBJECT_TYPE_DESCRIPTOR_SET);

	if (layout->dynamic_offset_count) {
		set->dynamic_descriptors = (struct radv_descriptor_range*)((uint8_t*)set + range_offset);
	}

	set->layout = layout;
	set->buffer_count = buffer_count;
	uint32_t layout_size = layout->size;
	if (variable_count) {
		assert(layout->has_variable_descriptors);
		uint32_t stride = layout->binding[layout->binding_count - 1].size;
		if (layout->binding[layout->binding_count - 1].type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT)
			stride = 1;

		layout_size = layout->binding[layout->binding_count - 1].offset +
		              *variable_count * stride;
	}
	layout_size = align_u32(layout_size, 32);
	if (layout_size) {
		set->size = layout_size;

		if (!pool->host_memory_base && pool->entry_count == pool->max_entry_count) {
			vk_free2(&device->vk.alloc, NULL, set);
			return vk_error(device->instance, VK_ERROR_OUT_OF_POOL_MEMORY);
		}

		/* try to allocate linearly first, so that we don't spend
		 * time looking for gaps if the app only allocates &
		 * resets via the pool. */
		if (pool->current_offset + layout_size <= pool->size) {
			set->bo = pool->bo;
			set->mapped_ptr = (uint32_t*)(pool->mapped_ptr + pool->current_offset);
			set->va = radv_buffer_get_va(set->bo) + pool->current_offset;
			if (!pool->host_memory_base) {
				pool->entries[pool->entry_count].offset = pool->current_offset;
				pool->entries[pool->entry_count].size = layout_size;
				pool->entries[pool->entry_count].set = set;
				pool->entry_count++;
			}
			pool->current_offset += layout_size;
		} else if (!pool->host_memory_base) {
			uint64_t offset = 0;
			int index;

			for (index = 0; index < pool->entry_count; ++index) {
				if (pool->entries[index].offset - offset >= layout_size)
					break;
				offset = pool->entries[index].offset + pool->entries[index].size;
			}

			if (pool->size - offset < layout_size) {
				vk_free2(&device->vk.alloc, NULL, set);
				return vk_error(device->instance, VK_ERROR_OUT_OF_POOL_MEMORY);
			}
			set->bo = pool->bo;
			set->mapped_ptr = (uint32_t*)(pool->mapped_ptr + offset);
			set->va = radv_buffer_get_va(set->bo) + offset;
			memmove(&pool->entries[index + 1], &pool->entries[index],
				sizeof(pool->entries[0]) * (pool->entry_count - index));
			pool->entries[index].offset = offset;
			pool->entries[index].size = layout_size;
			pool->entries[index].set = set;
			pool->entry_count++;
		} else
			return vk_error(device->instance, VK_ERROR_OUT_OF_POOL_MEMORY);
	}

	if (layout->has_immutable_samplers) {
		for (unsigned i = 0; i < layout->binding_count; ++i) {
			if (!layout->binding[i].immutable_samplers_offset ||
			layout->binding[i].immutable_samplers_equal)
				continue;

			unsigned offset = layout->binding[i].offset / 4;
			if (layout->binding[i].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
				offset += radv_combined_image_descriptor_sampler_offset(layout->binding + i) / 4;

			const uint32_t *samplers = (const uint32_t*)((const char*)layout + layout->binding[i].immutable_samplers_offset);
			for (unsigned j = 0; j < layout->binding[i].array_size; ++j) {
				memcpy(set->mapped_ptr + offset, samplers + 4 * j, 16);
				offset += layout->binding[i].size / 4;
			}

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
	assert(!pool->host_memory_base);

	if (free_bo && set->size && !pool->host_memory_base) {
		uint32_t offset = (uint8_t*)set->mapped_ptr - pool->mapped_ptr;
		for (int i = 0; i < pool->entry_count; ++i) {
			if (pool->entries[i].offset == offset) {
				memmove(&pool->entries[i], &pool->entries[i+1],
					sizeof(pool->entries[i]) * (pool->entry_count - i - 1));
				--pool->entry_count;
				break;
			}
		}
	}
	vk_object_base_finish(&set->base);
	vk_free2(&device->vk.alloc, NULL, set);
}

static void radv_destroy_descriptor_pool(struct radv_device *device,
                                         const VkAllocationCallbacks *pAllocator,
                                         struct radv_descriptor_pool *pool)
{
	if (!pool->host_memory_base) {
		for(int i = 0; i < pool->entry_count; ++i) {
			radv_descriptor_set_destroy(device, pool, pool->entries[i].set, false);
		}
	}

	if (pool->bo)
		device->ws->buffer_destroy(pool->bo);

	vk_object_base_finish(&pool->base);
	vk_free2(&device->vk.alloc, pAllocator, pool);
}

VkResult radv_CreateDescriptorPool(
	VkDevice                                    _device,
	const VkDescriptorPoolCreateInfo*           pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkDescriptorPool*                           pDescriptorPool)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_descriptor_pool *pool;
	uint64_t size = sizeof(struct radv_descriptor_pool);
	uint64_t bo_size = 0, bo_count = 0, range_count = 0;

	vk_foreach_struct(ext, pCreateInfo->pNext) {
		switch (ext->sType) {
		case VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_INLINE_UNIFORM_BLOCK_CREATE_INFO_EXT: {
			const struct VkDescriptorPoolInlineUniformBlockCreateInfoEXT *info =
				(const struct VkDescriptorPoolInlineUniformBlockCreateInfoEXT*)ext;
			/* the sizes are 4 aligned, and we need to align to at
			 * most 32, which needs at most 28 bytes extra per
			 * binding. */
			bo_size += 28llu * info->maxInlineUniformBlockBindings;
			break;
		}
		default:
			break;
		}
	}

	for (unsigned i = 0; i < pCreateInfo->poolSizeCount; ++i) {
		if (pCreateInfo->pPoolSizes[i].type != VK_DESCRIPTOR_TYPE_SAMPLER)
			bo_count += pCreateInfo->pPoolSizes[i].descriptorCount;

		switch(pCreateInfo->pPoolSizes[i].type) {
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			range_count += pCreateInfo->pPoolSizes[i].descriptorCount;
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
		case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT:
			bo_size += pCreateInfo->pPoolSizes[i].descriptorCount;
			break;
		default:
			break;
		}
	}

	if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
		uint64_t host_size = pCreateInfo->maxSets * sizeof(struct radv_descriptor_set);
		host_size += sizeof(struct radeon_winsys_bo*) * bo_count;
		host_size += sizeof(struct radv_descriptor_range) * range_count;
		size += host_size;
	} else {
		size += sizeof(struct radv_descriptor_pool_entry) * pCreateInfo->maxSets;
	}

	pool = vk_alloc2(&device->vk.alloc, pAllocator, size, 8,
	                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!pool)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(pool, 0, sizeof(*pool));

	vk_object_base_init(&device->vk, &pool->base,
			    VK_OBJECT_TYPE_DESCRIPTOR_POOL);

	if (!(pCreateInfo->flags & VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT)) {
		pool->host_memory_base = (uint8_t*)pool + sizeof(struct radv_descriptor_pool);
		pool->host_memory_ptr = pool->host_memory_base;
		pool->host_memory_end = (uint8_t*)pool + size;
	}

	if (bo_size) {
		pool->bo = device->ws->buffer_create(device->ws, bo_size, 32,
						     RADEON_DOMAIN_VRAM,
						     RADEON_FLAG_NO_INTERPROCESS_SHARING |
						     RADEON_FLAG_READ_ONLY |
						     RADEON_FLAG_32BIT,
						     RADV_BO_PRIORITY_DESCRIPTOR);
		if (!pool->bo) {
			radv_destroy_descriptor_pool(device, pAllocator, pool);
			return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
		}
		pool->mapped_ptr = (uint8_t*)device->ws->buffer_map(pool->bo);
		if (!pool->mapped_ptr) {
			radv_destroy_descriptor_pool(device, pAllocator, pool);
			return vk_error(device->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);
		}
	}
	pool->size = bo_size;
	pool->max_entry_count = pCreateInfo->maxSets;

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

	radv_destroy_descriptor_pool(device, pAllocator, pool);
}

VkResult radv_ResetDescriptorPool(
	VkDevice                                    _device,
	VkDescriptorPool                            descriptorPool,
	VkDescriptorPoolResetFlags                  flags)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_pool, pool, descriptorPool);

	if (!pool->host_memory_base) {
		for(int i = 0; i < pool->entry_count; ++i) {
			radv_descriptor_set_destroy(device, pool, pool->entries[i].set, false);
		}
		pool->entry_count = 0;
	}

	pool->current_offset = 0;
	pool->host_memory_ptr = pool->host_memory_base;

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
	struct radv_descriptor_set *set = NULL;

	const VkDescriptorSetVariableDescriptorCountAllocateInfo *variable_counts =
		vk_find_struct_const(pAllocateInfo->pNext, DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO);
	const uint32_t zero = 0;

	/* allocate a set of buffers for each shader to contain descriptors */
	for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
		RADV_FROM_HANDLE(radv_descriptor_set_layout, layout,
				 pAllocateInfo->pSetLayouts[i]);

		const uint32_t *variable_count = NULL;
		if (variable_counts) {
			if (i < variable_counts->descriptorSetCount)
				variable_count = variable_counts->pDescriptorCounts + i;
			else
				variable_count = &zero;
		}

		assert(!(layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));

		result = radv_descriptor_set_create(device, pool, layout, variable_count, &set);
		if (result != VK_SUCCESS)
			break;

		pDescriptorSets[i] = radv_descriptor_set_to_handle(set);
	}

	if (result != VK_SUCCESS) {
		radv_FreeDescriptorSets(_device, pAllocateInfo->descriptorPool,
					i, pDescriptorSets);
		for (i = 0; i < pAllocateInfo->descriptorSetCount; i++) {
			pDescriptorSets[i] = VK_NULL_HANDLE;
		}
	}
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

		if (set && !pool->host_memory_base)
			radv_descriptor_set_destroy(device, pool, set, true);
	}
	return VK_SUCCESS;
}

static void write_texel_buffer_descriptor(struct radv_device *device,
					  struct radv_cmd_buffer *cmd_buffer,
					  unsigned *dst,
					  struct radeon_winsys_bo **buffer_list,
					  const VkBufferView _buffer_view)
{
	RADV_FROM_HANDLE(radv_buffer_view, buffer_view, _buffer_view);

	if (!buffer_view) {
		memset(dst, 0, 4 * 4);
		return;
	}

	memcpy(dst, buffer_view->state, 4 * 4);

	if (cmd_buffer)
		radv_cs_add_buffer(device->ws, cmd_buffer->cs, buffer_view->bo);
	else
		*buffer_list = buffer_view->bo;
}

static void write_buffer_descriptor(struct radv_device *device,
                                    struct radv_cmd_buffer *cmd_buffer,
                                    unsigned *dst,
                                    struct radeon_winsys_bo **buffer_list,
                                    const VkDescriptorBufferInfo *buffer_info)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, buffer_info->buffer);

	if (!buffer) {
		memset(dst, 0, 4 * 4);
		return;
	}

	uint64_t va = radv_buffer_get_va(buffer->bo);
	uint32_t range = buffer_info->range;

	if (buffer_info->range == VK_WHOLE_SIZE)
		range = buffer->size - buffer_info->offset;

	/* robustBufferAccess is relaxed enough to allow this (in combination
	 * with the alignment/size we return from vkGetBufferMemoryRequirements)
	 * and this allows the shader compiler to create more efficient 8/16-bit
	 * buffer accesses. */
	range = align(range, 4);

	va += buffer_info->offset + buffer->offset;

	uint32_t rsrc_word3 = S_008F0C_DST_SEL_X(V_008F0C_SQ_SEL_X) |
			      S_008F0C_DST_SEL_Y(V_008F0C_SQ_SEL_Y) |
			      S_008F0C_DST_SEL_Z(V_008F0C_SQ_SEL_Z) |
			      S_008F0C_DST_SEL_W(V_008F0C_SQ_SEL_W);

	if (device->physical_device->rad_info.chip_class >= GFX10) {
		rsrc_word3 |= S_008F0C_FORMAT(V_008F0C_IMG_FORMAT_32_FLOAT) |
			      S_008F0C_OOB_SELECT(V_008F0C_OOB_SELECT_RAW) |
			      S_008F0C_RESOURCE_LEVEL(1);
	} else {
		rsrc_word3 |= S_008F0C_NUM_FORMAT(V_008F0C_BUF_NUM_FORMAT_FLOAT) |
			      S_008F0C_DATA_FORMAT(V_008F0C_BUF_DATA_FORMAT_32);
	}

	dst[0] = va;
	dst[1] = S_008F04_BASE_ADDRESS_HI(va >> 32);
	dst[2] = range;
	dst[3] = rsrc_word3;

	if (cmd_buffer)
		radv_cs_add_buffer(device->ws, cmd_buffer->cs, buffer->bo);
	else
		*buffer_list = buffer->bo;
}

static void write_block_descriptor(struct radv_device *device,
                                   struct radv_cmd_buffer *cmd_buffer,
                                   void *dst,
                                   const VkWriteDescriptorSet *writeset)
{
	const VkWriteDescriptorSetInlineUniformBlockEXT *inline_ub =
		vk_find_struct_const(writeset->pNext, WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK_EXT);

	memcpy(dst, inline_ub->pData, inline_ub->dataSize);
}

static void write_dynamic_buffer_descriptor(struct radv_device *device,
                                            struct radv_descriptor_range *range,
                                            struct radeon_winsys_bo **buffer_list,
                                            const VkDescriptorBufferInfo *buffer_info)
{
	RADV_FROM_HANDLE(radv_buffer, buffer, buffer_info->buffer);
	uint64_t va;
	unsigned size;

	if (!buffer) {
		range->va = 0;
		return;
	}

	va = radv_buffer_get_va(buffer->bo);
	size = buffer_info->range;

	if (buffer_info->range == VK_WHOLE_SIZE)
		size = buffer->size - buffer_info->offset;

	/* robustBufferAccess is relaxed enough to allow this (in combination
	 * with the alignment/size we return from vkGetBufferMemoryRequirements)
	 * and this allows the shader compiler to create more efficient 8/16-bit
	 * buffer accesses. */
	size = align(size, 4);

	va += buffer_info->offset + buffer->offset;
	range->va = va;
	range->size = size;

	*buffer_list = buffer->bo;
}

static void
write_image_descriptor(struct radv_device *device,
		       struct radv_cmd_buffer *cmd_buffer,
		       unsigned size, unsigned *dst,
		       struct radeon_winsys_bo **buffer_list,
		       VkDescriptorType descriptor_type,
		       const VkDescriptorImageInfo *image_info)
{
	RADV_FROM_HANDLE(radv_image_view, iview, image_info->imageView);
	union radv_descriptor *descriptor;

	if (!iview) {
		memset(dst, 0, size);
		return;
	}

	if (descriptor_type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
		descriptor = &iview->storage_descriptor;
	} else {
		descriptor = &iview->descriptor;
	}

	memcpy(dst, descriptor, size);

	if (cmd_buffer)
		radv_cs_add_buffer(device->ws, cmd_buffer->cs, iview->bo);
	else
		*buffer_list = iview->bo;
}

static void
write_combined_image_sampler_descriptor(struct radv_device *device,
					struct radv_cmd_buffer *cmd_buffer,
					unsigned sampler_offset,
					unsigned *dst,
					struct radeon_winsys_bo **buffer_list,
					VkDescriptorType descriptor_type,
					const VkDescriptorImageInfo *image_info,
					bool has_sampler)
{
	RADV_FROM_HANDLE(radv_sampler, sampler, image_info->sampler);

	write_image_descriptor(device, cmd_buffer, sampler_offset, dst, buffer_list,
	                       descriptor_type, image_info);
	/* copy over sampler state */
	if (has_sampler) {
		memcpy(dst + sampler_offset / sizeof(*dst), sampler->state, 16);
	}
}

static void
write_sampler_descriptor(struct radv_device *device,
					unsigned *dst,
					const VkDescriptorImageInfo *image_info)
{
	RADV_FROM_HANDLE(radv_sampler, sampler, image_info->sampler);

	memcpy(dst, sampler->state, 16);
}

void radv_update_descriptor_sets(
	struct radv_device*                         device,
	struct radv_cmd_buffer*                     cmd_buffer,
	VkDescriptorSet                             dstSetOverride,
	uint32_t                                    descriptorWriteCount,
	const VkWriteDescriptorSet*                 pDescriptorWrites,
	uint32_t                                    descriptorCopyCount,
	const VkCopyDescriptorSet*                  pDescriptorCopies)
{
	uint32_t i, j;
	for (i = 0; i < descriptorWriteCount; i++) {
		const VkWriteDescriptorSet *writeset = &pDescriptorWrites[i];
		RADV_FROM_HANDLE(radv_descriptor_set, set,
		                 dstSetOverride ? dstSetOverride : writeset->dstSet);
		const struct radv_descriptor_set_binding_layout *binding_layout =
			set->layout->binding + writeset->dstBinding;
		uint32_t *ptr = set->mapped_ptr;
		struct radeon_winsys_bo **buffer_list =  set->descriptors;
		/* Immutable samplers are not copied into push descriptors when they are
		 * allocated, so if we are writing push descriptors we have to copy the
		 * immutable samplers into them now.
		 */
		const bool copy_immutable_samplers = cmd_buffer &&
			binding_layout->immutable_samplers_offset && !binding_layout->immutable_samplers_equal;
		const uint32_t *samplers = radv_immutable_samplers(set->layout, binding_layout);

		ptr += binding_layout->offset / 4;

		if (writeset->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
			write_block_descriptor(device, cmd_buffer, (uint8_t*)ptr + writeset->dstArrayElement, writeset);
			continue;
		}

		ptr += binding_layout->size * writeset->dstArrayElement / 4;
		buffer_list += binding_layout->buffer_offset;
		buffer_list += writeset->dstArrayElement;
		for (j = 0; j < writeset->descriptorCount; ++j) {
			switch(writeset->descriptorType) {
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
				unsigned idx = writeset->dstArrayElement + j;
				idx += binding_layout->dynamic_offset_offset;
				assert(!(set->layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
				write_dynamic_buffer_descriptor(device, set->dynamic_descriptors + idx,
								buffer_list, writeset->pBufferInfo + j);
				break;
			}
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				write_buffer_descriptor(device, cmd_buffer, ptr, buffer_list,
							writeset->pBufferInfo + j);
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				write_texel_buffer_descriptor(device, cmd_buffer, ptr, buffer_list,
							      writeset->pTexelBufferView[j]);
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
				write_image_descriptor(device, cmd_buffer, 64, ptr, buffer_list,
						       writeset->descriptorType,
						       writeset->pImageInfo + j);
				break;
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: {
				unsigned sampler_offset = radv_combined_image_descriptor_sampler_offset(binding_layout);
				write_combined_image_sampler_descriptor(device, cmd_buffer, sampler_offset,
									ptr, buffer_list,
									writeset->descriptorType,
									writeset->pImageInfo + j,
									!binding_layout->immutable_samplers_offset);
				if (copy_immutable_samplers) {
					const unsigned idx = writeset->dstArrayElement + j;
					memcpy((char*)ptr + sampler_offset, samplers + 4 * idx, 16);
				}
				break;
			}
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				if (!binding_layout->immutable_samplers_offset) {
					write_sampler_descriptor(device, ptr,
					                         writeset->pImageInfo + j);
				} else if (copy_immutable_samplers) {
					unsigned idx = writeset->dstArrayElement + j;
					memcpy(ptr, samplers + 4 * idx, 16);
				}
				break;
			default:
				break;
			}
			ptr += binding_layout->size / 4;
			++buffer_list;
		}

	}

	for (i = 0; i < descriptorCopyCount; i++) {
		const VkCopyDescriptorSet *copyset = &pDescriptorCopies[i];
		RADV_FROM_HANDLE(radv_descriptor_set, src_set,
		                 copyset->srcSet);
		RADV_FROM_HANDLE(radv_descriptor_set, dst_set,
		                 copyset->dstSet);
		const struct radv_descriptor_set_binding_layout *src_binding_layout =
			src_set->layout->binding + copyset->srcBinding;
		const struct radv_descriptor_set_binding_layout *dst_binding_layout =
			dst_set->layout->binding + copyset->dstBinding;
		uint32_t *src_ptr = src_set->mapped_ptr;
		uint32_t *dst_ptr = dst_set->mapped_ptr;
		struct radeon_winsys_bo **src_buffer_list = src_set->descriptors;
		struct radeon_winsys_bo **dst_buffer_list = dst_set->descriptors;

		src_ptr += src_binding_layout->offset / 4;
		dst_ptr += dst_binding_layout->offset / 4;

		if (src_binding_layout->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
			src_ptr += copyset->srcArrayElement / 4;
			dst_ptr += copyset->dstArrayElement / 4;

			memcpy(dst_ptr, src_ptr, copyset->descriptorCount);
			continue;
		}

		src_ptr += src_binding_layout->size * copyset->srcArrayElement / 4;
		dst_ptr += dst_binding_layout->size * copyset->dstArrayElement / 4;

		src_buffer_list += src_binding_layout->buffer_offset;
		src_buffer_list += copyset->srcArrayElement;

		dst_buffer_list += dst_binding_layout->buffer_offset;
		dst_buffer_list += copyset->dstArrayElement;

		for (j = 0; j < copyset->descriptorCount; ++j) {
			switch (src_binding_layout->type) {
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
				unsigned src_idx = copyset->srcArrayElement + j;
				unsigned dst_idx = copyset->dstArrayElement + j;
				struct radv_descriptor_range *src_range, *dst_range;
				src_idx += src_binding_layout->dynamic_offset_offset;
				dst_idx += dst_binding_layout->dynamic_offset_offset;

				src_range = src_set->dynamic_descriptors + src_idx;
				dst_range = dst_set->dynamic_descriptors + dst_idx;
				*dst_range = *src_range;
				break;
			}
			default:
				memcpy(dst_ptr, src_ptr, src_binding_layout->size);
			}
			src_ptr += src_binding_layout->size / 4;
			dst_ptr += dst_binding_layout->size / 4;

			if (src_binding_layout->type != VK_DESCRIPTOR_TYPE_SAMPLER) {
				/* Sampler descriptors don't have a buffer list. */
				dst_buffer_list[j] = src_buffer_list[j];
			}
		}
	}
}

void radv_UpdateDescriptorSets(
	VkDevice                                    _device,
	uint32_t                                    descriptorWriteCount,
	const VkWriteDescriptorSet*                 pDescriptorWrites,
	uint32_t                                    descriptorCopyCount,
	const VkCopyDescriptorSet*                  pDescriptorCopies)
{
	RADV_FROM_HANDLE(radv_device, device, _device);

	radv_update_descriptor_sets(device, NULL, VK_NULL_HANDLE, descriptorWriteCount, pDescriptorWrites,
			            descriptorCopyCount, pDescriptorCopies);
}

VkResult radv_CreateDescriptorUpdateTemplate(VkDevice _device,
                                             const VkDescriptorUpdateTemplateCreateInfo *pCreateInfo,
                                             const VkAllocationCallbacks *pAllocator,
                                             VkDescriptorUpdateTemplate *pDescriptorUpdateTemplate)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_set_layout, set_layout, pCreateInfo->descriptorSetLayout);
	const uint32_t entry_count = pCreateInfo->descriptorUpdateEntryCount;
	const size_t size = sizeof(struct radv_descriptor_update_template) +
		sizeof(struct radv_descriptor_update_template_entry) * entry_count;
	struct radv_descriptor_update_template *templ;
	uint32_t i;

	templ = vk_alloc2(&device->vk.alloc, pAllocator, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (!templ)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	vk_object_base_init(&device->vk, &templ->base,
			    VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE);

	templ->entry_count = entry_count;

	if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR) {
		RADV_FROM_HANDLE(radv_pipeline_layout, pipeline_layout, pCreateInfo->pipelineLayout);

		/* descriptorSetLayout should be ignored for push descriptors
		 * and instead it refers to pipelineLayout and set.
		 */
		assert(pCreateInfo->set < MAX_SETS);
		set_layout = pipeline_layout->set[pCreateInfo->set].layout;

		templ->bind_point = pCreateInfo->pipelineBindPoint;
	}

	for (i = 0; i < entry_count; i++) {
		const VkDescriptorUpdateTemplateEntry *entry = &pCreateInfo->pDescriptorUpdateEntries[i];
		const struct radv_descriptor_set_binding_layout *binding_layout =
			set_layout->binding + entry->dstBinding;
		const uint32_t buffer_offset = binding_layout->buffer_offset + entry->dstArrayElement;
		const uint32_t *immutable_samplers = NULL;
		uint32_t dst_offset;
		uint32_t dst_stride;

		/* dst_offset is an offset into dynamic_descriptors when the descriptor
		   is dynamic, and an offset into mapped_ptr otherwise */
		switch (entry->descriptorType) {
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			assert(pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET);
			dst_offset = binding_layout->dynamic_offset_offset + entry->dstArrayElement;
			dst_stride = 0; /* Not used */
			break;
		default:
			switch (entry->descriptorType) {
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				/* Immutable samplers are copied into push descriptors when they are pushed */
				if (pCreateInfo->templateType == VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR &&
				    binding_layout->immutable_samplers_offset && !binding_layout->immutable_samplers_equal) {
					immutable_samplers = radv_immutable_samplers(set_layout, binding_layout) + entry->dstArrayElement * 4;
				}
				break;
			default:
				break;
			}
			dst_offset = binding_layout->offset / 4;
			if (entry->descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT)
				dst_offset += entry->dstArrayElement / 4;
			else
				dst_offset += binding_layout->size * entry->dstArrayElement / 4;

			dst_stride = binding_layout->size / 4;
			break;
		}

		templ->entry[i] = (struct radv_descriptor_update_template_entry) {
			.descriptor_type = entry->descriptorType,
			.descriptor_count = entry->descriptorCount,
			.src_offset = entry->offset,
			.src_stride = entry->stride,
			.dst_offset = dst_offset,
			.dst_stride = dst_stride,
			.buffer_offset = buffer_offset,
			.has_sampler = !binding_layout->immutable_samplers_offset,
			.sampler_offset = radv_combined_image_descriptor_sampler_offset(binding_layout),
			.immutable_samplers = immutable_samplers
		};
	}

	*pDescriptorUpdateTemplate = radv_descriptor_update_template_to_handle(templ);
	return VK_SUCCESS;
}

void radv_DestroyDescriptorUpdateTemplate(VkDevice _device,
                                          VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                          const VkAllocationCallbacks *pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_update_template, templ, descriptorUpdateTemplate);

	if (!templ)
		return;

	vk_object_base_finish(&templ->base);
	vk_free2(&device->vk.alloc, pAllocator, templ);
}

void radv_update_descriptor_set_with_template(struct radv_device *device,
                                              struct radv_cmd_buffer *cmd_buffer,
                                              struct radv_descriptor_set *set,
                                              VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                              const void *pData)
{
	RADV_FROM_HANDLE(radv_descriptor_update_template, templ, descriptorUpdateTemplate);
	uint32_t i;

	for (i = 0; i < templ->entry_count; ++i) {
		struct radeon_winsys_bo **buffer_list = set->descriptors + templ->entry[i].buffer_offset;
		uint32_t *pDst = set->mapped_ptr + templ->entry[i].dst_offset;
		const uint8_t *pSrc = ((const uint8_t *) pData) + templ->entry[i].src_offset;
		uint32_t j;

		if (templ->entry[i].descriptor_type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK_EXT) {
			memcpy((uint8_t*)pDst, pSrc, templ->entry[i].descriptor_count);
			continue;
		}

		for (j = 0; j < templ->entry[i].descriptor_count; ++j) {
			switch (templ->entry[i].descriptor_type) {
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
				const unsigned idx = templ->entry[i].dst_offset + j;
				assert(!(set->layout->flags & VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR));
				write_dynamic_buffer_descriptor(device, set->dynamic_descriptors + idx,
								buffer_list, (struct VkDescriptorBufferInfo *) pSrc);
				break;
			}
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
				write_buffer_descriptor(device, cmd_buffer, pDst, buffer_list,
				                        (struct VkDescriptorBufferInfo *) pSrc);
				break;
			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				write_texel_buffer_descriptor(device, cmd_buffer, pDst, buffer_list,
						              *(VkBufferView *) pSrc);
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
				write_image_descriptor(device, cmd_buffer, 64, pDst, buffer_list,
						       templ->entry[i].descriptor_type,
					               (struct VkDescriptorImageInfo *) pSrc);
				break;
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				write_combined_image_sampler_descriptor(device, cmd_buffer, templ->entry[i].sampler_offset,
									pDst, buffer_list, templ->entry[i].descriptor_type,
									(struct VkDescriptorImageInfo *) pSrc,
									templ->entry[i].has_sampler);
				if (templ->entry[i].immutable_samplers) {
					memcpy((char*)pDst + templ->entry[i].sampler_offset, templ->entry[i].immutable_samplers + 4 * j, 16);
				}
				break;
			case VK_DESCRIPTOR_TYPE_SAMPLER:
				if (templ->entry[i].has_sampler)
					write_sampler_descriptor(device, pDst,
					                         (struct VkDescriptorImageInfo *) pSrc);
				else if (templ->entry[i].immutable_samplers)
					memcpy(pDst, templ->entry[i].immutable_samplers + 4 * j, 16);
				break;
			default:
				break;
			}
		        pSrc += templ->entry[i].src_stride;
			pDst += templ->entry[i].dst_stride;
			++buffer_list;
		}
	}
}

void radv_UpdateDescriptorSetWithTemplate(VkDevice _device,
                                          VkDescriptorSet descriptorSet,
                                          VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                          const void *pData)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_descriptor_set, set, descriptorSet);

	radv_update_descriptor_set_with_template(device, NULL, set, descriptorUpdateTemplate, pData);
}


VkResult radv_CreateSamplerYcbcrConversion(VkDevice _device,
					   const VkSamplerYcbcrConversionCreateInfo* pCreateInfo,
					   const VkAllocationCallbacks* pAllocator,
					   VkSamplerYcbcrConversion* pYcbcrConversion)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_sampler_ycbcr_conversion *conversion = NULL;

	conversion = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*conversion), 8,
	                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

	if (conversion == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	vk_object_base_init(&device->vk, &conversion->base,
			    VK_OBJECT_TYPE_SAMPLER_YCBCR_CONVERSION);

	conversion->format = pCreateInfo->format;
	conversion->ycbcr_model = pCreateInfo->ycbcrModel;
	conversion->ycbcr_range = pCreateInfo->ycbcrRange;
	conversion->components = pCreateInfo->components;
	conversion->chroma_offsets[0] = pCreateInfo->xChromaOffset;
	conversion->chroma_offsets[1] = pCreateInfo->yChromaOffset;
	conversion->chroma_filter = pCreateInfo->chromaFilter;

	*pYcbcrConversion = radv_sampler_ycbcr_conversion_to_handle(conversion);
	return VK_SUCCESS;
}


void radv_DestroySamplerYcbcrConversion(VkDevice _device,
					VkSamplerYcbcrConversion ycbcrConversion,
					const VkAllocationCallbacks* pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_sampler_ycbcr_conversion, ycbcr_conversion, ycbcrConversion);

	if (!ycbcr_conversion)
		return;

	vk_object_base_finish(&ycbcr_conversion->base);
	vk_free2(&device->vk.alloc, pAllocator, ycbcr_conversion);
}
