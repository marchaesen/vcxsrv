/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
#include "radv_private.h"

#include "vk_util.h"

static void
radv_render_pass_add_subpass_dep(struct radv_render_pass *pass,
				 const VkSubpassDependency2KHR *dep)
{
	uint32_t src = dep->srcSubpass;
	uint32_t dst = dep->dstSubpass;

	/* Ignore subpass self-dependencies as they allow the app to call
	 * vkCmdPipelineBarrier() inside the render pass and the driver should
	 * only do the barrier when called, not when starting the render pass.
	 */
	if (src == dst)
		return;

	/* Accumulate all ingoing external dependencies to the first subpass. */
	if (src == VK_SUBPASS_EXTERNAL)
		dst = 0;

	if (dst == VK_SUBPASS_EXTERNAL) {
		if (dep->dstStageMask != VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
			pass->end_barrier.src_stage_mask |= dep->srcStageMask;
		pass->end_barrier.src_access_mask |= dep->srcAccessMask;
		pass->end_barrier.dst_access_mask |= dep->dstAccessMask;
	} else {
		if (dep->dstStageMask != VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT)
			pass->subpasses[dst].start_barrier.src_stage_mask |= dep->srcStageMask;
		pass->subpasses[dst].start_barrier.src_access_mask |= dep->srcAccessMask;
		pass->subpasses[dst].start_barrier.dst_access_mask |= dep->dstAccessMask;
	}
}

static void
radv_render_pass_compile(struct radv_render_pass *pass)
{
	for (uint32_t i = 0; i < pass->subpass_count; i++) {
		struct radv_subpass *subpass = &pass->subpasses[i];
		uint32_t color_sample_count = 1, depth_sample_count = 1;

		/* We don't allow depth_stencil_attachment to be non-NULL and
		 * be VK_ATTACHMENT_UNUSED.  This way something can just check
		 * for NULL and be guaranteed that they have a valid
		 * attachment.
		 */
		if (subpass->depth_stencil_attachment &&
		    subpass->depth_stencil_attachment->attachment == VK_ATTACHMENT_UNUSED)
			subpass->depth_stencil_attachment = NULL;

		for (uint32_t j = 0; j < subpass->attachment_count; j++) {
			struct radv_subpass_attachment *subpass_att =
				&subpass->attachments[j];
			if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
				continue;

			struct radv_render_pass_attachment *pass_att =
				&pass->attachments[subpass_att->attachment];

			pass_att->last_subpass_idx = i;
		}

		subpass->has_color_att = false;
		for (uint32_t j = 0; j < subpass->color_count; j++) {
			struct radv_subpass_attachment *subpass_att =
				&subpass->color_attachments[j];
			if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
				continue;

			subpass->has_color_att = true;

			struct radv_render_pass_attachment *pass_att =
				&pass->attachments[subpass_att->attachment];

			color_sample_count = pass_att->samples;
		}

		if (subpass->depth_stencil_attachment) {
			const uint32_t a =
				subpass->depth_stencil_attachment->attachment;
			struct radv_render_pass_attachment *pass_att =
				&pass->attachments[a];
			depth_sample_count = pass_att->samples;
		}

		subpass->max_sample_count = MAX2(color_sample_count,
						 depth_sample_count);

		/* We have to handle resolve attachments specially */
		subpass->has_resolve = false;
		if (subpass->resolve_attachments) {
			for (uint32_t j = 0; j < subpass->color_count; j++) {
				struct radv_subpass_attachment *resolve_att =
					&subpass->resolve_attachments[j];

				if (resolve_att->attachment == VK_ATTACHMENT_UNUSED)
					continue;

				subpass->has_resolve = true;
			}
		}
	}
}

static unsigned
radv_num_subpass_attachments(const VkSubpassDescription *desc)
{
	return desc->inputAttachmentCount +
	       desc->colorAttachmentCount +
	       (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
	       (desc->pDepthStencilAttachment != NULL);
}

VkResult radv_CreateRenderPass(
	VkDevice                                    _device,
	const VkRenderPassCreateInfo*               pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkRenderPass*                               pRenderPass)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_render_pass *pass;
	size_t size;
	size_t attachments_offset;
	VkRenderPassMultiviewCreateInfo *multiview_info = NULL;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);

	size = sizeof(*pass);
	size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
	attachments_offset = size;
	size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

	pass = vk_alloc2(&device->alloc, pAllocator, size, 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pass == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(pass, 0, size);
	pass->attachment_count = pCreateInfo->attachmentCount;
	pass->subpass_count = pCreateInfo->subpassCount;
	pass->attachments = (void *) pass + attachments_offset;

	vk_foreach_struct(ext, pCreateInfo->pNext) {
		switch(ext->sType) {
		case  VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO:
			multiview_info = (VkRenderPassMultiviewCreateInfo*)ext;
			break;
		default:
			break;
		}
	}

	for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
		struct radv_render_pass_attachment *att = &pass->attachments[i];

		att->format = pCreateInfo->pAttachments[i].format;
		att->samples = pCreateInfo->pAttachments[i].samples;
		att->load_op = pCreateInfo->pAttachments[i].loadOp;
		att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
		att->initial_layout =  pCreateInfo->pAttachments[i].initialLayout;
		att->final_layout =  pCreateInfo->pAttachments[i].finalLayout;
		// att->store_op = pCreateInfo->pAttachments[i].storeOp;
		// att->stencil_store_op = pCreateInfo->pAttachments[i].stencilStoreOp;
	}
	uint32_t subpass_attachment_count = 0;
	struct radv_subpass_attachment *p;
	for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
		subpass_attachment_count +=
			radv_num_subpass_attachments(&pCreateInfo->pSubpasses[i]);
	}

	if (subpass_attachment_count) {
		pass->subpass_attachments =
			vk_alloc2(&device->alloc, pAllocator,
				    subpass_attachment_count * sizeof(struct radv_subpass_attachment), 8,
				    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (pass->subpass_attachments == NULL) {
			vk_free2(&device->alloc, pAllocator, pass);
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
		}
	} else
		pass->subpass_attachments = NULL;

	p = pass->subpass_attachments;
	for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
		const VkSubpassDescription *desc = &pCreateInfo->pSubpasses[i];
		struct radv_subpass *subpass = &pass->subpasses[i];

		subpass->input_count = desc->inputAttachmentCount;
		subpass->color_count = desc->colorAttachmentCount;
		subpass->attachment_count = radv_num_subpass_attachments(desc);
		subpass->attachments = p;

		if (multiview_info)
			subpass->view_mask = multiview_info->pViewMasks[i];

		if (desc->inputAttachmentCount > 0) {
			subpass->input_attachments = p;
			p += desc->inputAttachmentCount;

			for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
				subpass->input_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pInputAttachments[j].attachment,
					.layout = desc->pInputAttachments[j].layout,
				};
			}
		}

		if (desc->colorAttachmentCount > 0) {
			subpass->color_attachments = p;
			p += desc->colorAttachmentCount;

			for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
				subpass->color_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pColorAttachments[j].attachment,
					.layout = desc->pColorAttachments[j].layout,
				};
			}
		}

		if (desc->pResolveAttachments) {
			subpass->resolve_attachments = p;
			p += desc->colorAttachmentCount;

			for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
				uint32_t a = desc->pResolveAttachments[j].attachment;
				subpass->resolve_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pResolveAttachments[j].attachment,
					.layout = desc->pResolveAttachments[j].layout,
				};
			}
		}

		if (desc->pDepthStencilAttachment) {
			subpass->depth_stencil_attachment = p++;

			*subpass->depth_stencil_attachment = (struct radv_subpass_attachment) {
				.attachment = desc->pDepthStencilAttachment->attachment,
				.layout = desc->pDepthStencilAttachment->layout,
			};
		}
	}

	for (unsigned i = 0; i < pCreateInfo->dependencyCount; ++i) {
		/* Convert to a Dependency2KHR */
		struct VkSubpassDependency2KHR dep2 = {
			.srcSubpass       = pCreateInfo->pDependencies[i].srcSubpass,
			.dstSubpass       = pCreateInfo->pDependencies[i].dstSubpass,
			.srcStageMask     = pCreateInfo->pDependencies[i].srcStageMask,
			.dstStageMask     = pCreateInfo->pDependencies[i].dstStageMask,
			.srcAccessMask    = pCreateInfo->pDependencies[i].srcAccessMask,
			.dstAccessMask    = pCreateInfo->pDependencies[i].dstAccessMask,
			.dependencyFlags  = pCreateInfo->pDependencies[i].dependencyFlags,
		};
		radv_render_pass_add_subpass_dep(pass, &dep2);
	}

	radv_render_pass_compile(pass);

	*pRenderPass = radv_render_pass_to_handle(pass);

	return VK_SUCCESS;
}

