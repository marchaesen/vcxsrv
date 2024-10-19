/* Copyright 2022 Advanced Micro Devices, Inc.
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

/**
 * @file         vpe_types.h
 * @brief        This is the file containing the API structures for the VPE library.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include "vpe_hw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct vpe;

/** @def MAX_NB_POLYPHASE_COEFFS
 *
 *  @brief Maximum number of filter coefficients for polyphase scaling.
 *  VPE library supports up to 8 taps and 64 phases, only (32+1) phases needed
 */
#define MAX_NB_POLYPHASE_COEFFS (8 * 33)
 
/** @enum vpe_status
 *  @brief The status of VPE to indicate whether it supports the given job or not.
 */
enum vpe_status {
    VPE_STATUS_OK = 1,                          /**<  VPE supports the job. */
    VPE_STATUS_ERROR,                           /**<  Unknown Error in VPE. */
    VPE_STATUS_NO_MEMORY,                       /**<  VPE is out of memory. */
    VPE_STATUS_NOT_SUPPORTED,                   /**<  VPE is out of memory. */
    VPE_STATUS_INPUT_DCC_NOT_SUPPORTED,         /**<  Input DCC is not supported. */
    VPE_STATUS_OUTPUT_DCC_NOT_SUPPORTED,        /**<  Output DCC is not supported. */
    VPE_STATUS_SWIZZLE_NOT_SUPPORTED,           /**<  Swizzle mode is not supported. */
    VPE_STATUS_NUM_STREAM_NOT_SUPPORTED,        /**<  Number of streams is not supported. Too many
                                                   streams. */
    VPE_STATUS_PIXEL_FORMAT_NOT_SUPPORTED,      /**<  Pixel format is not supported. */
    VPE_STATUS_COLOR_SPACE_VALUE_NOT_SUPPORTED, /**<  Input DCC is not supported. */
    VPE_STATUS_SCALING_RATIO_NOT_SUPPORTED,     /**<  Given scaling is not supported. */
    VPE_STATUS_PITCH_ALIGNMENT_NOT_SUPPORTED,   /**<  Given pitch alignment is not supported. */
    VPE_STATUS_ROTATION_NOT_SUPPORTED,          /**<  Given rotation is not supported. */
    VPE_STATUS_MIRROR_NOT_SUPPORTED,            /**<  Given mirror is not supported. */
    VPE_STATUS_ALPHA_BLENDING_NOT_SUPPORTED,    /**<  Alpha blending is not supported. */
    VPE_STATUS_VIEWPORT_SIZE_NOT_SUPPORTED,     /**<  Given viewport size is not supported. */
    VPE_STATUS_LUMA_KEYING_NOT_SUPPORTED,       /**<  Luma keying is not supported. */
    VPE_STATUS_COLOR_KEYING_NOT_SUPPORTED,      /**<  Color keying is not supported. */
    VPE_STATUS_INVALID_KEYER_CONFIG,            /**<  Keying config is invalid. */
    VPE_STATUS_PLANE_ADDR_NOT_SUPPORTED,        /**<  Given plane address is not supported. */
    VPE_STATUS_ADJUSTMENT_NOT_SUPPORTED,        /**<  Color adjustment is not supported. */
    VPE_STATUS_CMD_OVERFLOW_ERROR,              /**<  More than 256 commands/jobs. */
    VPE_STATUS_SEGMENT_WIDTH_ERROR,             /**<  Calculated segment width is not supported. */
    VPE_STATUS_PARAM_CHECK_ERROR,               /**<  Given parametrs is not supported. */
    VPE_STATUS_TONE_MAP_NOT_SUPPORTED,          /**<  Tone mapping is not supported for the given
                                                   job. */
    VPE_STATUS_BAD_TONE_MAP_PARAMS,             /**<  Invalid tone mapping parameters. */
    VPE_STATUS_BAD_HDR_METADATA,                /**<  Ivalid HDR metadata. */
    VPE_STATUS_BUFFER_OVERFLOW,                 /**<  Buffer overflow. */
    VPE_STATUS_BUFFER_UNDERRUN,                 /**<  Buffer does not have enough capacity. */
    VPE_STATUS_BG_COLOR_OUT_OF_RANGE,           /**<  Given backgroud color does not lie in the
                                                   range of output color. */
    VPE_STATUS_REPEAT_ITEM,                     /**<  Descriptor writer is on a repeated job.
                                                   Used internally */
    VPE_STATUS_PATCH_OVER_MAXSIZE,              /**<  Descriptor writer patch size is larger than
                                                   supported path size. */
    VPE_STATUS_INVALID_BUFFER_SIZE,             /**<  Provided buffer size is less than required
                                                   buffer size. */
    VPE_STATUS_SCALER_NOT_SET,                  /**<  Scaler parameters are not set. */
    VPE_STATUS_GEOMETRICSCALING_ERROR,          /**<  Geometric scaling is not supported for the
                                                   given case. */
};

