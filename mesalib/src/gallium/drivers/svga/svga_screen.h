/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_SCREEN_H
#define SVGA_SCREEN_H


#include "pipe/p_screen.h"
#include "util/u_thread.h"

#include "svga_screen_cache.h"


struct svga_winsys_screen;
struct svga_winsys_context;
struct SVGACmdMemory;

/**
 * Subclass of pipe_screen
 */
struct svga_screen
{
   struct pipe_screen screen;
   struct svga_winsys_screen *sws;

   SVGA3dHardwareVersion hw_version;

   /** Device caps */
   bool haveProvokingVertex;
   bool haveLineStipple, haveLineSmooth;
   bool haveBlendLogicops;
   float maxLineWidth, maxLineWidthAA;
   float maxPointSize;
   float pointSmoothThreshold; /** Disable point AA for sizes less than this */
   unsigned max_color_buffers;
   unsigned max_const_buffers;
   unsigned max_viewports;
   unsigned ms_samples;
   unsigned forcedSampleCount; /* available with GL43 capable device only */
   unsigned max_vs_inputs;
   unsigned max_vs_outputs;
   unsigned max_gs_inputs;

   struct {
      unsigned force_level_surface_view:1;
      unsigned force_surface_view:1;
      unsigned no_surface_view:1;
      unsigned force_sampler_view:1;
      unsigned no_sampler_view:1;
      unsigned no_cache_index_buffers:1;
      unsigned tessellation:1;
      unsigned sampler_state_mapping:1;
      unsigned pad:24;
   } debug;

   unsigned texture_timestamp;
   mtx_t tex_mutex;

   mtx_t swc_mutex; /* Used for buffer uploads */

   /* which formats to translate depth formats into */
   struct {
     enum SVGA3dSurfaceFormat z16;

     /* note gallium order */
     enum SVGA3dSurfaceFormat x8z24;
     enum SVGA3dSurfaceFormat s8z24;
   } depth;

   struct svga_host_surface_cache cache;

   /** HUD counters */
   struct {
      /** Memory used by all resources (buffers and surfaces) */
      uint64_t total_resource_bytes;
      uint64_t num_resources;
      uint64_t num_failed_allocations;
   } hud;
};

#if !MESA_DEBUG
/** cast wrapper */
static inline struct svga_screen *
svga_screen(struct pipe_screen *pscreen)
{
   return (struct svga_screen *) pscreen;
}
#else
struct svga_screen *
svga_screen(struct pipe_screen *screen);
#endif

#endif /* SVGA_SCREEN_H */
