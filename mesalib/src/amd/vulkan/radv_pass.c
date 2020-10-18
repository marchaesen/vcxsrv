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
				 const VkSubpassDependency2 *dep)
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

static bool
radv_pass_has_layout_transitions(const struct radv_render_pass *pass)
{
	for (unsigned i = 0; i < pass->subpass_count; i++) {
		const struct radv_subpass *subpass = &pass->subpasses[i];
		for (unsigned j = 0; j < subpass->attachment_count; j++) {
			const uint32_t a = subpass->attachments[j].attachment;
			if (a == VK_ATTACHMENT_UNUSED)
				continue;

			uint32_t initial_layout = pass->attachments[a].initial_layout;
			uint32_t stencil_initial_layout = pass->attachments[a].stencil_initial_layout;
			uint32_t final_layout = pass->attachments[a].final_layout;
			uint32_t stencil_final_layout = pass->attachments[a].stencil_final_layout;

			if (subpass->attachments[j].layout != initial_layout ||
			    subpass->attachments[j].layout != stencil_initial_layout ||
			    subpass->attachments[j].layout != final_layout ||
			    subpass->attachments[j].layout != stencil_final_layout)
				return true;
		}
	}

	return false;
}

static void
radv_render_pass_add_implicit_deps(struct radv_render_pass *pass,
				   bool has_ingoing_dep, bool has_outgoing_dep)
{
	/* From the Vulkan 1.0.39 spec:
	*
	*    If there is no subpass dependency from VK_SUBPASS_EXTERNAL to the
	*    first subpass that uses an attachment, then an implicit subpass
	*    dependency exists from VK_SUBPASS_EXTERNAL to the first subpass it is
	*    used in. The implicit subpass dependency only exists if there
	*    exists an automatic layout transition away from initialLayout.
	*    The subpass dependency operates as if defined with the
	*    following parameters:
	*
	*    VkSubpassDependency implicitDependency = {
	*        .srcSubpass = VK_SUBPASS_EXTERNAL;
	*        .dstSubpass = firstSubpass; // First subpass attachment is used in
	*        .srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
	*        .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	*        .srcAccessMask = 0;
	*        .dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
	*                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
	*                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	*                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
	*                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	*        .dependencyFlags = 0;
	*    };
	*
	*    Similarly, if there is no subpass dependency from the last subpass
	*    that uses an attachment to VK_SUBPASS_EXTERNAL, then an implicit
	*    subpass dependency exists from the last subpass it is used in to
	*    VK_SUBPASS_EXTERNAL. The implicit subpass dependency only exists
	*    if there exists an automatic layout transition into finalLayout.
	*    The subpass dependency operates as if defined with the following
	*    parameters:
	*
	*    VkSubpassDependency implicitDependency = {
	*        .srcSubpass = lastSubpass; // Last subpass attachment is used in
	*        .dstSubpass = VK_SUBPASS_EXTERNAL;
	*        .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	*        .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
	*        .srcAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
	*                         VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
	*                         VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
	*                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
	*                         VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	*        .dstAccessMask = 0;
	*        .dependencyFlags = 0;
	*    };
	*/

	/* Implicit subpass dependencies only make sense if automatic layout
	 * transitions are performed.
	 */
	if (!radv_pass_has_layout_transitions(pass))
		return;

	if (!has_ingoing_dep) {
		const VkSubpassDependency2KHR implicit_ingoing_dep = {
			.srcSubpass = VK_SUBPASS_EXTERNAL,
			.dstSubpass = 0,
			.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			.srcAccessMask = 0,
			.dstAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
					 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
					 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
					 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
					 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dependencyFlags = 0,
		};

		radv_render_pass_add_subpass_dep(pass, &implicit_ingoing_dep);
	}

	if (!has_outgoing_dep) {
		const VkSubpassDependency2KHR implicit_outgoing_dep = {
			.srcSubpass = 0,
			.dstSubpass = VK_SUBPASS_EXTERNAL,
			.srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			.srcAccessMask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
					 VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
					 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
					 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
					 VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			.dstAccessMask = 0,
			.dependencyFlags = 0,
		};

		radv_render_pass_add_subpass_dep(pass, &implicit_outgoing_dep);
	}
}