/** @enum vpe_ip_level
 *  @brief HW IP level
 */
enum vpe_ip_level {
    VPE_IP_LEVEL_UNKNOWN = (-1),
    VPE_IP_LEVEL_1_0, /**< vpe 1.0 */
    VPE_IP_LEVEL_1_1, /**< vpe 1.1 */
};

/****************************************
 * Plane Caps
 ****************************************/

/** @struct vpe_pixel_format_support
 *  @brief Capability to support formats
 */
struct vpe_pixel_format_support {
    uint32_t argb_packed_32b : 1; /**< Packed RGBA formats 32-bits per pixel */
    uint32_t nv12            : 1; /**< planar 4:2:0 8-bits */
    uint32_t fp16            : 1; /**< Floating point RGB 16-bits */
    uint32_t p010            : 1; /**< planar 4:2:0 10-bits */
    uint32_t p016            : 1; /**< planar 4:2:0 16-bits */
    uint32_t ayuv            : 1; /**< packed 4:4:4 8-bits */
    uint32_t yuy2            : 1; /**< packed 4:2:2 8-bits */
};

/** @struct vpe_plane_caps
 *  @brief Capability to support given plane
 */
struct vpe_plane_caps {
    uint32_t per_pixel_alpha : 1; /**< Per-pixel alpha */

    struct vpe_pixel_format_support
        input_pixel_format_support;  /**< Input pixel format capability */
    struct vpe_pixel_format_support
        output_pixel_format_support; /**< Output pixel format capability */

    uint32_t max_upscale_factor;     /**< Maximum upscaling factor (dst/src) x 1000.
                                        E.g. 1080p -> 4k is 4000 */
    uint32_t max_downscale_factor;   /**< Maximum downscaling factor (dst/src) x 1000.
                                        E.g. 4k -> 1080p is 250 */

    uint32_t pitch_alignment;        /**< Pitch alignment in bytes */
    uint32_t addr_alignment;         /**< Plane address alignment in bytes */
    uint32_t max_viewport_width;     /**< Maximum viewport size */
};

/*************************
 * Color management caps
 *************************/

/** @struct vpe_rom_curve_caps
 *  @brief Capability to support given transfer function
 */
struct vpe_rom_curve_caps {
    uint32_t srgb     : 1; /**< SRGB Gamma */
    uint32_t bt2020   : 1; /**< BT 2020 */
    uint32_t gamma2_2 : 1; /**< Gamma 2.2 */
    uint32_t pq       : 1; /**< Perceptual Quantizer */
    uint32_t hlg      : 1; /**< Hybrid log-gamma */
};

/** @struct dpp_color_caps
 *  @brief Color management caps for dpp layer
 */
struct dpp_color_caps {
    uint32_t                  pre_csc    : 1;
    uint32_t                  luma_key   : 1;
    uint32_t                  color_key  : 1;
    uint32_t                  dgam_ram   : 1;
    uint32_t                  post_csc   : 1; /**< before gamut remap */
    uint32_t                  gamma_corr : 1;
    uint32_t                  hw_3dlut   : 1;
    uint32_t                  ogam_ram   : 1;
    uint32_t                  ocsc       : 1;
    struct vpe_rom_curve_caps dgam_rom_caps;
};

/** @struct mpc_color_caps
 *  @brief Color management caps for mpc layer
 */
struct mpc_color_caps {
    uint32_t gamut_remap         : 1; /**< Gamut remap */
    uint32_t ogam_ram            : 1; /**< Ogam */
    uint32_t ocsc                : 1; /**< OCSC */
    uint32_t shared_3d_lut       : 1; /**< can be in either dpp or mpc, but single instance */
    uint32_t global_alpha        : 1; /**< e.g. top plane 30 %. bottom 70 % */
    uint32_t top_bottom_blending : 1; /**< two-layer blending */
};

/** @struct vpe_color_caps
 *  @brief VPE color management caps
 */
struct vpe_color_caps {
    struct dpp_color_caps dpp; /**< DPP color caps */
    struct mpc_color_caps mpc; /**< MPC color caps */
};