static unsigned
radv_num_subpass_attachments2(const VkSubpassDescription2KHR *desc)
{
	return desc->inputAttachmentCount +
	       desc->colorAttachmentCount +
	       (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
	       (desc->pDepthStencilAttachment != NULL);
}

VkResult radv_CreateRenderPass2KHR(
    VkDevice                                    _device,
    const VkRenderPassCreateInfo2KHR*           pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_render_pass *pass;
	size_t size;
	size_t attachments_offset;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2_KHR);

	size = sizeof(*pass);
	size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
	attachments_offset = size;
	size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

	pass = vk_alloc2(&device->alloc, pAllocator, size, 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pass == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(pass, 0, size);
	pass->attachment_count = pCreateInfo->attachmentCount;
	pass->subpass_count = pCreateInfo->subpassCount;
	pass->attachments = (void *) pass + attachments_offset;

	for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
		struct radv_render_pass_attachment *att = &pass->attachments[i];

		att->format = pCreateInfo->pAttachments[i].format;
		att->samples = pCreateInfo->pAttachments[i].samples;
		att->load_op = pCreateInfo->pAttachments[i].loadOp;
		att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
		att->initial_layout =  pCreateInfo->pAttachments[i].initialLayout;
		att->final_layout =  pCreateInfo->pAttachments[i].finalLayout;
		// att->store_op = pCreateInfo->pAttachments[i].storeOp;
		// att->stencil_store_op = pCreateInfo->pAttachments[i].stencilStoreOp;
	}
	uint32_t subpass_attachment_count = 0;
	struct radv_subpass_attachment *p;
	for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
		subpass_attachment_count +=
			radv_num_subpass_attachments2(&pCreateInfo->pSubpasses[i]);
	}

	if (subpass_attachment_count) {
		pass->subpass_attachments =
			vk_alloc2(&device->alloc, pAllocator,
				    subpass_attachment_count * sizeof(struct radv_subpass_attachment), 8,
				    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (pass->subpass_attachments == NULL) {
			vk_free2(&device->alloc, pAllocator, pass);
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
		}
	} else
		pass->subpass_attachments = NULL;

	p = pass->subpass_attachments;
	for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
		const VkSubpassDescription2KHR *desc = &pCreateInfo->pSubpasses[i];
		struct radv_subpass *subpass = &pass->subpasses[i];

		subpass->input_count = desc->inputAttachmentCount;
		subpass->color_count = desc->colorAttachmentCount;
		subpass->attachment_count = radv_num_subpass_attachments2(desc);
		subpass->attachments = p;
		subpass->view_mask = desc->viewMask;

		if (desc->inputAttachmentCount > 0) {
			subpass->input_attachments = p;
			p += desc->inputAttachmentCount;

			for (uint32_t j = 0; j < desc->inputAttachmentCount; j++) {
				subpass->input_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pInputAttachments[j].attachment,
					.layout = desc->pInputAttachments[j].layout,
				};
			}
		}

		if (desc->colorAttachmentCount > 0) {
			subpass->color_attachments = p;
			p += desc->colorAttachmentCount;

			for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
				subpass->color_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pColorAttachments[j].attachment,
					.layout = desc->pColorAttachments[j].layout,
				};
			}
		}

		if (desc->pResolveAttachments) {
			subpass->resolve_attachments = p;
			p += desc->colorAttachmentCount;

			for (uint32_t j = 0; j < desc->colorAttachmentCount; j++) {
				uint32_t a = desc->pResolveAttachments[j].attachment;
				subpass->resolve_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pResolveAttachments[j].attachment,
					.layout = desc->pResolveAttachments[j].layout,
				};
			}
		}

		if (desc->pDepthStencilAttachment) {
			subpass->depth_stencil_attachment = p++;

			*subpass->depth_stencil_attachment = (struct radv_subpass_attachment) {
				.attachment = desc->pDepthStencilAttachment->attachment,
				.layout = desc->pDepthStencilAttachment->layout,
			};
		}
	}

	for (unsigned i = 0; i < pCreateInfo->dependencyCount; ++i) {
		radv_render_pass_add_subpass_dep(pass,
						 &pCreateInfo->pDependencies[i]);
	}

	radv_render_pass_compile(pass);

	*pRenderPass = radv_render_pass_to_handle(pass);

	return VK_SUCCESS;
}

void radv_DestroyRenderPass(
	VkDevice                                    _device,
	VkRenderPass                                _pass,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_render_pass, pass, _pass);

	if (!_pass)
		return;
	vk_free2(&device->alloc, pAllocator, pass->subpass_attachments);
	vk_free2(&device->alloc, pAllocator, pass);
}

void radv_GetRenderAreaGranularity(
    VkDevice                                    device,
    VkRenderPass                                renderPass,
    VkExtent2D*                                 pGranularity)
{
	pGranularity->width = 1;
	pGranularity->height = 1;
}

