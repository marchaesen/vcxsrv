/*
 * Copyright © 2017 Intel Corporation
 * Copyright © 2022 Collabora, Ltd
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
#ifndef VK_DESCRIPTOR_UPDATE_TEMPLATE_H
#define VK_DESCRIPTOR_UPDATE_TEMPLATE_H

#include "vk_object.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vk_descriptor_template_entry {
   /** VkDescriptorUpdateTemplateEntry::descriptorType */
   VkDescriptorType type;

   /** VkDescriptorUpdateTemplateEntry::dstBinding */
   uint32_t binding;

   /** VkDescriptorUpdateTemplateEntry::dstArrayElement */
   uint32_t array_element;

   /** VkDescriptorUpdateTemplateEntry::descriptorCount */
   uint32_t array_count;

   /** VkDescriptorUpdateTemplateEntry::offset
    *
    * Offset into the user provided data */
   size_t offset;

   /** VkDescriptorUpdateTemplateEntry::stride
    *
    * Stride between elements into the user provided data
    */
   size_t stride;
};

struct vk_descriptor_update_template {
   struct vk_object_base base;

   /** VkDescriptorUpdateTemplateCreateInfo::templateType */
   VkDescriptorUpdateTemplateType type;

   /** VkDescriptorUpdateTemplateCreateInfo::pipelineBindPoint */
   VkPipelineBindPoint bind_point;

   /** VkDescriptorUpdateTemplateCreateInfo::set
    *
    * The descriptor set this template corresponds to. This value is only
    * valid if the template was created with the templateType
    * VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET.
    */
   uint8_t set;

   /** VkDescriptorUpdateTemplateCreateInfo::descriptorUpdateEntryCount */
   uint32_t entry_count;

   /** Reference count.
    *
    * It is legal to enqueue a push template update to a secondary command
    * buffer and destroy the template before executing the secondary. As
    * capture-replay based secondaries reference the template, we need to
    * reference count to extend the lifetime appropriately.
    */
   uint32_t ref_cnt;

   /** Entries of the template */
   struct vk_descriptor_template_entry entries[0];
};

static inline struct vk_descriptor_update_template *
vk_descriptor_update_template_ref(struct vk_descriptor_update_template *templ)
{
   assert(templ && templ->ref_cnt >= 1);
   p_atomic_inc(&templ->ref_cnt);
   return templ;
}

static inline void
vk_descriptor_update_template_unref(struct vk_device *device,
                                    struct vk_descriptor_update_template *templ)
{
   assert(templ && templ->ref_cnt >= 1);
   if (p_atomic_dec_zero(&templ->ref_cnt))
      vk_object_free(device, NULL, templ);
}

VK_DEFINE_NONDISP_HANDLE_CASTS(vk_descriptor_update_template, base,
                               VkDescriptorUpdateTemplate,
                               VK_OBJECT_TYPE_DESCRIPTOR_UPDATE_TEMPLATE)

#ifdef __cplusplus
}
#endif

#endif /* VK_DESCRIPTOR_UPDATE_TEMPLATE_H */
