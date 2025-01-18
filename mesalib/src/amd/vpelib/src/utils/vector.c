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
#include <string.h>
#include "vector.h"
#include "vpe_priv.h"

struct vpe_vector *vpe_vector_create(
    struct vpe_priv *vpe_priv, size_t element_size, size_t initial_capacity)
{
    struct vpe_vector *vector;
    vector = (struct vpe_vector *)vpe_zalloc(sizeof(struct vpe_vector));
    if (!vector)
        return NULL;

    vector->element = vpe_zalloc(initial_capacity * element_size);
    if (!vector->element) {
        vpe_free(vector);
        return NULL;
    }

    vector->vpe_priv     = vpe_priv;
    vector->num_elements = 0;
    vector->capacity     = initial_capacity;
    vector->element_size = element_size;

    return vector;
}

static struct vpe_vector *vector_realloc(struct vpe_vector *vector, size_t new_size)
{
    struct vpe_priv *vpe_priv = vector->vpe_priv;

    void *new_element = vpe_zalloc(new_size);
    if (!new_element)
        return NULL;

    memcpy(new_element, vector->element, vector->num_elements * vector->element_size);
    vpe_free(vector->element);

    vector->element  = new_element;
    vector->capacity = new_size / vector->element_size;

    return vector;
}

void *vpe_vector_get(struct vpe_vector *vector, size_t idx)
{
    if (!vector)
        return NULL;

    return (void *)((char *)(vector->element) + (idx * vector->element_size));
}

void vpe_vector_push(struct vpe_vector *vector, void *p_element)
{
    if (!p_element || !vector)
        return;

    if (vector->num_elements >= vector->capacity) {
        vector->capacity *= 2;
        vector = vector_realloc(vector, vector->capacity * vector->element_size);
    }

    if (!vector)
        return;

    memcpy((void *)((char *)(vector->element) + (vector->num_elements * vector->element_size)),
        p_element, vector->element_size);
    vector->num_elements++;
}

void vpe_vector_clear(struct vpe_vector *vector)
{
    if (!vector)
        return;

    vector->num_elements = 0;
    memset(vector->element, 0, vector->capacity * vector->element_size);
}

void vpe_vector_free(struct vpe_vector *vector)
{
    struct vpe_priv *vpe_priv = vector->vpe_priv;

    vpe_free(vector->element);
    vector->element = NULL;
    vpe_free(vector);
}