/**************************************************
 * @struct vpe_caps
 * @brief VPE Capabilities.
 *
 * Those depend on the condition like input format
 * shall be queried by @ref vpe_cap_funcs
 **************************************************/
struct vpe_caps {
    uint32_t max_downscale_ratio; /**< max downscaling ratio (src/dest) x 100.
                                     E.g. 4k -> 1080p is 400 */
    uint64_t lut_size;            /**< 3dlut size */

    uint32_t rotation_support       : 1;
    uint32_t h_mirror_support       : 1;
    uint32_t v_mirror_support       : 1;
    uint32_t is_apu                 : 1;
    uint32_t bg_color_check_support : 1;
    struct {
        uint32_t num_dpp;
        uint32_t num_opp;
        uint32_t num_mpc_3dlut;
        uint32_t num_cdc_be;

        uint32_t num_queue; /**< num of hw queue */
    } resource_caps;

    struct vpe_color_caps color_caps;
    struct vpe_plane_caps plane_caps;

};

/***********************************
 * Conditional Capabilities
 ***********************************/
/** @struct vpe_dcc_surface_param
 *  @brief DCC surface parameters
 */
struct vpe_dcc_surface_param {
    struct vpe_size               surface_size;
    enum vpe_surface_pixel_format format;
    enum vpe_swizzle_mode_values  swizzle_mode;
    enum vpe_scan_direction       scan;
    enum vpe_mirror               mirror;
};

/** @struct vpe_dcc_setting
 *  @brief DCC Settings
 */
struct vpe_dcc_setting {
    unsigned int max_compressed_blk_size;
    unsigned int max_uncompressed_blk_size;
    bool         independent_64b_blks;

    struct {
        uint32_t dcc_256_64_64             : 1;
        uint32_t dcc_128_128_uncontrained  : 1;
        uint32_t dcc_256_128_128           : 1;
        uint32_t dcc_256_256_unconstrained : 1;
    } dcc_controls;
};

/** @struct vpe_surface_dcc_cap
 *  @brief DCC Capabilities
 */
struct vpe_surface_dcc_cap {
    union {
        struct {
            struct vpe_dcc_setting rgb;
        } grph;

        struct {
            struct vpe_dcc_setting luma;
            struct vpe_dcc_setting chroma;
        } video;
    };

    bool capable;
    bool const_color_support;

};

/** @struct vpe_cap_funcs
 *  @brief Conditional Capability functions
 */
struct vpe_cap_funcs {
    /** @brief
     * Get DCC support and setting according to the format,
     * scan direction and swizzle mode for output.
     *
     * @param[in]      vpe           vpe instance
     * @param[in]      params        surface properties
     * @param[in/out]  cap           dcc capable result and related settings
     * @return true if supported
     */
    bool (*get_dcc_compression_output_cap)(const struct vpe *vpe,
        const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap);

    /** @brief
     * Get DCC support and setting according to the format,
     * scan direction and swizzle mode for input.
     *
     * @param[in]      vpe           vpe instance
     * @param[in]      params        surface properties
     * @param[in/out]  cap           dcc capable result and related settings
     * @return true if supported
     */
    bool (*get_dcc_compression_input_cap)(const struct vpe *vpe,
        const struct vpe_dcc_surface_param *params, struct vpe_surface_dcc_cap *cap);
};

/****************************************
 * VPE Init Param
 ****************************************/
/** @brief Log function
 * @param[in] log_ctx  given in the struct @ref vpe_init_data
 * @param[in] fmt      format string
 */
typedef void (*vpe_log_func_t)(void *log_ctx, const char *fmt, ...);

/** @brief system memory zalloc, allocated memory initailized with 0
 *
 * @param[in] mem_ctx  given in the struct @ref vpe_init_data
 * @param[in] size     number of bytes
 * @return             allocated memory
 */
typedef void *(*vpe_zalloc_func_t)(void *mem_ctx, size_t size);

/** @brief system memory free
 * @param[in] mem_ctx  given in the struct @ref vpe_init_data
 * @param[in] ptr      number of bytes
 */
typedef void (*vpe_free_func_t)(void *mem_ctx, void *ptr);

/** @struct vpe_callback_funcs
 *  @brief Callback functions.
 */
struct vpe_callback_funcs {
    void          *log_ctx; /**< optional. provided by the caller and pass back to callback */
    vpe_log_func_t log;     /**< Logging function */

