/*
 * Copyright 2024 Autodesk, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wsi_common_metal_layer.h"

#import <QuartzCore/CAMetalLayer.h>
#import <Metal/Metal.h>

void
wsi_metal_layer_size(const CAMetalLayer *metal_layer,
   uint32_t *width, uint32_t *height)
{
   @autoreleasepool {
      CGSize size = [metal_layer drawableSize];
      if (width)
         *width = size.width;
      if (height)
         *height = size.height;
   }
}

void
wsi_metal_layer_configure(const CAMetalLayer *metal_layer,
   uint32_t width, uint32_t height, uint32_t image_count,
   MTLPixelFormat format, bool enable_opaque, bool enable_immediate)
{
   @autoreleasepool {
      if (metal_layer.device == nil) {
         metal_layer.device = metal_layer.preferredDevice;
      }

      /* So acquire timeout works */
      metal_layer.allowsNextDrawableTimeout = YES;
      /* So we can blit to the drawable */
      metal_layer.framebufferOnly = NO;

      metal_layer.maximumDrawableCount = image_count;
      metal_layer.drawableSize = (CGSize){.width = width, .height = height};
      metal_layer.pixelFormat = format;
      metal_layer.opaque = enable_opaque;
      metal_layer.displaySyncEnabled = !enable_immediate;
   }
}

CAMetalDrawableBridged *
wsi_metal_layer_acquire_drawable(const CAMetalLayer *metal_layer)
{
   @autoreleasepool {
      id<CAMetalDrawable> drawable = [metal_layer nextDrawable];
      return (__bridge_retained CAMetalDrawableBridged *)drawable;
   }
}

struct wsi_metal_layer_blit_context {
   id<MTLDevice> device;
   id<MTLCommandQueue> commandQueue;
};

struct wsi_metal_layer_blit_context *
wsi_create_metal_layer_blit_context()
{
   @autoreleasepool {
      struct wsi_metal_layer_blit_context *context = malloc(sizeof(struct wsi_metal_layer_blit_context));
      memset((void*)context, 0, sizeof(*context));

      context->device = MTLCreateSystemDefaultDevice();
      context->commandQueue = [context->device newCommandQueue];

      return context;
   }
}

void
wsi_destroy_metal_layer_blit_context(struct wsi_metal_layer_blit_context *context)
{
   @autoreleasepool {
      context->device = nil;
      context->commandQueue = nil;
      free(context);
   }
}

void
wsi_metal_layer_blit_and_present(struct wsi_metal_layer_blit_context *context,
   CAMetalDrawableBridged **drawable_ptr, void *buffer,
   uint32_t width, uint32_t height, uint32_t row_pitch)
{
   @autoreleasepool {
      id<CAMetalDrawable> drawable = (__bridge_transfer id<CAMetalDrawable>)*drawable_ptr;

      id<MTLCommandBuffer> commandBuffer = [context->commandQueue commandBuffer];
      id<MTLBlitCommandEncoder> commandEncoder = [commandBuffer blitCommandEncoder];

      NSUInteger image_size = height * row_pitch;
      id<MTLBuffer> image_buffer = [context->device newBufferWithBytesNoCopy:buffer
         length:image_size
         options:MTLResourceStorageModeShared
         deallocator:nil];

      [commandEncoder copyFromBuffer:image_buffer
         sourceOffset:0
         sourceBytesPerRow:row_pitch
         sourceBytesPerImage:image_size
         sourceSize:MTLSizeMake(width, height, 1)
         toTexture:drawable.texture
         destinationSlice:0
         destinationLevel:0
         destinationOrigin:MTLOriginMake(0, 0, 0)];
      [commandEncoder endEncoding];
      [commandBuffer presentDrawable:drawable];
      [commandBuffer commit];

      *drawable_ptr = nil;
   }
}

void
wsi_metal_layer_cancel_present(struct wsi_metal_layer_blit_context *context,
   CAMetalDrawableBridged **drawable_ptr)
{
   @autoreleasepool {
      id<CAMetalDrawable> drawable = (__bridge_transfer id<CAMetalDrawable>)*drawable_ptr;
      if (drawable == nil)
         return;

      /* We need to present the drawable to release it... */
      id<MTLCommandBuffer> commandBuffer = [context->commandQueue commandBuffer];
      [commandBuffer presentDrawable:drawable];
      [commandBuffer commit];

      *drawable_ptr = nil;
   }
}
