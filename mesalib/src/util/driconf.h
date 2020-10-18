/*
 * XML DRI client-side driver configuration
 * Copyright (C) 2003 Felix Kuehling
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * FELIX KUEHLING, OR ANY OTHER CONTRIBUTORS BE LIABLE FOR ANY CLAIM, 
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR 
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE 
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 */
/**
 * \file driconf.h
 * \brief Pool of common options
 * \author Felix Kuehling
 *
 * This file defines macros that can be used to construct
 * driConfigOptions in the drivers.
 */

#ifndef __DRICONF_H
#define __DRICONF_H

/*
 * generic macros
 */

/** \brief Begin __driConfigOptions */
#define DRI_CONF_BEGIN \
"<?xml version=\"1.0\" standalone=\"yes\"?>" \
"<!DOCTYPE driinfo [" \
"   <!ELEMENT driinfo      (section*)>" \
"   <!ELEMENT section      (description+, option+)>" \
"   <!ELEMENT description  (enum*)>" \
"   <!ATTLIST description  lang CDATA #FIXED \"en\"" \
"                          text CDATA #REQUIRED>" \
"   <!ELEMENT option       (description+)>" \
"   <!ATTLIST option       name CDATA #REQUIRED" \
"                          type (bool|enum|int|float) #REQUIRED" \
"                          default CDATA #REQUIRED" \
"                          valid CDATA #IMPLIED>" \
"   <!ELEMENT enum         EMPTY>" \
"   <!ATTLIST enum         value CDATA #REQUIRED" \
"                          text CDATA #REQUIRED>" \
"]>" \
"<driinfo>\n"

/** \brief End __driConfigOptions */
#define DRI_CONF_END \
"</driinfo>\n"

/** \brief Begin a section of related options */
#define DRI_CONF_SECTION_BEGIN \
"<section>\n"

/** \brief End a section of related options */
#define DRI_CONF_SECTION_END \
"</section>\n"

/** \brief Begin an option definition */
#define DRI_CONF_OPT_BEGIN(name,type,def) \
"<option name=\""#name"\" type=\""#type"\" default=\""#def"\">\n"

/**
 * \brief Begin a boolean option definition, with the default value passed in
 * as a string
 */
#define DRI_CONF_OPT_BEGIN_B(name,def) \
"<option name=\""#name"\" type=\"bool\" default="#def">\n"

/** \brief Begin an option definition with restrictions on valid values */
#define DRI_CONF_OPT_BEGIN_V(name,type,def,valid) \
"<option name=\""#name"\" type=\""#type"\" default=\""#def"\" valid=\""valid"\">\n"

/** \brief End an option description */
#define DRI_CONF_OPT_END \
"</option>\n"

/** \brief A verbal description (empty version) */
#define DRI_CONF_DESC(text) \
"<description lang=\"en\" text=\""text"\"/>\n"

/** \brief Begining of a verbal description */
#define DRI_CONF_DESC_BEGIN(text) \
"<description lang=\"en\" text=\""text"\">\n"

/** \brief End a description */
#define DRI_CONF_DESC_END \
"</description>\n"

/** \brief A verbal description of an enum value */
#define DRI_CONF_ENUM(value,text) \
"<enum value=\""#value"\" text=\""text"\"/>\n"


/**
 * \brief Debugging options
 */
#define DRI_CONF_SECTION_DEBUG \
DRI_CONF_SECTION_BEGIN \
        DRI_CONF_DESC("Debugging")

#define DRI_CONF_ALWAYS_FLUSH_BATCH(def) \
DRI_CONF_OPT_BEGIN_B(always_flush_batch, def) \
        DRI_CONF_DESC("Enable flushing batchbuffer after each draw call") \
DRI_CONF_OPT_END

#define DRI_CONF_ALWAYS_FLUSH_CACHE(def) \
DRI_CONF_OPT_BEGIN_B(always_flush_cache, def) \
        DRI_CONF_DESC("Enable flushing GPU caches with each draw call") \
DRI_CONF_OPT_END

#define DRI_CONF_DISABLE_THROTTLING(def) \
DRI_CONF_OPT_BEGIN_B(disable_throttling, def) \
        DRI_CONF_DESC("Disable throttling on first batch after flush") \
