/* Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#pragma once

#include "vpe_types.h"
#include "resource.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vpe_priv;

struct vpe_vector {
    struct vpe_priv *vpe_priv; /*< store the vpe_priv for alloc/free memory */

    void  *element;      /*< the internal vector memory storage */
    size_t num_elements; /*< number of stored elements */
    size_t capacity;
    /*< size of the storage space currently allocated for the vector */
    size_t element_size; /*< size of elements in bytes */
};

/**
 * Create the vector.
 * @param[in]  vpe_priv         vpe instance created by vpe_create()
 * @param[in]  element_size     size of each element of this vector.
 * @param[in]  initial_capacity initial capacity of the vector.
 */
struct vpe_vector *vpe_vector_create(
    struct vpe_priv *vpe_priv, size_t element_size, size_t initial_capacity);

/**
 * Get the specific element from vector by index.
 * @param[in]  vector vector that we want to get the element from.
 * @param[in]  idx    index
 */
void *vpe_vector_get(struct vpe_vector *vector, size_t idx);

/**
 * Push the element to end of the vector.
 * @param[in]  vector    vector that we want to push to the end.
 * @param[in]  p_element pointer of the element
 */
void vpe_vector_push(struct vpe_vector *vector, void *p_element);

/**
 * Clear the vector.
 * @param[in]  vector vector that we want to clear.
 */
void vpe_vector_clear(struct vpe_vector *vector);

/**
 * Free the vector.
 * @param[in]  vector   vector that we want to free.
 */
void vpe_vector_free(struct vpe_vector *vpe_vector);

#ifdef __cplusplus
}
#endif