    void             *mem_ctx; /**< optional. provided by the caller and pass back to callback */
    vpe_zalloc_func_t zalloc;  /**< Memory allocation */
    vpe_free_func_t   free;    /**< Free memory. In sync with @ref zalloc */
};

/** @struct vpe_mem_low_power_enable_options
 *  @brief Component activation on low power mode. Only used for debugging.
 */
struct vpe_mem_low_power_enable_options {
    // override flags
    struct {
        uint32_t dscl : 1;
        uint32_t cm   : 1;
        uint32_t mpc  : 1;
    } flags;

    struct {
        uint32_t dscl : 1;
        uint32_t cm   : 1;
        uint32_t mpc  : 1;
    } bits;
};

/** @enum vpe_expansion_mode
 *  @brief Color component expansion mode
 */
enum vpe_expansion_mode {
    VPE_EXPANSION_MODE_DYNAMIC, /**< Dynamic expansion */
    VPE_EXPANSION_MODE_ZERO     /**< Zero expansion */
};

/** @enum vpe_clamping_range
 *  @brief Color clamping
 */
enum vpe_clamping_range {
    VPE_CLAMPING_FULL_RANGE = 0,             /**< No Clamping */
    VPE_CLAMPING_LIMITED_RANGE_8BPC,         /**< 8  bpc: Clamping 1  to FE */
    VPE_CLAMPING_LIMITED_RANGE_10BPC,        /**< 10 bpc: Clamping 4  to 3FB */
    VPE_CLAMPING_LIMITED_RANGE_12BPC,        /**< 12 bpc: Clamping 10 to FEF */
    VPE_CLAMPING_LIMITED_RANGE_PROGRAMMABLE, /**< Programmable. Use programmable clampping value on
                                                FMT_CLAMP_COMPONENT_R/G/B. */
};

/** @struct vpe_clamping_params
 *  @brief Upper and lower bound of each color channel for clamping.
 */
struct vpe_clamping_params {
    enum vpe_clamping_range clamping_range;
    uint32_t                r_clamp_component_upper; /**< Red channel upper bound */
    uint32_t                b_clamp_component_upper; /**< Blue channel upper bound */
    uint32_t                g_clamp_component_upper; /**< Green channel upper bound */
    uint32_t                r_clamp_component_lower; /**< Red channel lower bound */
    uint32_t                b_clamp_component_lower; /**< Blue channel lower bound */
    uint32_t                g_clamp_component_lower; /**< Green channel lower bound */
};

/** @struct vpe_visual_confirm
 *  @brief Configurable parameters for visual confirm bar
 */
struct vpe_visual_confirm {
    union {
        struct {
            uint32_t input_format  : 1;
            uint32_t output_format : 1;
            uint32_t reserved      : 30;
        };
        uint32_t value;
    };
};

/** @struct vpe_debug_options
 *  @brief Configurable parameters for debugging purpose
 */
struct vpe_debug_options {
    // override flags
    struct {
        uint32_t cm_in_bypass            : 1;
        uint32_t vpcnvc_bypass           : 1;
        uint32_t mpc_bypass              : 1;
        uint32_t identity_3dlut          : 1;
        uint32_t sce_3dlut               : 1;
        uint32_t disable_reuse_bit       : 1;
        uint32_t bg_color_fill_only      : 1;
        uint32_t assert_when_not_support : 1;
        uint32_t bypass_gamcor           : 1;
        uint32_t bypass_ogam             : 1;
        uint32_t bypass_dpp_gamut_remap  : 1;
        uint32_t bypass_post_csc         : 1;
        uint32_t bypass_blndgam          : 1;
        uint32_t clamping_setting        : 1;
        uint32_t expansion_mode          : 1;
        uint32_t bypass_per_pixel_alpha  : 1;
        uint32_t dpp_crc_ctrl            : 1;
        uint32_t opp_pipe_crc_ctrl       : 1;
        uint32_t mpc_crc_ctrl            : 1;
        uint32_t bg_bit_depth            : 1;
        uint32_t visual_confirm          : 1;
        uint32_t skip_optimal_tap_check  : 1;
        uint32_t disable_lut_caching     : 1;
    } flags;

