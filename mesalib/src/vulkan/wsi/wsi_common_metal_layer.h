/*
 * Copyright 2024 Autodesk, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef WSI_COMMON_METAL_LAYER_H
#define WSI_COMMON_METAL_LAYER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __OBJC__
@class CAMetalLayer;
typedef unsigned long NSUInteger;
typedef enum MTLPixelFormat : NSUInteger MTLPixelFormat;
#else
typedef void CAMetalLayer;
typedef enum MTLPixelFormat : unsigned long
{
   MTLPixelFormatBGRA8Unorm = 80,
   MTLPixelFormatBGRA8Unorm_sRGB = 81,
   MTLPixelFormatRGB10A2Unorm = 90,
   MTLPixelFormatBGR10A2Unorm = 94,
   MTLPixelFormatRGBA16Float = 115,
} MTLPixelFormat;
#endif

typedef void CAMetalDrawableBridged;

void
wsi_metal_layer_size(const CAMetalLayer *metal_layer,
   uint32_t *width, uint32_t *height);

void
wsi_metal_layer_configure(const CAMetalLayer *metal_layer,
   uint32_t width, uint32_t height, uint32_t image_count,
   MTLPixelFormat format, bool enable_opaque, bool enable_immediate);

CAMetalDrawableBridged *
wsi_metal_layer_acquire_drawable(const CAMetalLayer *metal_layer);

struct wsi_metal_layer_blit_context;

struct wsi_metal_layer_blit_context *
wsi_create_metal_layer_blit_context();

void
wsi_destroy_metal_layer_blit_context(struct wsi_metal_layer_blit_context *context);

void
wsi_metal_layer_blit_and_present(struct wsi_metal_layer_blit_context *context,
   CAMetalDrawableBridged **drawable_ptr, void *buffer,
   uint32_t width, uint32_t height, uint32_t row_pitch);

void
wsi_metal_layer_cancel_present(struct wsi_metal_layer_blit_context *context,
   CAMetalDrawableBridged **drawable_ptr);

#endif // WSI_COMMON_METAL_LAYER_H