DRI_CONF_OPT_END

#define DRI_CONF_FORCE_GLSL_EXTENSIONS_WARN(def) \
DRI_CONF_OPT_BEGIN_B(force_glsl_extensions_warn, def) \
        DRI_CONF_DESC("Force GLSL extension default behavior to 'warn'") \
DRI_CONF_OPT_END

#define DRI_CONF_DISABLE_BLEND_FUNC_EXTENDED(def) \
DRI_CONF_OPT_BEGIN_B(disable_blend_func_extended, def) \
        DRI_CONF_DESC("Disable dual source blending") \
DRI_CONF_OPT_END

#define DRI_CONF_DISABLE_ARB_GPU_SHADER5(def) \
DRI_CONF_OPT_BEGIN_B(disable_arb_gpu_shader5, def) \
        DRI_CONF_DESC("Disable GL_ARB_gpu_shader5") \
DRI_CONF_OPT_END

#define DRI_CONF_DUAL_COLOR_BLEND_BY_LOCATION(def) \
DRI_CONF_OPT_BEGIN_B(dual_color_blend_by_location, def) \
        DRI_CONF_DESC("Identify dual color blending sources by location rather than index") \
DRI_CONF_OPT_END

#define DRI_CONF_DISABLE_GLSL_LINE_CONTINUATIONS(def) \
DRI_CONF_OPT_BEGIN_B(disable_glsl_line_continuations, def) \
        DRI_CONF_DESC("Disable backslash-based line continuations in GLSL source") \
DRI_CONF_OPT_END

#define DRI_CONF_FORCE_GLSL_VERSION(def) \
DRI_CONF_OPT_BEGIN_V(force_glsl_version, int, def, "0:999") \
        DRI_CONF_DESC("Force a default GLSL version for shaders that lack an explicit #version line") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_GLSL_EXTENSION_DIRECTIVE_MIDSHADER(def) \
DRI_CONF_OPT_BEGIN_B(allow_glsl_extension_directive_midshader, def) \
        DRI_CONF_DESC("Allow GLSL #extension directives in the middle of shaders") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_GLSL_120_SUBSET_IN_110(def) \
DRI_CONF_OPT_BEGIN_B(allow_glsl_120_subset_in_110, def) \
        DRI_CONF_DESC("Allow a subset of GLSL 1.20 in GLSL 1.10 as needed by SPECviewperf13") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_GLSL_BUILTIN_CONST_EXPRESSION(def) \
DRI_CONF_OPT_BEGIN_B(allow_glsl_builtin_const_expression, def) \
        DRI_CONF_DESC("Allow builtins as part of constant expressions") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_GLSL_RELAXED_ES(def) \
DRI_CONF_OPT_BEGIN_B(allow_glsl_relaxed_es, def) \
        DRI_CONF_DESC("Allow some relaxation of GLSL ES shader restrictions") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_GLSL_BUILTIN_VARIABLE_REDECLARATION(def) \
DRI_CONF_OPT_BEGIN_B(allow_glsl_builtin_variable_redeclaration, def) \
        DRI_CONF_DESC("Allow GLSL built-in variables to be redeclared verbatim") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_HIGHER_COMPAT_VERSION(def) \
DRI_CONF_OPT_BEGIN_B(allow_higher_compat_version, def) \
        DRI_CONF_DESC("Allow a higher compat profile (version 3.1+) for apps that request it") \
DRI_CONF_OPT_END

#define DRI_CONF_FORCE_GLSL_ABS_SQRT(def) \
DRI_CONF_OPT_BEGIN_B(force_glsl_abs_sqrt, def) \
        DRI_CONF_DESC("Force computing the absolute value for sqrt() and inversesqrt()") \
DRI_CONF_OPT_END

#define DRI_CONF_GLSL_CORRECT_DERIVATIVES_AFTER_DISCARD(def) \
DRI_CONF_OPT_BEGIN_B(glsl_correct_derivatives_after_discard, def) \
        DRI_CONF_DESC("Implicit and explicit derivatives after a discard behave as if the discard didn't happen") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_GLSL_CROSS_STAGE_INTERPOLATION_MISMATCH(def) \