    // valid only if the corresponding flag is set
    uint32_t cm_in_bypass            : 1;
    uint32_t vpcnvc_bypass           : 1;
    uint32_t mpc_bypass              : 1;
    uint32_t identity_3dlut          : 1;
    uint32_t sce_3dlut               : 1;
    uint32_t disable_reuse_bit       : 1;
    uint32_t bg_color_fill_only      : 1;
    uint32_t assert_when_not_support : 1;
    uint32_t bypass_gamcor           : 1;
    uint32_t bypass_ogam             : 1;
    uint32_t bypass_dpp_gamut_remap  : 1;
    uint32_t bypass_post_csc         : 1;
    uint32_t bypass_blndgam          : 1;
    uint32_t clamping_setting        : 1;
    uint32_t bypass_per_pixel_alpha  : 1;
    uint32_t dpp_crc_ctrl            : 1;
    uint32_t opp_pipe_crc_ctrl       : 1;
    uint32_t mpc_crc_ctrl            : 1;
    uint32_t skip_optimal_tap_check  : 1;
    uint32_t disable_lut_caching     : 1; /*< disable config caching for all luts */

    uint32_t bg_bit_depth;

    struct vpe_mem_low_power_enable_options enable_mem_low_power;
    enum vpe_expansion_mode                 expansion_mode;
    struct vpe_clamping_params              clamping_params;
    struct vpe_visual_confirm               visual_confirm_params;
};

/** @struct vpe_init_data
 *  @brief VPE ip info and debug/callback functions
 */
struct vpe_init_data {

    uint8_t                   ver_major; /**< vpe major version */
    uint8_t                   ver_minor; /**< vpe minor version */
    uint8_t                   ver_rev;   /**< vpe revision version */
    struct vpe_callback_funcs funcs;     /**< function callbacks */
    struct vpe_debug_options  debug;     /**< debug options */
};

/** @struct vpe
 *  @brief VPE instance created through vpelib entry function vpe_create()
 */
struct vpe {
    uint32_t          version;       /**< API version */
    enum vpe_ip_level level;         /**< HW IP level */

    struct vpe_caps      *caps;      /**< general static chip caps */
    struct vpe_cap_funcs *cap_funcs; /**< conditional caps */
};

/*****************************************************
 * Structures for build VPE command
 *****************************************************/

/** @enum vpe_pixel_encoding
 *  @brief Color space format
 */
enum vpe_pixel_encoding {
    VPE_PIXEL_ENCODING_YCbCr, /**< YCbCr Color space format */
    VPE_PIXEL_ENCODING_RGB,   /**< RGB Color space format */
    VPE_PIXEL_ENCODING_COUNT
};

/** @enum vpe_color_range
 *  @brief Color Range
 */
enum vpe_color_range {
    VPE_COLOR_RANGE_FULL,   /**< Full range */
    VPE_COLOR_RANGE_STUDIO, /**< Studio/limited range */
    VPE_COLOR_RANGE_COUNT
};

/** @enum vpe_chroma_cositing
 *  @brief Chroma Cositing
 *
 * The position of the chroma for the chroma sub-sampled pixel formats.
 */
enum vpe_chroma_cositing {
    VPE_CHROMA_COSITING_NONE,    /**< No cositing */
    VPE_CHROMA_COSITING_LEFT,    /**< Left cositing */
    VPE_CHROMA_COSITING_TOPLEFT, /**< Top-left cositing */
    VPE_CHROMA_COSITING_COUNT
};

/** @enum vpe_color_primaries
 *  @brief Color Primaries
 */
enum vpe_color_primaries {
    VPE_PRIMARIES_BT601,  /**< BT. 601, Rec. 601 */
    VPE_PRIMARIES_BT709,  /**< BT. 709, Rec. 709 */
    VPE_PRIMARIES_BT2020, /**< BT. 2020, Rec. 2020 */
    VPE_PRIMARIES_JFIF,   /**< JPEG File Interchange Format */
    VPE_PRIMARIES_COUNT
};

/** @enum vpe_transfer_function
 *  @brief Gamma Transfer function
 */
enum vpe_transfer_function {
    VPE_TF_G22,           /**< Gamma 2.2 */
    VPE_TF_G24,           /**< Gamma 2.4 */
    VPE_TF_G10,           /**< Linear */
    VPE_TF_PQ,            /**< Perceptual Quantizer */
    VPE_TF_PQ_NORMALIZED, /**< Normalized Perceptual Quantizer */
    VPE_TF_HLG,           /**< Hybrid Log-Gamma */
    VPE_TF_SRGB,          /**< Standard RGB */
    VPE_TF_BT709,         /**< BT 709 */
    VPE_TF_COUNT
};

/** @enum vpe_alpha_mode
 *  @brief Alpha mode of the stream.
 */