static void
radv_render_pass_compile(struct radv_render_pass *pass)
{
	for (uint32_t i = 0; i < pass->subpass_count; i++) {
		struct radv_subpass *subpass = &pass->subpasses[i];

		for (uint32_t j = 0; j < subpass->attachment_count; j++) {
			struct radv_subpass_attachment *subpass_att =
				&subpass->attachments[j];
			if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
				continue;

			struct radv_render_pass_attachment *pass_att =
				&pass->attachments[subpass_att->attachment];

			pass_att->first_subpass_idx = UINT32_MAX;
		}
	}

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

		if (subpass->ds_resolve_attachment &&
		    subpass->ds_resolve_attachment->attachment == VK_ATTACHMENT_UNUSED)
			subpass->ds_resolve_attachment = NULL;

		for (uint32_t j = 0; j < subpass->attachment_count; j++) {
			struct radv_subpass_attachment *subpass_att =
				&subpass->attachments[j];
			if (subpass_att->attachment == VK_ATTACHMENT_UNUSED)
				continue;

			struct radv_render_pass_attachment *pass_att =
				&pass->attachments[subpass_att->attachment];

			if (i < pass_att->first_subpass_idx)
				pass_att->first_subpass_idx = i;
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
		subpass->color_sample_count = color_sample_count;
		subpass->depth_sample_count = depth_sample_count;

		/* We have to handle resolve attachments specially */
		subpass->has_color_resolve = false;
		if (subpass->resolve_attachments) {
			for (uint32_t j = 0; j < subpass->color_count; j++) {
				struct radv_subpass_attachment *resolve_att =
					&subpass->resolve_attachments[j];

				if (resolve_att->attachment == VK_ATTACHMENT_UNUSED)
					continue;

				subpass->has_color_resolve = true;
			}
		}

		for (uint32_t j = 0; j < subpass->input_count; ++j) {
			if (subpass->input_attachments[j].attachment == VK_ATTACHMENT_UNUSED)
				continue;

			for (uint32_t k = 0; k < subpass->color_count; ++k) {
				if (subpass->color_attachments[k].attachment == subpass->input_attachments[j].attachment) {
					subpass->input_attachments[j].in_render_loop = true;
					subpass->color_attachments[k].in_render_loop = true;
				}
			}

			if (subpass->depth_stencil_attachment &&
			    subpass->depth_stencil_attachment->attachment == subpass->input_attachments[j].attachment) {
				subpass->input_attachments[j].in_render_loop = true;
				subpass->depth_stencil_attachment->in_render_loop = true;
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

static void
radv_destroy_render_pass(struct radv_device *device,
			 const VkAllocationCallbacks *pAllocator,
			 struct radv_render_pass *pass)
{
	vk_object_base_finish(&pass->base);
	vk_free2(&device->vk.alloc, pAllocator, pass->subpass_attachments);
	vk_free2(&device->vk.alloc, pAllocator, pass);
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

	pass = vk_alloc2(&device->vk.alloc, pAllocator, size, 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pass == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(pass, 0, size);

	vk_object_base_init(&device->vk, &pass->base,
			    VK_OBJECT_TYPE_RENDER_PASS);

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
		att->stencil_initial_layout = pCreateInfo->pAttachments[i].initialLayout;
		att->stencil_final_layout = pCreateInfo->pAttachments[i].finalLayout;
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
			vk_alloc2(&device->vk.alloc, pAllocator,
				    subpass_attachment_count * sizeof(struct radv_subpass_attachment), 8,
				    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (pass->subpass_attachments == NULL) {
			radv_destroy_render_pass(device, pAllocator, pass);
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
					.stencil_layout = desc->pInputAttachments[j].layout,
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
				subpass->resolve_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pResolveAttachments[j].attachment,
					.layout = desc->pResolveAttachments[j].layout,
					.stencil_layout = desc->pResolveAttachments[j].layout,
				};
			}
		}

		if (desc->pDepthStencilAttachment) {
			subpass->depth_stencil_attachment = p++;

			*subpass->depth_stencil_attachment = (struct radv_subpass_attachment) {
				.attachment = desc->pDepthStencilAttachment->attachment,
				.layout = desc->pDepthStencilAttachment->layout,
				.stencil_layout = desc->pDepthStencilAttachment->layout,
			};
		}
	}

	bool has_ingoing_dep = false;
	bool has_outgoing_dep = false;

	for (unsigned i = 0; i < pCreateInfo->dependencyCount; ++i) {
		/* Convert to a Dependency2 */
		struct VkSubpassDependency2 dep2 = {
			.srcSubpass       = pCreateInfo->pDependencies[i].srcSubpass,
			.dstSubpass       = pCreateInfo->pDependencies[i].dstSubpass,
			.srcStageMask     = pCreateInfo->pDependencies[i].srcStageMask,
			.dstStageMask     = pCreateInfo->pDependencies[i].dstStageMask,
			.srcAccessMask    = pCreateInfo->pDependencies[i].srcAccessMask,
			.dstAccessMask    = pCreateInfo->pDependencies[i].dstAccessMask,
			.dependencyFlags  = pCreateInfo->pDependencies[i].dependencyFlags,
		};
		radv_render_pass_add_subpass_dep(pass, &dep2);

		/* Determine if the subpass has explicit dependencies from/to
		 * VK_SUBPASS_EXTERNAL.
		 */
		if (pCreateInfo->pDependencies[i].srcSubpass == VK_SUBPASS_EXTERNAL)
			has_ingoing_dep = true;
		if (pCreateInfo->pDependencies[i].dstSubpass == VK_SUBPASS_EXTERNAL)
			has_outgoing_dep = true;
	}

	radv_render_pass_add_implicit_deps(pass,
					   has_ingoing_dep, has_outgoing_dep);

	radv_render_pass_compile(pass);

	*pRenderPass = radv_render_pass_to_handle(pass);

	return VK_SUCCESS;
}

static unsigned
radv_num_subpass_attachments2(const VkSubpassDescription2 *desc)
{
	const VkSubpassDescriptionDepthStencilResolve *ds_resolve =
		vk_find_struct_const(desc->pNext,
				     SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE);

	return desc->inputAttachmentCount +
	       desc->colorAttachmentCount +
	       (desc->pResolveAttachments ? desc->colorAttachmentCount : 0) +
	       (desc->pDepthStencilAttachment != NULL) +
	       (ds_resolve && ds_resolve->pDepthStencilResolveAttachment);
}

VkResult radv_CreateRenderPass2(
    VkDevice                                    _device,
    const VkRenderPassCreateInfo2*              pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkRenderPass*                               pRenderPass)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_render_pass *pass;
	size_t size;
	size_t attachments_offset;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2);

	size = sizeof(*pass);
	size += pCreateInfo->subpassCount * sizeof(pass->subpasses[0]);
	attachments_offset = size;
	size += pCreateInfo->attachmentCount * sizeof(pass->attachments[0]);

	pass = vk_alloc2(&device->vk.alloc, pAllocator, size, 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (pass == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	memset(pass, 0, size);

	vk_object_base_init(&device->vk, &pass->base,
			    VK_OBJECT_TYPE_RENDER_PASS);

	pass->attachment_count = pCreateInfo->attachmentCount;
	pass->subpass_count = pCreateInfo->subpassCount;
	pass->attachments = (void *) pass + attachments_offset;

	for (uint32_t i = 0; i < pCreateInfo->attachmentCount; i++) {
		struct radv_render_pass_attachment *att = &pass->attachments[i];
		const VkAttachmentDescriptionStencilLayoutKHR *stencil_layout =
			vk_find_struct_const(pCreateInfo->pAttachments[i].pNext,
					     ATTACHMENT_DESCRIPTION_STENCIL_LAYOUT_KHR);

		att->format = pCreateInfo->pAttachments[i].format;
		att->samples = pCreateInfo->pAttachments[i].samples;
		att->load_op = pCreateInfo->pAttachments[i].loadOp;
		att->stencil_load_op = pCreateInfo->pAttachments[i].stencilLoadOp;
		att->initial_layout =  pCreateInfo->pAttachments[i].initialLayout;
		att->final_layout =  pCreateInfo->pAttachments[i].finalLayout;
		att->stencil_initial_layout = (stencil_layout ?
					       stencil_layout->stencilInitialLayout :
					       pCreateInfo->pAttachments[i].initialLayout);
		att->stencil_final_layout = (stencil_layout ?
					     stencil_layout->stencilFinalLayout :
					     pCreateInfo->pAttachments[i].finalLayout);
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
			vk_alloc2(&device->vk.alloc, pAllocator,
				    subpass_attachment_count * sizeof(struct radv_subpass_attachment), 8,
				    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
		if (pass->subpass_attachments == NULL) {
			radv_destroy_render_pass(device, pAllocator, pass);
			return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
		}
	} else
		pass->subpass_attachments = NULL;

	p = pass->subpass_attachments;
	for (uint32_t i = 0; i < pCreateInfo->subpassCount; i++) {
		const VkSubpassDescription2 *desc = &pCreateInfo->pSubpasses[i];
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
				const VkAttachmentReferenceStencilLayoutKHR *stencil_attachment =
			            vk_find_struct_const(desc->pInputAttachments[j].pNext,
							 ATTACHMENT_REFERENCE_STENCIL_LAYOUT_KHR);

				subpass->input_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pInputAttachments[j].attachment,
					.layout = desc->pInputAttachments[j].layout,
					.stencil_layout = (stencil_attachment ?
							   stencil_attachment->stencilLayout :
							   desc->pInputAttachments[j].layout),
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
				subpass->resolve_attachments[j] = (struct radv_subpass_attachment) {
					.attachment = desc->pResolveAttachments[j].attachment,
					.layout = desc->pResolveAttachments[j].layout,
				};
			}
		}

		if (desc->pDepthStencilAttachment) {
			subpass->depth_stencil_attachment = p++;

			const VkAttachmentReferenceStencilLayoutKHR *stencil_attachment =
		            vk_find_struct_const(desc->pDepthStencilAttachment->pNext,
						 ATTACHMENT_REFERENCE_STENCIL_LAYOUT_KHR);

			*subpass->depth_stencil_attachment = (struct radv_subpass_attachment) {
				.attachment = desc->pDepthStencilAttachment->attachment,
				.layout = desc->pDepthStencilAttachment->layout,
				.stencil_layout = (stencil_attachment ?
						   stencil_attachment->stencilLayout :
						   desc->pDepthStencilAttachment->layout),
			};
		}

		const VkSubpassDescriptionDepthStencilResolve *ds_resolve =
			vk_find_struct_const(desc->pNext,
					     SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE);

		if (ds_resolve && ds_resolve->pDepthStencilResolveAttachment) {
			subpass->ds_resolve_attachment = p++;

			const VkAttachmentReferenceStencilLayoutKHR *stencil_resolve_attachment =
		            vk_find_struct_const(ds_resolve->pDepthStencilResolveAttachment->pNext,
						 ATTACHMENT_REFERENCE_STENCIL_LAYOUT_KHR);

			*subpass->ds_resolve_attachment = (struct radv_subpass_attachment) {
				.attachment =  ds_resolve->pDepthStencilResolveAttachment->attachment,
				.layout =      ds_resolve->pDepthStencilResolveAttachment->layout,
				.stencil_layout = (stencil_resolve_attachment ?
						   stencil_resolve_attachment->stencilLayout :
						   ds_resolve->pDepthStencilResolveAttachment->layout),
			};

			subpass->depth_resolve_mode = ds_resolve->depthResolveMode;
			subpass->stencil_resolve_mode = ds_resolve->stencilResolveMode;
		}
	}

	bool has_ingoing_dep = false;
	bool has_outgoing_dep = false;

	for (unsigned i = 0; i < pCreateInfo->dependencyCount; ++i) {
		radv_render_pass_add_subpass_dep(pass,
						 &pCreateInfo->pDependencies[i]);

		/* Determine if the subpass has explicit dependencies from/to
		 * VK_SUBPASS_EXTERNAL.
		 */
		if (pCreateInfo->pDependencies[i].srcSubpass == VK_SUBPASS_EXTERNAL)
			has_ingoing_dep = true;
		if (pCreateInfo->pDependencies[i].dstSubpass == VK_SUBPASS_EXTERNAL)
			has_outgoing_dep = true;
	}

	radv_render_pass_add_implicit_deps(pass,
					   has_ingoing_dep, has_outgoing_dep);

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

	radv_destroy_render_pass(device, pAllocator, pass);
}

void radv_GetRenderAreaGranularity(
    VkDevice                                    device,
    VkRenderPass                                renderPass,
    VkExtent2D*                                 pGranularity)
{
	pGranularity->width = 1;
	pGranularity->height = 1;
}