DRI_CONF_OPT_BEGIN_B(allow_glsl_cross_stage_interpolation_mismatch, def) \
        DRI_CONF_DESC("Allow interpolation qualifier mismatch across shader stages") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_GLSL_LAYOUT_QUALIFIER_ON_FUNCTION_PARAMETERS(def) \
DRI_CONF_OPT_BEGIN_B(allow_glsl_layout_qualifier_on_function_parameters, def) \
        DRI_CONF_DESC("Allow layout qualifiers on function parameters.") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_DRAW_OUT_OF_ORDER(def) \
DRI_CONF_OPT_BEGIN_B(allow_draw_out_of_order, def) \
        DRI_CONF_DESC("Allow out-of-order draw optimizations. Set when Z fighting doesn't have to be accurate.") \
DRI_CONF_OPT_END

#define DRI_CONF_FORCE_GL_VENDOR(def) \
DRI_CONF_OPT_BEGIN(force_gl_vendor, string, def) \
        DRI_CONF_DESC("Allow GPU vendor to be overridden.") \
DRI_CONF_OPT_END

#define DRI_CONF_FORCE_COMPAT_PROFILE(def) \
DRI_CONF_OPT_BEGIN_B(force_compat_profile, def) \
        DRI_CONF_DESC("Force an OpenGL compatibility context") \
DRI_CONF_OPT_END

/**
 * \brief Image quality-related options
 */
#define DRI_CONF_SECTION_QUALITY \
DRI_CONF_SECTION_BEGIN \
        DRI_CONF_DESC("Image Quality")

#define DRI_CONF_PRECISE_TRIG(def) \
DRI_CONF_OPT_BEGIN_B(precise_trig, def) \
        DRI_CONF_DESC("Prefer accuracy over performance in trig functions") \
DRI_CONF_OPT_END

#define DRI_CONF_PP_CELSHADE(def) \
DRI_CONF_OPT_BEGIN_V(pp_celshade,enum,def,"0:1") \
        DRI_CONF_DESC("A post-processing filter to cel-shade the output") \
DRI_CONF_OPT_END

#define DRI_CONF_PP_NORED(def) \
DRI_CONF_OPT_BEGIN_V(pp_nored,enum,def,"0:1") \
        DRI_CONF_DESC("A post-processing filter to remove the red channel") \
DRI_CONF_OPT_END

#define DRI_CONF_PP_NOGREEN(def) \
DRI_CONF_OPT_BEGIN_V(pp_nogreen,enum,def,"0:1") \
        DRI_CONF_DESC("A post-processing filter to remove the green channel") \
DRI_CONF_OPT_END

#define DRI_CONF_PP_NOBLUE(def) \
DRI_CONF_OPT_BEGIN_V(pp_noblue,enum,def,"0:1") \
        DRI_CONF_DESC("A post-processing filter to remove the blue channel") \
DRI_CONF_OPT_END