enum vpe_alpha_mode {
    VPE_ALPHA_OPAQUE, /**< Opaque. In this mode, If output has alpha channel, it is set to
                       * maximum value. For FP16 format it is set to 125.0f,
                       * and 2^(AlphaChannelBitDepth)-1 for other formats.
                       */
    VPE_ALPHA_BGCOLOR /**< If the output has alpha channel, sets the output alpha to be the
                       * alpha value of the user-provided background color.
                       */
};

/** @struct vpe_color_space
 *  @brief Color space parameters.
 */
struct vpe_color_space {
    enum vpe_pixel_encoding    encoding;  /**< Color space format. RGBA vs. YCbCr */
    enum vpe_color_range       range;     /**< Color range. Full vs. Studio */
    enum vpe_transfer_function tf;        /**< Transfer Function/Gamma */
    enum vpe_chroma_cositing   cositing;  /**< Chroma Cositing */
    enum vpe_color_primaries   primaries; /**< Color primaries */
};

/** @struct vpe_color_rgba
 *  @brief Color value of each channel for RGBA color space formats.
 *
 *  Component values are in the range: 0.0f - 1.0f
 */
struct vpe_color_rgba {
    float r; /**< Red Channel*/
    float g; /**< Green Channel*/
    float b; /**< Blue Channel*/
    float a; /**< Alpha Channel*/
};

/** @struct vpe_color_ycbcra
 *  @brief Color value of each channel for YCbCr color space formats.
 *
 *  Component values are in the range: 0.0f - 1.0f
 */
struct vpe_color_ycbcra {
    float y;  /**< Luminance/Luma Channel */
    float cb; /**< Blue-difference Chrominance/Chroma Channel */
    float cr; /**< Red-difference Chrominance/Chroma Channel */
    float a;  /**< Alpha Channel */
};

/** @struct vpe_color
 *  @brief Color value of each pixel
 */
struct vpe_color {
    bool is_ycbcr;                      /**< Set if the color space format is YCbCr.
                                           If Ture, use @ref ycbcra. If False, use @ref rgba. */
    union {
        struct vpe_color_rgba   rgba;   /**< RGBA value */
        struct vpe_color_ycbcra ycbcra; /**< YCbCr value */
    };
};

/** @struct vpe_color_adjust
 * @brief Color adjustment values
 * <pre>
 * Adjustment     Min      Max    default   step
 *
 * Brightness  -100.0f,  100.0f,   0.0f,    0.1f
 *
 * Contrast       0.0f,    2.0f,    1.0f,   0.01f
 *
 * Hue         -180.0f,  180.0f,   0.0f,    1.0f
 *
 * Saturation     0.0f,    3.0f,   1.0f,    0.01f
 * </pre>
 */
struct vpe_color_adjust {
    float brightness; /**< Brightness */
    float contrast;   /**< Contrast */
    float hue;        /**< Hue */
    float saturation; /**< Saturation */
};

/** @struct vpe_surface_info
 *  @brief Surface address and properties
 *
 */
struct vpe_surface_info {

    struct vpe_plane_address     address;     /**< Address */
    enum vpe_swizzle_mode_values swizzle;     /**< Swizzle mode */

    struct vpe_plane_size         plane_size; /**< Pitch */
    struct vpe_plane_dcc_param    dcc;
    enum vpe_surface_pixel_format format;     /**< Surface pixel format */

    struct vpe_color_space cs;                /**< Surface color space */
};

struct vpe_blend_info {
    bool  blending;             /**< Enable blending */
    bool  pre_multiplied_alpha; /**< Is the pixel value pre-multiplied with alpha */
    bool  global_alpha;         /**< Enable global alpha */
    float global_alpha_value;   /**< Global alpha value. In range of 0.0-1.0 */
};

/** @struct vpe_scaling_info
 *  @brief Data needs to calculate scaling data.
 */
struct vpe_scaling_info {
    struct vpe_rect         src_rect; /**< Input frame/stream rectangle*/
    struct vpe_rect         dst_rect; /**< Output rectangle on the destination surface. */
    struct vpe_scaling_taps taps;     /**< Number of taps to be used for scaler.
                                       * If taps are set to 0, vpe internally calculates the
                                       * required number of taps based on the scaling ratio.
                                       */

};

/** @struct vpe_scaling_filter_coeffs
 *  @brief Filter coefficients for polyphase scaling
 *
 *  If the number of taps are set to be 0, vpe internally calculates the number of taps and filter
 * coefficients based on the scaling ratio.
 */
struct vpe_scaling_filter_coeffs {

