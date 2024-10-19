/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hk_private.h"

#include "asahi/lib/agx_bo.h"
#include "util/simple_mtx.h"

struct hk_device;

struct hk_descriptor_table {
   simple_mtx_t mutex;

   uint32_t desc_size;  /**< Size of a descriptor */
   uint32_t alloc;      /**< Number of descriptors allocated */
   uint32_t max_alloc;  /**< Maximum possible number of descriptors */
   uint32_t next_desc;  /**< Next unallocated descriptor */
   uint32_t free_count; /**< Size of free_table */

   struct agx_bo *bo;
   void *map;

   /* Stack for free descriptor elements */
   uint32_t *free_table;
};

VkResult hk_descriptor_table_init(struct hk_device *dev,
                                  struct hk_descriptor_table *table,
                                  uint32_t descriptor_size,
                                  uint32_t min_descriptor_count,
                                  uint32_t max_descriptor_count);

void hk_descriptor_table_finish(struct hk_device *dev,
                                struct hk_descriptor_table *table);

VkResult hk_descriptor_table_add(struct hk_device *dev,
                                 struct hk_descriptor_table *table,
                                 const void *desc_data, size_t desc_size,
                                 uint32_t *index_out);

void hk_descriptor_table_remove(struct hk_device *dev,
                                struct hk_descriptor_table *table,
                                uint32_t index);
