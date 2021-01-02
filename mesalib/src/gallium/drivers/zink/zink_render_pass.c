/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_render_pass.h"

#include "zink_screen.h"

#include "util/u_memory.h"
#include "util/u_string.h"

static VkRenderPass
create_render_pass(VkDevice dev, struct zink_render_pass_state *state)
{

   VkAttachmentReference color_refs[PIPE_MAX_COLOR_BUFS], zs_ref;
   VkAttachmentDescription attachments[PIPE_MAX_COLOR_BUFS + 1];

   for (int i = 0; i < state->num_cbufs; i++) {
      struct zink_rt_attrib *rt = state->rts + i;
      attachments[i].flags = 0;
      attachments[i].format = rt->format;
      attachments[i].samples = rt->samples;
      attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
      attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
      attachments[i].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      attachments[i].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
      color_refs[i].attachment = i;
      color_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
   }

   int num_attachments = state->num_cbufs;
   if (state->have_zsbuf)  {
      struct zink_rt_attrib *rt = state->rts + state->num_cbufs;
      attachments[num_attachments].flags = 0;
      attachments[num_attachments].format = rt->format;
      attachments[num_attachments].samples = rt->samples;
      attachments[num_attachments].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      attachments[num_attachments].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
      attachments[num_attachments].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
      attachments[num_attachments].stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
      attachments[num_attachments].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
      attachments[num_attachments].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

      zs_ref.attachment = num_attachments++;
      zs_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
   }

   VkSubpassDescription subpass = {};
   subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
   subpass.colorAttachmentCount = state->num_cbufs;
   subpass.pColorAttachments = color_refs;
   subpass.pDepthStencilAttachment = state->have_zsbuf ? &zs_ref : NULL;

   VkRenderPassCreateInfo rpci = {};
   rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
   rpci.attachmentCount = num_attachments;
   rpci.pAttachments = attachments;
   rpci.subpassCount = 1;
   rpci.pSubpasses = &subpass;

   VkRenderPass render_pass;
   if (vkCreateRenderPass(dev, &rpci, NULL, &render_pass) != VK_SUCCESS)
      return VK_NULL_HANDLE;

   return render_pass;
}

struct zink_render_pass *
zink_create_render_pass(struct zink_screen *screen,
                        struct zink_render_pass_state *state)
{
   struct zink_render_pass *rp = CALLOC_STRUCT(zink_render_pass);
   if (!rp)
      goto fail;

   pipe_reference_init(&rp->reference, 1);

   rp->render_pass = create_render_pass(screen->dev, state);
   if (!rp->render_pass)
      goto fail;
   memcpy(&rp->state, state, sizeof(struct zink_render_pass_state));
   return rp;

fail:
   if (rp)
      zink_destroy_render_pass(screen, rp);
   return NULL;
}

void
zink_destroy_render_pass(struct zink_screen *screen,
                         struct zink_render_pass *rp)
{
   vkDestroyRenderPass(screen->dev, rp->render_pass, NULL);
   FREE(rp);
}

void
debug_describe_zink_render_pass(char* buf, const struct zink_render_pass *ptr)
{
   sprintf(buf, "zink_render_pass");
}