    struct vpe_scaling_taps taps;                             /**< Number of taps for polyphase
                                                                 scaling */
    unsigned int nb_phases;                                   /**< Number of phases for polyphase
                                                                 scaling */
    uint16_t horiz_polyphase_coeffs[MAX_NB_POLYPHASE_COEFFS]; /**< Filter coefficients for
                                                                 horizontal polyphase scaling */
    uint16_t vert_polyphase_coeffs[MAX_NB_POLYPHASE_COEFFS];  /**< Filter coefficients for
                                                                 vertical polyphase scaling */
};

/** @struct vpe_hdr_metadata
 *  @brief HDR metadata
 */
struct vpe_hdr_metadata {
    uint16_t redX;          /**< Red point chromaticity X-value */
    uint16_t redY;          /**< Red point chromaticity Y-value */
    uint16_t greenX;        /**< Green point chromaticity X-value */
    uint16_t greenY;        /**< Green point chromaticity Y-value */
    uint16_t blueX;         /**< Blue point chromaticity X-value */
    uint16_t blueY;         /**< Blue point chromaticity Y-value */
    uint16_t whiteX;        /**< White point chromaticity X-value */
    uint16_t whiteY;        /**< White point chromaticity Y-value */

    uint32_t min_mastering; /**< Minimum luminance for HDR frame/stream in 1/10000 nits */
    uint32_t max_mastering; /**< Maximum luminance for HDR frame/stream in nits */
    uint32_t max_content;   /**< Maximum stream's content light level */
    uint32_t avg_content;   /**< Frame's average light level */
};

struct vpe_reserved_param {
    void    *param;
    uint32_t size;
};

/** @struct vpe_tonemap_params
 *  @brief Tone mapping parameters
 */
struct vpe_tonemap_params {
    uint64_t UID;                                    /**< Unique ID for tonemap params provided by
                                                      * user. If tone mapping is not needed, set
                                                      * to 0, otherwise, each update to the
                                                      * tonemap parameter should use a new ID to
                                                      * signify a tonemap update.
                                                      */
    enum vpe_transfer_function shaper_tf;            /**< Shaper LUT transfer function */
    enum vpe_transfer_function lut_out_tf;           /**< Output transfer function */
    enum vpe_color_primaries   lut_in_gamut;         /**< Input color primary */
    enum vpe_color_primaries   lut_out_gamut;        /**< Output color primary */
    uint16_t                   input_pq_norm_factor; /**< Perceptual Quantizer normalization
                                                        factor. */
    uint16_t lut_dim;                                /**< Size of one dimension of the 3D-LUT */
    union {
        uint16_t *lut_data;                          /**< Accessible to CPU */
        void     *dma_lut_data;                      /**< Accessible to GPU. Only for fast load */
    };
    bool is_dma_lut;
    bool enable_3dlut; /**< Enable/Disable 3D-LUT */
};

/** @enum vpe_keyer_mode
 *  @brief Dictates the behavior of keyer's generated alpha
 */
enum vpe_keyer_mode {
    VPE_KEYER_MODE_RANGE_00 = 0, /**< (Default) if in range -> generated alpha = 00 */
    VPE_KEYER_MODE_RANGE_FF,     /**< if in_range -> generated alpha = FF */
    VPE_KEYER_MODE_FORCE_00,     /**< ignore range setting, force generating alpha = 00 */
    VPE_KEYER_MODE_FORCE_FF,     /**< ignore range setting, force generating alpha = FF */
};

/** @enum vpe_color_keyer
 *  @brief Input Parameters for Color keyer.
 *  bounds should be programmed to 0.0 <= 1.0, with lower < upper
 *  if format does not have alpha (RGBx) when using the color keyer, alpha should be programmed to
 *  lower=0.0, upper=1.0
 */
struct vpe_color_keyer {
    bool  enable_color_key; /**< Enable Color Key. Mutually Exclusive with Luma Key */
    float lower_g_bound;    /**< Green Low Bound.  */
    float upper_g_bound;    /**< Green High Bound. */
    float lower_b_bound;    /**< Blue Low Bound.   */
    float upper_b_bound;    /**< Blue High Bound.  */
    float lower_r_bound;    /**< Red Low Bound.    */
    float upper_r_bound;    /**< Red High Bound.   */
    float lower_a_bound; /**< Alpha Low Bound. Program 0.0f if no alpha channel in input format.*/
    float upper_a_bound; /**< Alpha High Bound. Program 1.0f if no alpha channel in input format.*/
};
/** @struct vpe_stream
 *  @brief Input stream/frame properties to be passed to vpelib
 */