#define DRI_CONF_PP_JIMENEZMLAA(def,min,max) \
DRI_CONF_OPT_BEGIN_V(pp_jimenezmlaa,int,def, # min ":" # max ) \
        DRI_CONF_DESC("Morphological anti-aliasing based on Jimenez\\\' MLAA. 0 to disable, 8 for default quality") \
DRI_CONF_OPT_END

#define DRI_CONF_PP_JIMENEZMLAA_COLOR(def,min,max) \
DRI_CONF_OPT_BEGIN_V(pp_jimenezmlaa_color,int,def, # min ":" # max ) \
        DRI_CONF_DESC("Morphological anti-aliasing based on Jimenez\\\' MLAA. 0 to disable, 8 for default quality. Color version, usable with 2d GL apps") \
DRI_CONF_OPT_END



/**
 * \brief Performance-related options
 */
#define DRI_CONF_SECTION_PERFORMANCE \
DRI_CONF_SECTION_BEGIN \
        DRI_CONF_DESC("Performance")

#define DRI_CONF_VBLANK_NEVER 0
#define DRI_CONF_VBLANK_DEF_INTERVAL_0 1
#define DRI_CONF_VBLANK_DEF_INTERVAL_1 2
#define DRI_CONF_VBLANK_ALWAYS_SYNC 3
#define DRI_CONF_VBLANK_MODE(def) \
DRI_CONF_OPT_BEGIN_V(vblank_mode,enum,def,"0:3") \
        DRI_CONF_DESC_BEGIN("Synchronization with vertical refresh (swap intervals)") \
                DRI_CONF_ENUM(0,"Never synchronize with vertical refresh, ignore application's choice") \
                DRI_CONF_ENUM(1,"Initial swap interval 0, obey application's choice") \
                DRI_CONF_ENUM(2,"Initial swap interval 1, obey application's choice") \
                DRI_CONF_ENUM(3,"Always synchronize with vertical refresh, application chooses the minimum swap interval") \
        DRI_CONF_DESC_END \
DRI_CONF_OPT_END

#define DRI_CONF_ADAPTIVE_SYNC(def) \
DRI_CONF_OPT_BEGIN_B(adaptive_sync,def) \
        DRI_CONF_DESC("Adapt the monitor sync to the application performance (when possible)") \
DRI_CONF_OPT_END

#define DRI_CONF_VK_WSI_FORCE_BGRA8_UNORM_FIRST(def) \
DRI_CONF_OPT_BEGIN_B(vk_wsi_force_bgra8_unorm_first, def) \
        DRI_CONF_DESC("Force vkGetPhysicalDeviceSurfaceFormatsKHR to return VK_FORMAT_B8G8R8A8_UNORM as the first format") \
DRI_CONF_OPT_END

#define DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(def) \
DRI_CONF_OPT_BEGIN_V(vk_x11_override_min_image_count, int, def, "0:999") \
        DRI_CONF_DESC("Override the VkSurfaceCapabilitiesKHR::minImageCount (0 = no override)") \
DRI_CONF_OPT_END

#define DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(def) \
DRI_CONF_OPT_BEGIN_B(vk_x11_strict_image_count, def) \
        DRI_CONF_DESC("Force the X11 WSI to create exactly the number of image specified by the application in VkSwapchainCreateInfoKHR::minImageCount") \
DRI_CONF_OPT_END

#define DRI_CONF_MESA_GLTHREAD(def) \
DRI_CONF_OPT_BEGIN_B(mesa_glthread, def) \
        DRI_CONF_DESC("Enable offloading GL driver work to a separate thread") \
DRI_CONF_OPT_END

#define DRI_CONF_MESA_NO_ERROR(def) \
DRI_CONF_OPT_BEGIN_B(mesa_no_error, def) \
        DRI_CONF_DESC("Disable GL driver error checking") \
DRI_CONF_OPT_END

#define DRI_CONF_DISABLE_EXT_BUFFER_AGE(def) \
DRI_CONF_OPT_BEGIN_B(glx_disable_ext_buffer_age, def) \
   DRI_CONF_DESC("Disable the GLX_EXT_buffer_age extension") \
DRI_CONF_OPT_END

#define DRI_CONF_DISABLE_OML_SYNC_CONTROL(def) \
DRI_CONF_OPT_BEGIN_B(glx_disable_oml_sync_control, def) \
   DRI_CONF_DESC("Disable the GLX_OML_sync_control extension") \
DRI_CONF_OPT_END

#define DRI_CONF_DISABLE_SGI_VIDEO_SYNC(def) \
DRI_CONF_OPT_BEGIN_B(glx_disable_sgi_video_sync, def) \
   DRI_CONF_DESC("Disable the GLX_SGI_video_sync extension") \
DRI_CONF_OPT_END



/**
 * \brief Miscellaneous configuration options
 */
#define DRI_CONF_SECTION_MISCELLANEOUS \
DRI_CONF_SECTION_BEGIN \
        DRI_CONF_DESC("Miscellaneous")

#define DRI_CONF_ALWAYS_HAVE_DEPTH_BUFFER(def) \
DRI_CONF_OPT_BEGIN_B(always_have_depth_buffer, def) \
        DRI_CONF_DESC("Create all visuals with a depth buffer") \
DRI_CONF_OPT_END

#define DRI_CONF_GLSL_ZERO_INIT(def) \
DRI_CONF_OPT_BEGIN_B(glsl_zero_init, def) \
        DRI_CONF_DESC("Force uninitialized variables to default to zero") \
DRI_CONF_OPT_END

#define DRI_CONF_VS_POSITION_ALWAYS_INVARIANT(def) \
DRI_CONF_OPT_BEGIN_B(vs_position_always_invariant, def) \
        DRI_CONF_DESC("Force the vertex shader's gl_Position output to be considered 'invariant'") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_RGB10_CONFIGS(def) \
DRI_CONF_OPT_BEGIN_B(allow_rgb10_configs, def) \
DRI_CONF_DESC("Allow exposure of visuals and fbconfigs with rgb10a2 formats") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_RGB565_CONFIGS(def) \
DRI_CONF_OPT_BEGIN_B(allow_rgb565_configs, def) \
DRI_CONF_DESC("Allow exposure of visuals and fbconfigs with rgb565 formats") \
DRI_CONF_OPT_END

#define DRI_CONF_ALLOW_FP16_CONFIGS(def) \
DRI_CONF_OPT_BEGIN_B(allow_fp16_configs, def) \
DRI_CONF_DESC("Allow exposure of visuals and fbconfigs with fp16 formats") \
DRI_CONF_OPT_END

#define DRI_CONF_FORCE_INTEGER_TEX_NEAREST(def) \
DRI_CONF_OPT_BEGIN_B(force_integer_tex_nearest, def) \
        DRI_CONF_DESC("Force integer textures to use nearest filtering") \
DRI_CONF_OPT_END

/**
 * \brief Initialization configuration options
 */
#define DRI_CONF_SECTION_INITIALIZATION \
DRI_CONF_SECTION_BEGIN \
        DRI_CONF_DESC("Initialization")

#define DRI_CONF_DEVICE_ID_PATH_TAG(def) \
DRI_CONF_OPT_BEGIN(device_id, string, def) \
        DRI_CONF_DESC("Define the graphic device to use if possible") \
DRI_CONF_OPT_END

#define DRI_CONF_DRI_DRIVER(def) \
DRI_CONF_OPT_BEGIN(dri_driver, string, def) \
        DRI_CONF_DESC("Override the DRI driver to load") \
DRI_CONF_OPT_END

/**
 * \brief Gallium-Nine specific configuration options
 */

#define DRI_CONF_SECTION_NINE \
DRI_CONF_SECTION_BEGIN \
        DRI_CONF_DESC("Gallium Nine")

#define DRI_CONF_NINE_THROTTLE(def) \
DRI_CONF_OPT_BEGIN(throttle_value, int, def) \
        DRI_CONF_DESC("Define the throttling value. -1 for no throttling, -2 for default (usually 2), 0 for glfinish behaviour") \
DRI_CONF_OPT_END

#define DRI_CONF_NINE_THREADSUBMIT(def) \
DRI_CONF_OPT_BEGIN_B(thread_submit, def) \
        DRI_CONF_DESC("Use an additional thread to submit buffers.") \
DRI_CONF_OPT_END

#define DRI_CONF_NINE_OVERRIDEVENDOR(def) \
DRI_CONF_OPT_BEGIN(override_vendorid, int, def) \
        DRI_CONF_DESC("Define the vendor_id to report. This allows faking another hardware vendor.") \
DRI_CONF_OPT_END

#define DRI_CONF_NINE_ALLOWDISCARDDELAYEDRELEASE(def) \
DRI_CONF_OPT_BEGIN_B(discard_delayed_release, def) \
        DRI_CONF_DESC("Whether to allow the display server to release buffers with a delay when using d3d's presentation mode DISCARD. Default to true. Set to false if suffering from lag (thread_submit=true can also help in this situation).") \
DRI_CONF_OPT_END

#define DRI_CONF_NINE_TEARFREEDISCARD(def) \
DRI_CONF_OPT_BEGIN_B(tearfree_discard, def) \
        DRI_CONF_DESC("Whether to make d3d's presentation mode DISCARD (games usually use that mode) Tear Free. If rendering above screen refresh, some frames will get skipped. false by default.") \
DRI_CONF_OPT_END

#define DRI_CONF_NINE_CSMT(def) \
DRI_CONF_OPT_BEGIN(csmt_force, int, def) \
        DRI_CONF_DESC("If set to 1, force gallium nine CSMT. If set to 0, disable it. By default (-1) CSMT is enabled on known thread-safe drivers.") \
DRI_CONF_OPT_END

#define DRI_CONF_NINE_DYNAMICTEXTUREWORKAROUND(def) \
DRI_CONF_OPT_BEGIN_B(dynamic_texture_workaround, def) \
        DRI_CONF_DESC("If set to true, use a ram intermediate buffer for dynamic textures. Increases ram usage, which can cause out of memory issues, but can fix glitches for some games.") \
DRI_CONF_OPT_END

#define DRI_CONF_NINE_SHADERINLINECONSTANTS(def) \
DRI_CONF_OPT_BEGIN_B(shader_inline_constants, def) \
        DRI_CONF_DESC("If set to true, recompile shaders with integer or boolean constants when the values are known. Can cause stutter, but can increase slightly performance.") \
DRI_CONF_OPT_END

/**
 * \brief radeonsi specific configuration options
 */

#define DRI_CONF_RADEONSI_ASSUME_NO_Z_FIGHTS(def) \
DRI_CONF_OPT_BEGIN_B(radeonsi_assume_no_z_fights, def) \
        DRI_CONF_DESC("Assume no Z fights (enables aggressive out-of-order rasterization to improve performance; may cause rendering errors)") \
DRI_CONF_OPT_END

#define DRI_CONF_RADEONSI_COMMUTATIVE_BLEND_ADD(def) \
DRI_CONF_OPT_BEGIN_B(radeonsi_commutative_blend_add, def) \
        DRI_CONF_DESC("Commutative additive blending optimizations (may cause rendering errors)") \
DRI_CONF_OPT_END

#define DRI_CONF_RADEONSI_ZERO_ALL_VRAM_ALLOCS(def) \
DRI_CONF_OPT_BEGIN_B(radeonsi_zerovram, def) \
        DRI_CONF_DESC("Zero all vram allocations") \
DRI_CONF_OPT_END

#define DRI_CONF_V3D_NONMSAA_TEXTURE_SIZE_LIMIT(def) \
DRI_CONF_OPT_BEGIN_B(v3d_nonmsaa_texture_size_limit, def) \
        DRI_CONF_DESC("Report the non-MSAA-only texture size limit") \
DRI_CONF_OPT_END

/**
 * \brief virgl specific configuration options
 */

#define DRI_CONF_GLES_EMULATE_BGRA(def) \
DRI_CONF_OPT_BEGIN_B(gles_emulate_bgra, def) \
        DRI_CONF_DESC("On GLES emulate BGRA formats by using a swizzled RGBA format") \
DRI_CONF_OPT_END

#define DRI_CONF_GLES_APPLY_BGRA_DEST_SWIZZLE(def) \
DRI_CONF_OPT_BEGIN_B(gles_apply_bgra_dest_swizzle, def) \
        DRI_CONF_DESC("When the BGRA formats are emulated by using swizzled RGBA formats on GLES apply the swizzle when writing") \
DRI_CONF_OPT_END

#define DRI_CONF_GLES_SAMPLES_PASSED_VALUE(def, minimum, maximum) \
DRI_CONF_OPT_BEGIN_V(gles_samples_passed_value, def, minimum, maximum) \
        DRI_CONF_DESC("GL_SAMPLES_PASSED value when emulated by GL_ANY_SAMPLES_PASSED") \
DRI_CONF_OPT_END

/**
 * \brief RADV specific configuration options
 */

#define DRI_CONF_RADV_REPORT_LLVM9_VERSION_STRING(def) \
DRI_CONF_OPT_BEGIN_B(radv_report_llvm9_version_string, def) \
        DRI_CONF_DESC("Report LLVM 9.0.1 for games that apply shader workarounds if missing (for ACO only)") \
DRI_CONF_OPT_END

#define DRI_CONF_RADV_ENABLE_MRT_OUTPUT_NAN_FIXUP(def) \
DRI_CONF_OPT_BEGIN_B(radv_enable_mrt_output_nan_fixup, def) \
        DRI_CONF_DESC("Replace NaN outputs from fragment shaders with zeroes for floating point render target") \
DRI_CONF_OPT_END

#define DRI_CONF_RADV_NO_DYNAMIC_BOUNDS(def) \
DRI_CONF_OPT_BEGIN_B(radv_no_dynamic_bounds, def) \
        DRI_CONF_DESC("Disabling bounds checking for dynamic buffer descriptors") \
DRI_CONF_OPT_END

#endif