struct vpe_stream {
    struct vpe_surface_info surface_info;                      /**< Stream plane information. */
    struct vpe_scaling_info scaling_info;                      /**< Scaling information. */
    struct vpe_blend_info   blend_info;                        /**< Alpha blending */
    struct vpe_color_adjust color_adj;                         /**< Color adjustment. Brightness,
                                                                  contrast, hue and saturation.*/
    struct vpe_tonemap_params        tm_params;                /**< Tone mapping parameters*/
    struct vpe_hdr_metadata          hdr_metadata;             /**< HDR metadata */
    struct vpe_scaling_filter_coeffs polyphase_scaling_coeffs; /**< Filter coefficients for
                                                                  polyphase scaling. */
    enum vpe_rotation_angle rotation;                          /**< Rotation angle of the
                                                                  stream/frame */
    bool horizontal_mirror;                                    /**< Set if the stream is flipped
                                                                  horizontally */
    bool vertical_mirror;                                      /**< Set if the stream is flipped
                                                                  vertically */
    bool use_external_scaling_coeffs;                          /**< Use provided polyphase scaling
                                                                * filter coefficients.
                                                                * See @ref vpe_scaling_filter_coeffs
                                                                */
    bool enable_luma_key;                                      /**< Enable luma keying. Only
                                                                * works if vpe version supports
                                                                * luma keying.
                                                                */
    float lower_luma_bound;                                    /**< Lowest range of the luma */
    float upper_luma_bound;                                    /**< Highest range of the luma */
    struct vpe_color_keyer color_keyer; /**< Enable Luma Keying & Set Parameters. */
    enum vpe_keyer_mode    keyer_mode;  /**< Set Keyer Behavior.
                                         * Used for both Luma & Color Keying.
                                         */
    struct vpe_reserved_param        reserved_param;

    struct {
        uint32_t hdr_metadata : 1;
        uint32_t geometric_scaling : 1; /**< Enables geometric scaling.
                                         * Support 1 input stream only.
                                         * If set, gamut/gamma remapping will be disabled,
                                         * as well as blending.
                                         * Destination rect must equal to target rect.
                                         */
        uint32_t reserved : 30;
    } flags;
};

/** @struct vpe_build_param
 *  @brief Build parametrs for vpelib. Must get populated before vpe_check_support() call.
 */
struct vpe_build_param {

    uint32_t                num_streams;    /**< Number of source streams */
    struct vpe_stream      *streams;        /**< List of input streams */
    struct vpe_surface_info dst_surface;    /**< Destination/Output surface */
    struct vpe_rect         target_rect;    /**< rectangle in target surface to be blt'd.
                                               Ranges out of target_rect won't be touched */
    struct vpe_color    bg_color;           /**< Background Color */
    enum vpe_alpha_mode alpha_mode;         /**< Alpha Mode. Output alpha in the output
                                               surface */
    struct vpe_hdr_metadata   hdr_metadata; /**< HDR Metadata */
    struct vpe_reserved_param dst_reserved_param;

    // data flags
    struct {
        uint32_t hdr_metadata : 1;
        uint32_t reserved     : 31;
    } flags;

    uint16_t num_instances;      /**< Number of instances for the collaboration mode */
    bool     collaboration_mode; /**< Collaboration mode. If set, multiple instances of VPE being
                                    used. */
};

/** @struct vpe_bufs_req
 *  @brief Command buffer and Embedded buffer required sizes reported through vpe_check_support()
 *
 * Once the operation is supported,
 * it returns the required memory for storing
 * 1. command buffer
 * 2. embedded buffer
 *    - Pointed by the command buffer content.
 *    - Shall be free'ed together with command buffer once
 *      command is finished.
 */
struct vpe_bufs_req {
    uint64_t cmd_buf_size; /**< total command buffer size for all vpe commands */
    uint64_t emb_buf_size; /**< total size for storing all embedded data */
};

/** @struct vpe_buf
 *  @brief Buffer information
 */
struct vpe_buf {
    uint64_t gpu_va; /**< GPU start address of the buffer */
    uint64_t cpu_va; /**< CPU start address of the buffer */
    uint64_t size;   /**< Size of the buffer */
    bool     tmz;    /**< allocated from tmz */
};

/** @struct vpe_build_bufs
 *  @brief Command buffer and Embedded buffer
 */
struct vpe_build_bufs {
    struct vpe_buf cmd_buf; /**< Command buffer. gpu_va is optional */
    struct vpe_buf emb_buf; /**< Embedded buffer */
};

#ifdef __cplusplus
}
#endif
