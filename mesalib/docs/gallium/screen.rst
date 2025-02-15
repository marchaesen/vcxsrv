.. _screen:

Screen
======

A screen is an object representing the context-independent part of a device.

Flags and enumerations
----------------------

XXX some of these don't belong in this section.


.. _pipe_caps:

pipe_caps
^^^^^^^^^^

Capability about the features and limits of the driver/GPU.

* ``pipe_caps.graphics``: Whether graphics is supported. If not, contexts can
  only be created with PIPE_CONTEXT_COMPUTE_ONLY.
* ``pipe_caps.npot_textures``: Whether :term:`NPOT` textures may have repeat modes,
  normalized coordinates, and mipmaps.
* ``pipe_caps.max_dual_source_render_targets``: How many dual-source blend RTs are support.
  :ref:`Blend` for more information.
* ``pipe_caps.anisotropic_filter``: Whether textures can be filtered anisotropically.
* ``pipe_caps.max_render_targets``: The maximum number of render targets that may be
  bound.
* ``pipe_caps.occlusion_query``: Whether occlusion queries are available.
* ``pipe_caps.query_time_elapsed``: Whether PIPE_QUERY_TIME_ELAPSED queries are available.
* ``pipe_caps.texture_shadow_map``: indicates whether the fragment shader hardware
  can do the depth texture / Z comparison operation in TEX instructions
  for shadow testing.
* ``pipe_caps.texture_swizzle``: Whether swizzling through sampler views is
  supported.
* ``pipe_caps.max_texture_2d_size``: The maximum size of 2D (and 1D) textures.
* ``pipe_caps.max_texture_3d_levels``: The maximum number of mipmap levels available
  for a 3D texture.
* ``pipe_caps.max_texture_cube_levels``: The maximum number of mipmap levels available
  for a cubemap.
* ``pipe_caps.texture_mirror_clamp_to_edge``: Whether mirrored texture coordinates are
  supported with the clamp-to-edge wrap mode.
* ``pipe_caps.texture_mirror_clamp``: Whether mirrored texture coordinates are supported
  with clamp or clamp-to-border wrap modes.
* ``pipe_caps.blend_equation_separate``: Whether alpha blend equations may be different
  from color blend equations, in :ref:`Blend` state.
* ``pipe_caps.max_stream_output_buffers``: The maximum number of stream buffers.
* ``pipe_caps.primitive_restart``: Whether primitive restart is supported.
* ``pipe_caps.primitive_restart_fixed_index``: Subset of
  PRIMITIVE_RESTART where the restart index is always the fixed maximum
  value for the index type.
* ``pipe_caps.indep_blend_enable``: Whether per-rendertarget blend enabling and channel
  masks are supported. If 0, then the first rendertarget's blend mask is
  replicated across all MRTs.
* ``pipe_caps.indep_blend_func``: Whether per-rendertarget blend functions are
  available. If 0, then the first rendertarget's blend functions affect all
  MRTs.
* ``pipe_caps.max_texture_array_layers``: The maximum number of texture array
  layers supported. If 0, the array textures are not supported at all and
  the ARRAY texture targets are invalid.
* ``pipe_caps.fs_coord_origin_upper_left``: Whether the upper-left origin
  fragment convention is supported.
* ``pipe_caps.fs_coord_origin_lower_left``: Whether the lower-left origin
  fragment convention is supported.
* ``pipe_caps.fs_coord_pixel_center_half_integer``: Whether the half-integer
  pixel-center fragment convention is supported.
* ``pipe_caps.fs_coord_pixel_center_integer``: Whether the integer
  pixel-center fragment convention is supported.
* ``pipe_caps.depth_clip_disable``: Whether the driver is capable of disabling
  depth clipping (through pipe_rasterizer_state).
* ``pipe_caps.depth_clip_disable_separate``: Whether the driver is capable of
  disabling depth clipping (through pipe_rasterizer_state) separately for
  the near and far plane. If not, depth_clip_near and depth_clip_far will be
  equal.
  ``pipe_caps.depth_clamp_enable``: Whether the driver is capable of
  enabling depth clamping (through pipe_rasterizer_state) separately from depth
  clipping. If not, depth_clamp will be the inverse of depth_clip_far.
* ``pipe_caps.shader_stencil_export``: Whether a stencil reference value can be
  written from a fragment shader.
* ``pipe_caps.vs_instanceid``: Whether ``SYSTEM_VALUE_INSTANCE_ID`` is
  supported in the vertex shader.
* ``pipe_caps.vertex_element_instance_divisor``: Whether the driver supports
  per-instance vertex attribs.
* ``pipe_caps.fragment_color_clamped``: Whether fragment color clamping is
  supported.  That is, is the pipe_rasterizer_state::clamp_fragment_color
  flag supported by the driver?  If not, gallium frontends will insert
  clamping code into the fragment shaders when needed.

* ``pipe_caps.mixed_colorbuffer_formats``: Whether mixed colorbuffer formats are
  supported, e.g. RGBA8 and RGBA32F as the first and second colorbuffer, resp.
* ``pipe_caps.vertex_color_unclamped``: Whether the driver is capable of
  outputting unclamped vertex colors from a vertex shader. If unsupported,
  the vertex colors are always clamped. This is the default for DX9 hardware.
* ``pipe_caps.vertex_color_clamped``: Whether the driver is capable of
  clamping vertex colors when they come out of a vertex shader, as specified
  by the pipe_rasterizer_state::clamp_vertex_color flag.  If unsupported,
  the vertex colors are never clamped. This is the default for DX10 hardware.
  If both clamped and unclamped CAPs are supported, the clamping can be
  controlled through pipe_rasterizer_state.  If the driver cannot do vertex
  color clamping, gallium frontends may insert clamping code into the vertex
  shader.
* ``pipe_caps.glsl_feature_level``: Whether the driver supports features
  equivalent to a specific GLSL version. E.g. for GLSL 1.3, report 130.
* ``pipe_caps.glsl_feature_level_compatibility``: Whether the driver supports
  features equivalent to a specific GLSL version including all legacy OpenGL
  features only present in the OpenGL compatibility profile.
  The only legacy features that Gallium drivers must implement are
  the legacy shader inputs and outputs (colors, texcoords, fog, clipvertex,
  edge flag).
* ``pipe_caps.essl_feature_level``: An optional cap to allow drivers to
  report a higher GLSL version for GLES contexts.  This is useful when a
  driver does not support all the required features for a higher GL version,
  but does support the required features for a higher GLES version.  A driver
  is allowed to return ``0`` in which case ``pipe_caps.glsl_feature_level`` is
  used.
  Note that simply returning the same value as the GLSL feature level cap is
  incorrect.  For example, GLSL version 3.30 does not require
  :ext:`GL_EXT_gpu_shader5`, but ESSL version 3.20 es does require
  :ext:`GL_EXT_gpu_shader5`
* ``pipe_caps.quads_follow_provoking_vertex_convention``: Whether quads adhere to
  the flatshade_first setting in ``pipe_rasterizer_state``.
* ``pipe_caps.user_vertex_buffers``: Whether the driver supports user vertex
  buffers.  If not, gallium frontends must upload all data which is not in HW
  resources.  If user-space buffers are supported, the driver must also still
  accept HW resource buffers.
* ``pipe_caps.vertex_input_alignment``: This CAP describes a HW
  limitation.
  If ``PIPE_VERTEX_INPUT_ALIGNMENT_4BYTE```,
  pipe_vertex_buffer::buffer_offset must always be aligned
  to 4, and pipe_vertex_buffer::stride must always be aligned to 4,
  and pipe_vertex_element::src_offset must always be
  aligned to 4.
  If ``PIPE_VERTEX_INPUT_ALIGNMENT_ELEMENT``,
  the sum of
  ``pipe_vertex_element::src_offset + pipe_vertex_buffer::buffer_offset + pipe_vertex_buffer::stride``
  must always be aligned to the component size for the vertex attributes
  which access that buffer.
  If ``PIPE_VERTEX_INPUT_ALIGNMENT_NONE``, there are no restrictions on these values.
* ``pipe_caps.compute``: Whether the implementation supports the
  compute entry points defined in pipe_context and pipe_screen.
* ``pipe_caps.constant_buffer_offset_alignment``: Describes the required
  alignment of pipe_constant_buffer::buffer_offset.
* ``pipe_caps.start_instance``: Whether the driver supports
  pipe_draw_info::start_instance.
* ``pipe_caps.query_timestamp``: Whether PIPE_QUERY_TIMESTAMP and
  the pipe_screen::get_timestamp hook are implemented.
* ``pipe_caps.query_timestamp_bits``: How many bits the driver uses for the
  results of GL_TIMESTAMP queries.
* ``pipe_caps.timer_resolution``: The resolution of the timer in nanos.
* ``pipe_caps.texture_multisample``: Whether all MSAA resources supported
  for rendering are also supported for texturing.
* ``pipe_caps.min_map_buffer_alignment``: The minimum alignment that should be
  expected for a pointer returned by transfer_map if the resource is
  PIPE_BUFFER. In other words, the pointer returned by transfer_map is
  always aligned to this value.
* ``pipe_caps.texture_buffer_offset_alignment``: Describes the required
  alignment for pipe_sampler_view::u.buf.offset, in bytes.
  If a driver does not support offset/size, it should return 0.
* ``pipe_caps.linear_image_pitch_alignment``: Describes the row pitch alignment
  size that pipe_sampler_view::u.tex2d_from_buf must be multiple of, in pixels.
  If a driver does not support images created from buffers, it should return 0.
* ``pipe_caps.linear_image_base_address_alignment``: Describes the minimum alignment
  in pixels of the offset of a host pointer for images created from buffers.
  If a driver does not support images created from buffers, it should return 0.
* ``pipe_caps.buffer_sampler_view_rgba_only``: Whether the driver only
  supports R, RG, RGB and RGBA formats for PIPE_BUFFER sampler views.
  When this is the case it should be assumed that the swizzle parameters
  in the sampler view have no effect.
* ``pipe_caps.tgsi_texcoord``: This CAP describes a HW limitation.
  If true, the hardware cannot replace arbitrary shader inputs with sprite
  coordinates and hence the inputs that are desired to be replaceable must
  be declared with TGSI_SEMANTIC_TEXCOORD instead of TGSI_SEMANTIC_GENERIC.
  The rasterizer's sprite_coord_enable state therefore also applies to the
  TEXCOORD semantic.
  Also, TGSI_SEMANTIC_PCOORD becomes available, which labels a fragment shader
  input that will always be replaced with sprite coordinates.
* ``pipe_caps.texture_transfer_modes``: The ``pipe_texture_transfer_mode`` modes
  that are supported for implementing a texture transfer which needs format conversions
  and swizzling in gallium frontends. Generally, all hardware drivers with
  dedicated memory should return PIPE_TEXTURE_TRANSFER_BLIT and all software rasterizers
  should return PIPE_TEXTURE_TRANSFER_DEFAULT. PIPE_TEXTURE_TRANSFER_COMPUTE requires drivers
  to support 8bit and 16bit shader storage buffer writes and to implement
  pipe_screen::is_compute_copy_faster.
* ``pipe_caps.query_pipeline_statistics``: Whether PIPE_QUERY_PIPELINE_STATISTICS
  is supported.
* ``pipe_caps.texture_border_color_quirk``: Bitmask indicating whether special
  considerations have to be given to the interaction between the border color
  in the sampler object and the sampler view used with it.
  If PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_R600 is set, the border color
  may be affected in undefined ways for any kind of permutational swizzle
  (any swizzle XYZW where X/Y/Z/W are not ZERO, ONE, or R/G/B/A respectively)
  in the sampler view.
  If PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_NV50 is set, the border color
  state should be swizzled manually according to the swizzle in the sampler
  view it is intended to be used with, or herein undefined results may occur
  for permutational swizzles.
* ``pipe_caps.max_texel_buffer_elements``: The maximum accessible number of
  elements within a sampler buffer view and image buffer view. This is unsigned
  integer with the maximum of 4G - 1.
* ``pipe_caps.max_viewports``: The maximum number of viewports (and scissors
  since they are linked) a driver can support. Returning 0 is equivalent
  to returning 1 because every driver has to support at least a single
  viewport/scissor combination.
* ``pipe_caps.endianness``:: The endianness of the device.  Either
  PIPE_ENDIAN_BIG or PIPE_ENDIAN_LITTLE.
* ``pipe_caps.mixed_framebuffer_sizes``: Whether it is allowed to have
  different sizes for fb color/zs attachments. This controls whether
  :ext:`GL_ARB_framebuffer_object` is provided.
* ``pipe_caps.vs_layer_viewport``: Whether ``VARYING_SLOT_LAYER`` and
  ``VARYING_SLOT_VIEWPORT`` are supported as vertex shader outputs. Note that
  the viewport will only be used if multiple viewports are exposed.
* ``pipe_caps.max_geometry_output_vertices``: The maximum number of vertices
  output by a single invocation of a geometry shader.
* ``pipe_caps.max_geometry_total_output_components``: The maximum number of
  vertex components output by a single invocation of a geometry shader.
  This is the product of the number of attribute components per vertex and
  the number of output vertices.
* ``pipe_caps.max_texture_gather_components``: Max number of components
  in format that texture gather can operate on. 1 == RED, ALPHA etc,
  4 == All formats.
* ``pipe_caps.texture_gather_sm5``: Whether the texture gather
  hardware implements the SM5 features, component selection,
  shadow comparison, and run-time offsets.
* ``pipe_caps.buffer_map_persistent_coherent``: Whether
  PIPE_MAP_PERSISTENT and PIPE_MAP_COHERENT are supported
  for buffers.
* ``pipe_caps.texture_query_lod``: Whether the ``LODQ`` instruction is
  supported.
* ``pipe_caps.min_texture_gather_offset``: The minimum offset that can be used
  in conjunction with a texture gather opcode.
* ``pipe_caps.max_texture_gather_offset``: The maximum offset that can be used
  in conjunction with a texture gather opcode.
* ``pipe_caps.sample_shading``: Whether there is support for per-sample
  shading. The context->set_min_samples function will be expected to be
  implemented.
* ``pipe_caps.texture_gather_offsets``: Whether the ``TG4`` instruction can
  accept 4 offsets.
* ``pipe_caps.vs_window_space_position``: Whether window-space position is
  supported, which disables clipping and viewport transformation.
* ``pipe_caps.max_vertex_streams``: The maximum number of vertex streams
  supported by the geometry shader. If stream-out is supported, this should be
  at least 1. If stream-out is not supported, this should be 0.
* ``pipe_caps.draw_indirect``: Whether the driver supports taking draw arguments
  { count, instance_count, start, index_bias } from a PIPE_BUFFER resource.
  See pipe_draw_info.
* ``pipe_caps.multi_draw_indirect``: Whether the driver supports
  pipe_draw_info::indirect_stride and ::indirect_count
* ``pipe_caps.multi_draw_indirect_params``: Whether the driver supports
  taking the number of indirect draws from a separate parameter
  buffer, see pipe_draw_indirect_info::indirect_draw_count.
* ``pipe_caps.multi_draw_indirect_partial_stride``: Whether the driver supports
  indirect draws with an arbitrary stride.
* ``pipe_caps.fs_fine_derivative``: Whether the fragment shader supports
  the FINE versions of DDX/DDY.
* ``pipe_caps.vendor_id``: The vendor ID of the underlying hardware. If it's
  not available one should return 0xFFFFFFFF.
* ``pipe_caps.device_id``: The device ID (PCI ID) of the underlying hardware.
  0xFFFFFFFF if not available.
* ``pipe_caps.accelerated``: Whether the renderer is hardware accelerated. 0 means
  not accelerated (i.e. CPU rendering), 1 means accelerated (i.e. GPU rendering),
  -1 means unknown (i.e. an API translation driver which doesn't known what kind of
  hardware it's running above).
* ``pipe_caps.video_memory``: The amount of video memory in megabytes.
* ``pipe_caps.uma``: If the device has a unified memory architecture or on-card
  memory and GART.
* ``pipe_caps.conditional_render_inverted``: Whether the driver supports inverted
  condition for conditional rendering.
* ``pipe_caps.max_vertex_attrib_stride``: The maximum supported vertex stride.
* ``pipe_caps.sampler_view_target``: Whether the sampler view's target can be
  different than the underlying resource's, as permitted by
  :ext:`GL_ARB_texture_view`. For example a 2d array texture may be reinterpreted as a
  cube (array) texture and vice-versa.
* ``pipe_caps.clip_halfz``: Whether the driver supports the
  pipe_rasterizer_state::clip_halfz being set to true. This is required
  for enabling :ext:`GL_ARB_clip_control`.
* ``pipe_caps.polygon_offset_clamp``: If true, the driver implements support
  for ``pipe_rasterizer_state::offset_clamp``.
* ``pipe_caps.multisample_z_resolve``: Whether the driver supports blitting
  a multisampled depth buffer into a single-sampled texture (or depth buffer).
  Only the first sampled should be copied.
* ``pipe_caps.resource_from_user_memory``: Whether the driver can create
  a pipe_resource where an already-existing piece of (malloc'd) user memory
  is used as its backing storage. In other words, whether the driver can map
  existing user memory into the device address space for direct device access.
  The create function is pipe_screen::resource_from_user_memory. The address
  and size must be page-aligned.
* ``pipe_caps.resource_from_user_memory_compute_only``: Same as
  ``pipe_caps.resource_from_user_memory`` but indicates it is only supported from
  the compute engines.
* ``pipe_caps.device_reset_status_query``:
  Whether pipe_context::get_device_reset_status is implemented.
* ``pipe_caps.max_shader_patch_varyings``:
  How many per-patch outputs and inputs are supported between tessellation
  control and tessellation evaluation shaders, not counting in TESSINNER and
  TESSOUTER. The minimum allowed value for OpenGL is 30.
* ``pipe_caps.texture_float_linear``: Whether the linear minification and
  magnification filters are supported with single-precision floating-point
  textures.
* ``pipe_caps.texture_half_float_linear``: Whether the linear minification and
  magnification filters are supported with half-precision floating-point
  textures.
* ``pipe_caps.depth_bounds_test``: Whether bounds_test, bounds_min, and
  bounds_max states of pipe_depth_stencil_alpha_state behave according
  to the :ext:`GL_EXT_depth_bounds_test` specification.
* ``pipe_caps.texture_query_samples``: Whether the ``TXQS`` opcode is supported
* ``pipe_caps.force_persample_interp``: If the driver can force per-sample
  interpolation for all fragment shader inputs if
  pipe_rasterizer_state::force_persample_interp is set. This is only used
  by GL3-level sample shading (:ext:`GL_ARB_sample_shading`). GL4-level sample
  shading (:ext:`GL_ARB_gpu_shader5`) doesn't use this. While GL3 hardware has a
  state for it, GL4 hardware will likely need to emulate it with a shader
  variant, or by selecting the interpolation weights with a conditional
  assignment in the shader.
* ``pipe_caps.shareable_shaders``: Whether shader CSOs can be used by any
  pipe_context.  Important for reducing jank at draw time by letting GL shaders
  linked in one thread be used in another thread without recompiling.
* ``pipe_caps.copy_between_compressed_and_plain_formats``:
  Whether copying between compressed and plain formats is supported where
  a compressed block is copied to/from a plain pixel of the same size.
* ``pipe_caps.clear_scissored``: Whether ``clear`` can accept a scissored
  bounding box.
* ``pipe_caps.draw_parameters``: Whether ``TGSI_SEMANTIC_BASEVERTEX``,
  ``TGSI_SEMANTIC_BASEINSTANCE``, and ``TGSI_SEMANTIC_DRAWID`` are
  supported in vertex shaders.
* ``pipe_caps.shader_pack_half_float``: Whether packed 16-bit float
  packing/unpacking opcodes are supported.
* ``pipe_caps.fs_position_is_sysval``: If gallium frontends should use a
  system value for the POSITION fragment shader input.
* ``pipe_caps.fs_point_is_sysval``: If gallium frontends should use a system
  value for the POINT fragment shader input.
* ``pipe_caps.fs_face_is_integer_sysval``: If gallium frontends should use
  a system value for the FACE fragment shader input.
  Also, the FACE system value is integer, not float.
* ``pipe_caps.shader_buffer_offset_alignment``: Describes the required
  alignment for pipe_shader_buffer::buffer_offset, in bytes. Maximum
  value allowed is 256 (for GL conformance). 0 is only allowed if
  shader buffers are not supported.
* ``pipe_caps.invalidate_buffer``: Whether the use of ``invalidate_resource``
  for buffers is supported.
* ``pipe_caps.generate_mipmap``: Indicates whether pipe_context::generate_mipmap
  is supported.
* ``pipe_caps.string_marker``: Whether pipe->emit_string_marker() is supported.
* ``pipe_caps.surface_reinterpret_blocks``: Indicates whether
  pipe_context::create_surface supports reinterpreting a texture as a surface
  of a format with different block width/height (but same block size in bits).
  For example, a compressed texture image can be interpreted as a
  non-compressed surface whose texels are the same number of bits as the
  compressed blocks, and vice versa. The width and height of the surface is
  adjusted appropriately.
* ``pipe_caps.query_buffer_object``: Driver supports
  context::get_query_result_resource callback.
* ``pipe_caps.pci_group``: Return the PCI segment group number.
* ``pipe_caps.pci_bus``: Return the PCI bus number.
* ``pipe_caps.pci_device``: Return the PCI device number.
* ``pipe_caps.pci_function``: Return the PCI function number.
* ``pipe_caps.framebuffer_no_attachment``:
  If non-zero, rendering to framebuffers with no surface attachments
  is supported. The context->is_format_supported function will be expected
  to be implemented with PIPE_FORMAT_NONE yielding the MSAA modes the hardware
  supports. N.B., The maximum number of layers supported for rasterizing a
  primitive on a layer is obtained from ``pipe_caps.max_texture_array_layers``
  even though it can be larger than the number of layers supported by either
  rendering or textures.
* ``pipe_caps.robust_buffer_access_behavior``: Implementation uses bounds
  checking on resource accesses by shader if the context is created with
  PIPE_CONTEXT_ROBUST_BUFFER_ACCESS. See the
  :ext:`GL_ARB_robust_buffer_access_behavior` extension for information on the
  required behavior for out of bounds accesses and accesses to unbound
  resources.
* ``pipe_caps.cull_distance``: Whether the driver supports the
  :ext:`GL_ARB_cull_distance` extension and thus implements proper support for
  culling planes.
* ``pipe_caps.primitive_restart_for_patches``: Whether primitive restart is
  supported for patch primitives.
* ``pipe_caps.shader_group_vote``: Whether the ``VOTE_*`` ops can be used in
  shaders.
* ``pipe_caps.max_window_rectangles``: The maximum number of window rectangles
  supported in ``set_window_rectangles``.
* ``pipe_caps.polygon_offset_units_unscaled``: If true, the driver implements support
  for ``pipe_rasterizer_state::offset_units_unscaled``.
* ``pipe_caps.viewport_subpixel_bits``: Number of bits of subpixel precision for
  floating point viewport bounds.
* ``pipe_caps.rasterizer_subpixel_bits``: Number of bits of subpixel precision used
  by the rasterizer.
* ``pipe_caps.mixed_color_depth_bits``: Whether there is non-fallback
  support for color/depth format combinations that use a different
  number of bits. For the purpose of this cap, Z24 is treated as
  32-bit. If set to off, that means that a B5G6R5 + Z24 or RGBA8 + Z16
  combination will require a driver fallback, and should not be
  advertised in the GLX/EGL config list.
* ``pipe_caps.shader_array_components``: If true, the driver interprets the
  UsageMask of input and output declarations and allows declaring arrays
  in overlapping ranges. The components must be a contiguous range, e.g. a
  UsageMask of  xy or yzw is allowed, but xz or yw isn't. Declarations with
  overlapping locations must have matching semantic names and indices, and
  equal interpolation qualifiers.
  Components may overlap, notably when the gaps in an array of dvec3 are
  filled in.
* ``pipe_caps.stream_output_pause_resume``: Whether
  :ext:`GL_ARB_transform_feedback2` is supported, including pausing/resuming
  queries and having ``count_from_stream_output`` set on indirect draws to
  implement glDrawTransformFeedback.  Required for OpenGL 4.0.
* ``pipe_caps.stream_output_interleave_buffers``: Whether interleaved stream
  output mode is able to interleave across buffers. This is required for
  :ext:`GL_ARB_transform_feedback3`.
* ``pipe_caps.fbfetch``: The number of render targets whose value in the
  current framebuffer can be read in the shader.  0 means framebuffer fetch
  is not supported.  1 means that only the first render target can be read,
  and a larger value would mean that multiple render targets are supported.
* ``pipe_caps.fbfetch_coherent``: Whether framebuffer fetches from the fragment
  shader can be guaranteed to be coherent with framebuffer writes.
* ``pipe_caps.fbfetch_zs``: Whether fragment shader can fetch current values of
  Z/S attachments. These fetches are always coherent with framebuffer writes.
* ``pipe_caps.legacy_math_rules``: Whether NIR shaders support the
  ``shader_info.use_legacy_math_rules`` flag (see documentation there), and
  TGSI shaders support the corresponding ``TGSI_PROPERTY_LEGACY_MATH_RULES``.
* ``pipe_caps.fp16``: Whether 16-bit float operations are supported.
* ``pipe_caps.doubles``: Whether double precision floating-point operations
  are supported.
* ``pipe_caps.int64``: Whether 64-bit integer operations are supported.
* ``pipe_caps.tgsi_tex_txf_lz``: Whether TEX_LZ and TXF_LZ opcodes are
  supported.
* ``pipe_caps.shader_clock``: Whether the CLOCK opcode is supported.
* ``pipe_caps.polygon_mode_fill_rectangle``: Whether the
  PIPE_POLYGON_MODE_FILL_RECTANGLE mode is supported for
  ``pipe_rasterizer_state::fill_front`` and
  ``pipe_rasterizer_state::fill_back``.
* ``pipe_caps.sparse_buffer_page_size``: The page size of sparse buffers in
  bytes, or 0 if sparse buffers are not supported. The page size must be at
  most 64KB.
* ``pipe_caps.shader_ballot``: Whether the BALLOT and READ_* opcodes as well as
  the SUBGROUP_* semantics are supported.
* ``pipe_caps.tes_layer_viewport``: Whether ``VARYING_SLOT_LAYER`` and
  ``VARYING_SLOT_VIEWPORT`` are supported as tessellation evaluation
  shader outputs.
* ``pipe_caps.can_bind_const_buffer_as_vertex``: Whether a buffer with just
  PIPE_BIND_CONSTANT_BUFFER can be legally passed to set_vertex_buffers.
* ``pipe_caps.allow_mapped_buffers_during_execution``: As the name says.
* ``pipe_caps.post_depth_coverage``: whether
  ``TGSI_PROPERTY_FS_POST_DEPTH_COVERAGE`` is supported.
* ``pipe_caps.bindless_texture``: Whether bindless texture operations are
  supported.
* ``pipe_caps.nir_samplers_as_deref``: Whether NIR tex instructions should
  reference texture and sampler as NIR derefs instead of by indices.
* ``pipe_caps.query_so_overflow``: Whether the
  ``PIPE_QUERY_SO_OVERFLOW_PREDICATE`` and
  ``PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE`` query types are supported. Note that
  for a driver that does not support multiple output streams (i.e.,
  ``pipe_caps.max_vertex_streams`` is 1), both query types are identical.
* ``pipe_caps.memobj``: Whether operations on memory objects are supported.
* ``pipe_caps.load_constbuf``: True if the driver supports ``TGSI_OPCODE_LOAD`` use
  with constant buffers.
* ``pipe_caps.tile_raster_order``: Whether the driver supports
  :ext:`GL_MESA_tile_raster_order`, using the tile_raster_order_* fields in
  pipe_rasterizer_state.
* ``pipe_caps.max_combined_shader_output_resources``: Limit on combined shader
  output resources (images + buffers + fragment outputs). If 0 the state
  tracker works it out.
* ``pipe_caps.framebuffer_msaa_constraints``: This determines limitations
  on the number of samples that framebuffer attachments can have.
  Possible values:

    0. color.nr_samples == zs.nr_samples == color.nr_storage_samples
       (standard MSAA quality)
    1. color.nr_samples >= zs.nr_samples == color.nr_storage_samples
       (enhanced MSAA quality)
    2. color.nr_samples >= zs.nr_samples >= color.nr_storage_samples
       (full flexibility in tuning MSAA quality and performance)

  All color attachments must have the same number of samples and the same
  number of storage samples.
* ``pipe_caps.signed_vertex_buffer_offset``:
  Whether pipe_vertex_buffer::buffer_offset is treated as signed. The u_vbuf
  module needs this for optimal performance in workstation applications.
* ``pipe_caps.context_priority_mask``: For drivers that support per-context
  priorities, this returns a bitmask of ``PIPE_CONTEXT_PRIORITY_x`` for the
  supported priority levels.  A driver that does not support prioritized
  contexts can return 0.
* ``pipe_caps.fence_signal``: True if the driver supports signaling semaphores
  using fence_server_signal().
* ``pipe_caps.constbuf0_flags``: The bits of pipe_resource::flags that must be
  set when binding that buffer as constant buffer 0. If the buffer doesn't have
  those bits set, pipe_context::set_constant_buffer(.., 0, ..) is ignored
  by the driver, and the driver can throw assertion failures.
* ``pipe_caps.packed_uniforms``: True if the driver supports packed uniforms
  as opposed to padding to vec4s.  Requires ``pipe_shader_caps.integers`` if
  ``lower_uniforms_to_ubo`` is set.
* ``pipe_caps.conservative_raster_post_snap_triangles``: Whether the
  ``PIPE_CONSERVATIVE_RASTER_POST_SNAP`` mode is supported for triangles.
  The post-snap mode means the conservative rasterization occurs after
  the conversion from floating-point to fixed-point coordinates
  on the subpixel grid.
* ``pipe_caps.conservative_raster_post_snap_points_lines``: Whether the
  ``PIPE_CONSERVATIVE_RASTER_POST_SNAP`` mode is supported for points and lines.
* ``pipe_caps.conservative_raster_pre_snap_triangles``: Whether the
  ``PIPE_CONSERVATIVE_RASTER_PRE_SNAP`` mode is supported for triangles.
  The pre-snap mode means the conservative rasterization occurs before
  the conversion from floating-point to fixed-point coordinates.
* ``pipe_caps.conservative_raster_pre_snap_points_lines``: Whether the
  ``PIPE_CONSERVATIVE_RASTER_PRE_SNAP`` mode is supported for points and lines.
* ``pipe_caps.conservative_raster_post_depth_coverage``: Whether
  ``pipe_caps.post_depth_coverage`` works with conservative rasterization.
* ``pipe_caps.conservative_raster_inner_coverage``: Whether
  inner_coverage from :ext:`GL_INTEL_conservative_rasterization` is supported.
* ``pipe_caps.max_conservative_raster_subpixel_precision_bias``: The maximum
  subpixel precision bias in bits during conservative rasterization.
* ``pipe_caps.programmable_sample_locations``: True is the driver supports
  programmable sample location through ```get_sample_pixel_grid``` and
  ```set_sample_locations```.
* ``pipe_caps.max_gs_invocations``: Maximum supported value of
  TGSI_PROPERTY_GS_INVOCATIONS.
* ``pipe_caps.max_shader_buffer_size``: Maximum supported size for binding
  with set_shader_buffers. This is unsigned integer with the maximum of 4GB - 1.
* ``pipe_caps.max_combined_shader_buffers``: Maximum total number of shader
  buffers. A value of 0 means the sum of all per-shader stage maximums (see
  ``pipe_shader_caps.max_shader_buffers``).
* ``pipe_caps.max_combined_hw_atomic_counters``: Maximum total number of atomic
  counters. A value of 0 means the default value (MAX_ATOMIC_COUNTERS = 4096).
* ``pipe_caps.max_combined_hw_atomic_counter_buffers``: Maximum total number of
  atomic counter buffers. A value of 0 means the sum of all per-shader stage
  maximums (see ``pipe_shader_caps.max_hw_atomic_counter_buffers``).
* ``pipe_caps.max_texture_upload_memory_budget``: Maximum recommend memory size
  for all active texture uploads combined. This is a performance hint.
  0 means no limit.
* ``pipe_caps.max_vertex_element_src_offset``: The maximum supported value for
  of pipe_vertex_element::src_offset.
* ``pipe_caps.surface_sample_count``: Whether the driver
  supports pipe_surface overrides of resource nr_samples. If set, will
  enable :ext:`GL_EXT_multisampled_render_to_texture`.
* ``pipe_caps.image_atomic_float_add``: Atomic floating point adds are
  supported on images, buffers, and shared memory.
* ``pipe_caps.glsl_tess_levels_as_inputs``: True if the driver wants TESSINNER and TESSOUTER to be inputs (rather than system values) for tessellation evaluation shaders.
* ``pipe_caps.dest_surface_srgb_control``: Indicates whether the drivers
  supports switching the format between sRGB and linear for a surface that is
  used as destination in draw and blit calls.
* ``pipe_caps.max_varyings``: The maximum number of fragment shader
  varyings. This will generally correspond to
  ``pipe_shader_caps.max_inputs`` for the fragment shader, but in some
  cases may be a smaller number.
* ``pipe_caps.compute_grid_info_last_block``: Whether pipe_grid_info::last_block
  is implemented by the driver. See struct pipe_grid_info for more details.
* ``pipe_caps.compute_shader_derivative``: True if the driver supports derivatives (and texture lookups with implicit derivatives) in compute shaders.
* ``pipe_caps.image_load_formatted``: True if a format for image loads does not need to be specified in the shader IR
* ``pipe_caps.image_store_formatted``: True if a format for image stores does not need to be specified in the shader IR
* ``pipe_caps.throttle``: Whether or not gallium frontends should throttle pipe_context
  execution. 0 = throttling is disabled.
* ``pipe_caps.dmabuf``: Whether Linux DMABUF handles are supported by
  resource_from_handle and resource_get_handle.
  Possible bit field values:

    1. ``DRM_PRIME_CAP_IMPORT``: resource_from_handle is supported
    2. ``DRM_PRIME_CAP_EXPORT``: resource_get_handle is supported

* ``pipe_caps.cl_gl_sharing``: True if driver supports everything required by a frontend implementing the CL extension, and
  also supports importing/exporting all of pipe_texture_target via dma buffers.
* ``pipe_caps.prefer_compute_for_multimedia``: Whether VDPAU and VAAPI
  should use a compute-based blit instead of pipe_context::blit and compute pipeline for compositing images.
* ``pipe_caps.fragment_shader_interlock``: True if fragment shader interlock
  functionality is supported.
* ``pipe_caps.atomic_float_minmax``: Atomic float point minimum,
  maximum, exchange and compare-and-swap support to buffer and shared variables.
* ``pipe_caps.tgsi_div``: Whether opcode DIV is supported
* ``pipe_caps.dithering``: Whether dithering is supported
* ``pipe_caps.fragment_shader_texture_lod``: Whether texture lookups with
  explicit LOD is supported in the fragment shader.
* ``pipe_caps.fragment_shader_derivatives``: True if the driver supports
  derivatives in fragment shaders.
* ``pipe_caps.texture_shadow_lod``: True if the driver supports shadow sampler
  types with texture functions having interaction with LOD of texture lookup.
* ``pipe_caps.shader_samples_identical``: True if the driver supports a shader query to tell whether all samples of a multisampled surface are definitely identical.
* ``pipe_caps.image_atomic_inc_wrap``: Atomic increment/decrement + wrap around
  are supported.
* ``pipe_caps.prefer_imm_arrays_as_constbuf``: True if gallium frontends should
  turn arrays whose contents can be deduced at compile time into constant
  buffer loads, or false if the driver can handle such arrays itself in a more
  efficient manner (such as through nir_opt_large_constants() and nir->constant_data).
* ``pipe_caps.gl_spirv``: True if the driver supports :ext:`GL_ARB_gl_spirv` extension.
* ``pipe_caps.gl_spirv_variable_pointers``: True if the driver supports Variable Pointers in SPIR-V shaders.
* ``pipe_caps.demote_to_helper_invocation``: True if driver supports demote keyword in GLSL programs.
* ``pipe_caps.tgsi_tg4_component_in_swizzle``: True if driver wants the TG4 component encoded in sampler swizzle rather than as a separate source.
* ``pipe_caps.flatshade``: Driver supports pipe_rasterizer_state::flatshade.  Must be 1
    for non-NIR drivers or gallium nine.
* ``pipe_caps.alpha_test``: Driver supports alpha-testing.  Must be 1
    for non-NIR drivers or gallium nine.  If set, frontend may set
    ``pipe_depth_stencil_alpha_state->alpha_enabled`` and ``alpha_func``.
    Otherwise, alpha test will be lowered to a comparison and discard_if in the
    fragment shader.
* ``pipe_caps.point_size_fixed``: Driver supports point-sizes that are fixed,
  as opposed to writing gl_PointSize for every point.
* ``pipe_caps.two_sided_color``: Driver supports two-sided coloring.  Must be 1
    for non-NIR drivers.  If set, pipe_rasterizer_state may be set to indicate
    that back-facing primitives should use the back-side color as the FS input
    color.  If unset, mesa/st will lower it to gl_FrontFacing reads in the
    fragment shader.
* ``pipe_caps.clip_planes``: Driver supports user-defined clip-planes. 0 denotes none, 1 denotes MAX_CLIP_PLANES. > 1 overrides MAX. When is 0, pipe_rasterizer_state::clip_plane_enable is unused.
* ``pipe_caps.max_vertex_buffers``: Number of supported vertex buffers.
* ``pipe_caps.opencl_integer_functions``: Driver supports extended OpenCL-style integer functions.  This includes average, saturating addition, saturating subtraction, absolute difference, count leading zeros, and count trailing zeros.
* ``pipe_caps.integer_multiply_32x16``: Driver supports integer multiplication between a 32-bit integer and a 16-bit integer.  If the second operand is 32-bits, the upper 16-bits are ignored, and the low 16-bits are possibly sign extended as necessary.
* ``pipe_caps.nir_images_as_deref``: Whether NIR image load/store intrinsics should be nir_intrinsic_image_deref_* instead of nir_intrinsic_image_*.  Defaults to true.
* ``pipe_caps.packed_stream_output``: Driver supports packing optimization for stream output (e.g. GL transform feedback captured variables). Defaults to true.
* ``pipe_caps.viewport_transform_lowered``: Driver needs the nir_lower_viewport_transform pass to be enabled. This also means that the gl_Position value is modified and should be lowered for transform feedback, if needed. Defaults to false.
* ``pipe_caps.psiz_clamped``: Driver needs for the point size to be clamped. Additionally, the gl_PointSize has been modified and its value should be lowered for transform feedback, if needed. Defaults to false.
* ``pipe_caps.gl_begin_end_buffer_size``: Buffer size used to upload vertices for glBegin/glEnd.
* ``pipe_caps.viewport_swizzle``: Whether pipe_viewport_state::swizzle can be used to specify pre-clipping swizzling of coordinates (see :ext:`GL_NV_viewport_swizzle`).
* ``pipe_caps.system_svm``: True if all application memory can be shared with the GPU without explicit mapping.
* ``pipe_caps.viewport_mask``: Whether ``TGSI_SEMANTIC_VIEWPORT_MASK`` and ``TGSI_PROPERTY_LAYER_VIEWPORT_RELATIVE`` are supported (see :ext:`GL_NV_viewport_array2`).
* ``pipe_caps.map_unsynchronized_thread_safe``: Whether mapping a buffer as unsynchronized from any thread is safe.
* ``pipe_caps.glsl_zero_init``: Choose a default zero initialization some GLSL variables. If ``1``, then all GLSL shader variables and gl_FragColor are initialized to zero. If ``2``, then shader out variables are not initialized but function out variables are.
* ``pipe_caps.blend_equation_advanced``: Driver supports blend equation advanced without necessarily supporting FBFETCH.
* ``pipe_caps.nir_atomics_as_deref``: Whether NIR atomics instructions should reference atomics as NIR derefs instead of by indices.
* ``pipe_caps.no_clip_on_copy_tex``: Driver doesn't want x/y/width/height clipped based on src size when doing a copy texture operation (e.g.: may want out-of-bounds reads that produce 0 instead of leaving the texture content undefined)
* ``pipe_caps.max_texture_mb``: Maximum texture size in MB (default is 1024)
* ``pipe_caps.device_protected_surface``: Whether the device support protected / encrypted content.
* ``pipe_caps.prefer_real_buffer_in_constbuf0``: The state tracker is encouraged to upload constants into a real buffer and bind it into constant buffer 0 instead of binding a user pointer. This may enable a faster code-path in a gallium frontend for drivers that really prefer a real buffer.
* ``pipe_caps.gl_clamp``: Driver natively supports GL_CLAMP.  Required for non-NIR drivers with the GL frontend.  NIR drivers with the cap unavailable will have GL_CLAMP lowered to txd/txl with a saturate on the coordinates.
* ``pipe_caps.texrect``: Driver supports rectangle textures.  Required for OpenGL on ``!prefers_nir`` drivers.  If this cap is not present, st/mesa will lower the NIR to use normal 2D texture sampling by using either ``txs`` or ``nir_intrinsic_load_texture_scaling`` to normalize the texture coordinates.
* ``pipe_caps.sampler_reduction_minmax``: Driver supports EXT min/max sampler reduction.
* ``pipe_caps.sampler_reduction_minmax_arb``: Driver supports ARB min/max sampler reduction with format queries.
* ``pipe_caps.emulate_nonfixed_primitive_restart``: Driver requests all draws using a non-fixed restart index to be rewritten to use a fixed restart index.
* ``pipe_caps.supported_prim_modes``: A bitmask of the ``mesa_prim`` enum values that the driver can natively support.
* ``pipe_caps.supported_prim_modes_with_restart``: A bitmask of the ``mesa_prim`` enum values that the driver can natively support for primitive restart. Only useful if ``pipe_caps.primitive_restart`` is also exported.
* ``pipe_caps.prefer_back_buffer_reuse``: Only applies to DRI_PRIME. If 1, the driver prefers that DRI3 tries to use the same back buffer each frame. If 0, this means DRI3 will at least use 2 back buffers and ping-pong between them to allow the tiled->linear copy to run in parallel.
* ``pipe_caps.draw_vertex_state``: Driver supports ``pipe_screen::create_vertex_state/vertex_state_destroy`` and ``pipe_context::draw_vertex_state``. Only used by display lists and designed to serve vbo_save.
* ``pipe_caps.prefer_pot_aligned_varyings``: Driver prefers varyings to be aligned to power of two in a slot. If this cap is enabled, vec4 varying will be placed in .xyzw components of the varying slot, vec3 in .xyz and vec2 in .xy or .zw
* ``pipe_caps.max_sparse_texture_size``: Maximum 1D/2D/rectangle texture image dimension for a sparse texture.
* ``pipe_caps.max_sparse_3d_texture_size``: Maximum 3D texture image dimension for a sparse texture.
* ``pipe_caps.max_sparse_array_texture_layers``: Maximum number of layers in a sparse array texture.
* ``pipe_caps.sparse_texture_full_array_cube_mipmaps``: TRUE if there are no restrictions on the allocation of mipmaps in sparse textures and FALSE otherwise. See SPARSE_TEXTURE_FULL_ARRAY_CUBE_MIPMAPS_ARB description in :ext:`GL_ARB_sparse_texture` extension spec.
* ``pipe_caps.query_sparse_texture_residency``: TRUE if shader sparse texture sample instruction could also return the residency information.
* ``pipe_caps.clamp_sparse_texture_lod``: TRUE if shader sparse texture sample instruction support clamp the minimal lod to prevent read from uncommitted pages.
* ``pipe_caps.allow_draw_out_of_order``: TRUE if the driver allows the "draw out of order" optimization to be enabled. See _mesa_update_allow_draw_out_of_order for more details.
* ``pipe_caps.max_constant_buffer_size``: Maximum bound constant buffer size in bytes. This is unsigned integer with the maximum of 4GB - 1. This applies to all constant buffers used by UBOs, unlike ``pipe_shader_caps.max_const_buffer0_size``, which is specifically for GLSL uniforms.
* ``pipe_caps.hardware_gl_select``: Enable hardware accelerated GL_SELECT for this driver.
* ``pipe_caps.device_protected_context``: Whether the device supports protected / encrypted context which can manipulate protected / encrypted content (some devices might need protected contexts to access protected content, whereas ``pipe_caps.device_protected_surface`` does not require any particular context to do so).
* ``pipe_caps.allow_glthread_buffer_subdata_opt``: Whether to allow glthread to convert glBufferSubData to glCopyBufferSubData. This may improve or worsen performance depending on your driver.
* ``pipe_caps.null_textures`` : Whether the driver supports sampling from NULL textures.
* ``pipe_caps.astc_void_extents_need_denorm_flush`` : True if the driver/hardware needs denormalized values in ASTC void extent blocks flushed to zero.
* ``pipe_caps.validate_all_dirty_states`` : Whether state validation must also validate the state changes for resources types used in the previous shader but not in the current shader.
* ``pipe_caps.has_const_bw``: Whether the driver only supports non-data-dependent layouts (ie. not bandwidth compressed formats like AFBC, UBWC, etc), or supports ``PIPE_BIND_CONST_BW`` to disable data-dependent layouts on requested resources.
* ``pipe_caps.performance_monitor``: Whether GL_AMD_performance_monitor should be exposed.
* ``pipe_caps.texture_sampler_independent``: Whether sampler views and sampler states are independent objects, meaning both can be freely mixed and matched by the frontend. This isn't required for OpenGL where on the shader level those are the same object. However for proper gallium nine and OpenCL support this is required.
* ``pipe_caps.astc_decode_mode``: Whether the driver supports ASTC decode precision. The :ext:`GL_EXT_texture_compression_astc_decode_mode` extension will only get exposed if :ext:`GL_KHR_texture_compression_astc_ldr<GL_KHR_texture_compression_astc_hdr>` is also supported.
* ``pipe_caps.shader_subgroup_size``: A fixed subgroup size shader runs on GPU when GLSL GL_KHR_shader_subgroup_* extensions are enabled.
* ``pipe_caps.shader_subgroup_supported_stages``: Bitmask of shader stages which support GL_KHR_shader_subgroup_* intrinsics.
* ``pipe_caps.shader_subgroup_supported_features``: Bitmask of shader subgroup features listed in :ext:`GL_KHR_shader_subgroup`.
* ``pipe_caps.shader_subgroup_quad_all_stages``: Whether shader subgroup quad operations are supported by shader stages other than fragment shader.
* ``pipe_caps.multiview``: Whether multiview rendering of array textures is supported. A return of ``1`` indicates support for OVR_multiview, and ``2`` additionally supports OVR_multiview2. 
* ``pipe_caps.call_finalize_nir_in_linker``: Whether ``pipe_screen::finalize_nir`` can be called in the GLSL linker before the NIR is stored in the shader cache. It's always called again after st/mesa adds code for shader variants. It must be 1 if the driver wants to report compile failures to the GLSL linker. It must be 0 if two consecutive ``finalize_nir`` calls on the same shader can break it, or if ``finalize_nir`` can't handle NIR that isn't fully lowered for the driver, or if ``finalize_nir`` breaks passes that st/mesa runs after it. Setting it to 1 is generally safe for drivers that expose nir_io_has_intrinsics and that don't enable any optional shader variants in st/mesa. Since it's difficult to support, any future refactoring can change it to 0.
* ``pipe_caps.min_line_width``: The minimum width of a regular line.
* ``pipe_caps.min_line_width_aa``: The minimum width of a smoothed line.
* ``pipe_caps.max_line_width``: The maximum width of a regular line.
* ``pipe_caps.max_line_width_aa``: The maximum width of a smoothed line.
* ``pipe_caps.line_width_granularity``: The line width is rounded to a multiple of this number.
* ``pipe_caps.min_point_size``: The minimum width and height of a point.
* ``pipe_caps.min_point_size_aa``: The minimum width and height of a smoothed point.
* ``pipe_caps.max_point_size``: The maximum width and height of a point.
* ``pipe_caps.max_point_size_aa``: The maximum width and height of a smoothed point.
* ``pipe_caps.point_size_granularity``: The point size is rounded to a multiple of this number.
* ``pipe_caps.max_texture_anisotropy``: The maximum level of anisotropy that can be
  applied to anisotropically filtered textures.
* ``pipe_caps.max_texture_lod_bias``: The maximum :term:`LOD` bias that may be applied
  to filtered textures.
* ``pipe_caps.min_conservative_raster_dilate``: The minimum conservative rasterization
  dilation.
* ``pipe_caps.max_conservative_raster_dilate``: The maximum conservative rasterization
  dilation.
* ``pipe_caps.conservative_raster_dilate_granularity``: The conservative rasterization
  dilation granularity for values relative to the minimum dilation.


.. _pipe_shader_caps:

pipe_shader_caps
^^^^^^^^^^^^^^^^^

These are per-shader-stage capabitity queries. Different shader stages may
support different features.

* ``pipe_shader_caps.max_instructions``: The maximum number of instructions.
* ``pipe_shader_caps.max_alu_instructions``: The maximum number of arithmetic instructions.
* ``pipe_shader_caps.max_tex_instructions``: The maximum number of texture instructions.
* ``pipe_shader_caps.max_tex_indirections``: The maximum number of texture indirections.
* ``pipe_shader_caps.max_control_flow_depth``: The maximum nested control flow depth.
* ``pipe_shader_caps.max_inputs``: The maximum number of input registers.
* ``pipe_shader_caps.max_outputs``: The maximum number of output registers.
  This is valid for all shaders except the fragment shader.
* ``pipe_shader_caps.max_const_buffer0_size``: The maximum size of constant buffer 0 in bytes.
* ``pipe_shader_caps.max_const_buffers``: Maximum number of constant buffers that can be bound
  to any shader stage using ``set_constant_buffer``. If 0 or 1, the pipe will
  only permit binding one constant buffer per shader.

  If a value greater than 0 is returned, the driver can have multiple
  constant buffers bound to shader stages. The CONST register file is
  accessed with two-dimensional indices, like in the example below.

  ::

    DCL CONST[0][0..7]       # declare first 8 vectors of constbuf 0
    DCL CONST[3][0]          # declare first vector of constbuf 3
    MOV OUT[0], CONST[0][3]  # copy vector 3 of constbuf 0

* ``pipe_shader_caps.max_temps``: The maximum number of temporary registers.
* ``pipe_shader_caps.cont_supported``: Whether continue is supported.
* ``pipe_shader_caps.indirect_temp_addr``: Whether indirect addressing
  of the temporary file is supported.
* ``pipe_shader_caps.indirect_const_addr``: Whether indirect addressing
  of the constant file is supported.
* ``pipe_shader_caps.subroutines``: Whether subroutines are supported, i.e.
  BGNSUB, ENDSUB, CAL, and RET, including RET in the main block.
* ``pipe_shader_caps.integers``: Whether integer opcodes are supported.
  If unsupported, only float opcodes are supported.
* ``pipe_shader_caps.int64_atomics``: Whether int64 atomic opcodes are supported. The device needs to support add, sub, swap, cmpswap, and, or, xor, min, and max.
* ``pipe_shader_caps.fp16``: Whether half precision floating-point opcodes are supported.
   If unsupported, half precision ops need to be lowered to full precision.
* ``pipe_shader_caps.fp16_derivatives``: Whether half precision floating-point
  DDX and DDY opcodes are supported.
* ``pipe_shader_caps.fp16_const_buffers``: Whether half precision floating-point
  constant buffer loads are supported. Drivers are recommended to report 0
  if x86 F16C is not supported by the CPU (or an equivalent instruction set
  on other CPU architectures), otherwise they could be impacted by emulated
  FP16 conversions in glUniform.
* ``pipe_shader_caps.int16``: Whether 16-bit signed and unsigned integer types
  are supported.
* ``pipe_shader_caps.glsl_16bit_consts``: Lower mediump constants to 16-bit.
  Note that 16-bit constants are not lowered to uniforms in GLSL.
* ``pipe_shader_caps.max_texture_samplers``: The maximum number of texture
  samplers.
* ``pipe_shader_caps.max_sampler_views``: The maximum number of texture
  sampler views. Must not be lower than pipe_shader_caps.max_texture_samplers.
* ``pipe_shader_caps.tgsi_any_inout_decl_range``: Whether the driver doesn't
  ignore tgsi_declaration_range::Last for shader inputs and outputs.
* ``pipe_shader_caps.max_shader_buffers``: Maximum number of memory buffers
  (also used to implement atomic counters). Having this be non-0 also
  implies support for the ``LOAD``, ``STORE``, and ``ATOM*`` TGSI
  opcodes.
* ``pipe_shader_caps.supported_irs``: Supported representations of the
  program.  It should be a mask of ``pipe_shader_ir`` bits.
* ``pipe_shader_caps.max_shader_images``: Maximum number of image units.
* ``pipe_shader_caps.max_hw_atomic_counters``: If atomic counters are separate,
  how many HW counters are available for this stage. (0 uses SSBO atomics).
* ``pipe_shader_caps.max_hw_atomic_counter_buffers``: If atomic counters are
  separate, how many atomic counter buffers are available for this stage.

.. _pipe_compute_caps:

pipe_compute_caps
^^^^^^^^^^^^^^^^^^

Compute-specific capabilities. They can be queried using
pipe_screen::get_compute_param.

* ``pipe_compute_caps.ir_target``: A description of the target of the form
  ``processor-arch-manufacturer-os`` that will be passed on to the compiler.
  This CAP is only relevant for drivers that specify PIPE_SHADER_IR_NATIVE for
  their preferred IR.
* ``pipe_compute_caps.grid_dimension``: Number of supported dimensions
  for grid and block coordinates.
* ``pipe_compute_caps.max_grid_size``: Maximum grid size in block
  units.
* ``pipe_compute_caps.max_block_size``: Maximum block size in thread
  units.
* ``pipe_compute_caps.max_block_size_clover``: Same as ``pipe_compute_caps.max_block_size``
  but used by clover only.
* ``pipe_compute_caps.max_threads_per_block``: Maximum number of threads that
  a single block can contain.
  This may be less than the product of the components of MAX_BLOCK_SIZE and is
  usually limited by the number of threads that can be resident simultaneously
  on a compute unit.
* ``pipe_compute_caps.max_threads_per_block_clover``: Same as
  ``pipe_compute_caps.max_threads_per_block`` but used by clover only.
* ``pipe_compute_caps.max_global_size``: Maximum size of the GLOBAL
  resource.
* ``pipe_compute_caps.max_local_size``: Maximum size of the LOCAL
  resource.
* ``pipe_compute_caps.max_private_size``: Maximum size of the PRIVATE
  resource.
* ``pipe_compute_caps.max_input_size``: Maximum size of the INPUT
  resource.
* ``pipe_compute_caps.max_mem_alloc_size``: Maximum size of a memory object
  allocation in bytes.
* ``pipe_compute_caps.max_clock_frequency``: Maximum frequency of the GPU
  clock in MHz
* ``pipe_compute_caps.max_compute_units``: Maximum number of compute units
* ``pipe_compute_caps.max_subgroups``: The max amount of subgroups there can be
  inside a block. Non 0 indicates support for OpenCL subgroups including
  implementing ``get_compute_state_subgroup_size`` if multiple subgroup sizes
  are supported.
* ``pipe_compute_caps.images_supported``: Whether images are supported
  non-zero means yes, zero means no
* ``pipe_compute_caps.subgroup_sizes``: Ored power of two sizes of a basic execution
  unit in threads. Also known as wavefront size, warp size or SIMD width.
  E.g. ``64 | 32``.
* ``pipe_compute_caps.address_bits``: The default compute device address space
  size specified as an unsigned integer value in bits.
* ``pipe_compute_caps.max_variable_threads_per_block``: Maximum variable number
  of threads that a single block can contain. This is similar to
  pipe_compute_caps.max_threads_per_block, except that the variable size is not
  known a compile-time but at dispatch-time.

.. _pipe_bind:

PIPE_BIND_*
^^^^^^^^^^^

These flags indicate how a resource will be used and are specified at resource
creation time. Resources may be used in different roles
during their life cycle. Bind flags are cumulative and may be combined to create
a resource which can be used for multiple things.
Depending on the pipe driver's memory management and these bind flags,
resources might be created and handled quite differently.

* ``PIPE_BIND_RENDER_TARGET``: A color buffer or pixel buffer which will be
  rendered to.  Any surface/resource attached to pipe_framebuffer_state::cbufs
  must have this flag set.
* ``PIPE_BIND_DEPTH_STENCIL``: A depth (Z) buffer and/or stencil buffer. Any
  depth/stencil surface/resource attached to pipe_framebuffer_state::zsbuf must
  have this flag set.
* ``PIPE_BIND_BLENDABLE``: Used in conjunction with PIPE_BIND_RENDER_TARGET to
  query whether a device supports blending for a given format.
  If this flag is set, surface creation may fail if blending is not supported
  for the specified format. If it is not set, a driver may choose to ignore
  blending on surfaces with formats that would require emulation.
* ``PIPE_BIND_DISPLAY_TARGET``: A surface that can be presented to screen. Arguments to
  pipe_screen::flush_front_buffer must have this flag set.
* ``PIPE_BIND_SAMPLER_VIEW``: A texture that may be sampled from in a fragment
  or vertex shader.
* ``PIPE_BIND_VERTEX_BUFFER``: A vertex buffer.
* ``PIPE_BIND_INDEX_BUFFER``: An vertex index/element buffer.
* ``PIPE_BIND_CONSTANT_BUFFER``: A buffer of shader constants.
* ``PIPE_BIND_STREAM_OUTPUT``: A stream output buffer.
* ``PIPE_BIND_CUSTOM``:
* ``PIPE_BIND_SCANOUT``: A front color buffer or scanout buffer.
* ``PIPE_BIND_SHARED``: A shareable buffer that can be given to another
  process.
* ``PIPE_BIND_GLOBAL``: A buffer that can be mapped into the global
  address space of a compute program.
* ``PIPE_BIND_SHADER_BUFFER``: A buffer without a format that can be bound
  to a shader and can be used with load, store, and atomic instructions.
* ``PIPE_BIND_SHADER_IMAGE``: A buffer or texture with a format that can be
  bound to a shader and can be used with load, store, and atomic instructions.
* ``PIPE_BIND_COMPUTE_RESOURCE``: A buffer or texture that can be
  bound to the compute program as a shader resource.
* ``PIPE_BIND_COMMAND_ARGS_BUFFER``: A buffer that may be sourced by the
  GPU command processor. It can contain, for example, the arguments to
  indirect draw calls.

.. _pipe_usage:

PIPE_USAGE_*
^^^^^^^^^^^^

The PIPE_USAGE enums are hints about the expected usage pattern of a resource.
Note that drivers must always support read and write CPU access at any time
no matter which hint they got.

* ``PIPE_USAGE_DEFAULT``: Optimized for fast GPU access.
* ``PIPE_USAGE_IMMUTABLE``: Optimized for fast GPU access and the resource is
  not expected to be mapped or changed (even by the GPU) after the first upload.
* ``PIPE_USAGE_DYNAMIC``: Expect frequent write-only CPU access. What is
  uploaded is expected to be used at least several times by the GPU.
* ``PIPE_USAGE_STREAM``: Expect frequent write-only CPU access. What is
  uploaded is expected to be used only once by the GPU.
* ``PIPE_USAGE_STAGING``: Optimized for fast CPU access.


Methods
-------

XXX to-do

get_name
^^^^^^^^

Returns an identifying name for the screen.

The returned string should remain valid and immutable for the lifetime of
pipe_screen.

get_vendor
^^^^^^^^^^

Returns the screen vendor.

The returned string should remain valid and immutable for the lifetime of
pipe_screen.

get_device_vendor
^^^^^^^^^^^^^^^^^

Returns the actual vendor of the device driving the screen
(as opposed to the driver vendor).

The returned string should remain valid and immutable for the lifetime of
pipe_screen.

context_create
^^^^^^^^^^^^^^

Create a pipe_context.

**priv** is private data of the caller, which may be put to various
unspecified uses, typically to do with implementing swapbuffers
and/or front-buffer rendering.

is_format_supported
^^^^^^^^^^^^^^^^^^^

Determine if a resource in the given format can be used in a specific manner.

**format** the resource format

**target** one of the PIPE_TEXTURE_x flags

**sample_count** the number of samples. 0 and 1 mean no multisampling,
the maximum allowed legal value is 32.

**storage_sample_count** the number of storage samples. This must be <=
sample_count. See the documentation of ``pipe_resource::nr_storage_samples``.

**bindings** is a bitmask of :ref:`PIPE_BIND` flags.

Returns TRUE if all usages can be satisfied.


can_create_resource
^^^^^^^^^^^^^^^^^^^

Check if a resource can actually be created (but don't actually allocate any
memory).  This is used to implement OpenGL's proxy textures.  Typically, a
driver will simply check if the total size of the given resource is less than
some limit.

For PIPE_TEXTURE_CUBE, the pipe_resource::array_size field should be 6.


.. _resource_create:

resource_create
^^^^^^^^^^^^^^^

Create a new resource from a template.
The following fields of the pipe_resource must be specified in the template:

**target** one of the pipe_texture_target enums.
Note that PIPE_BUFFER and PIPE_TEXTURE_X are not really fundamentally different.
Modern APIs allow using buffers as shader resources.

**format** one of the pipe_format enums.

**width0** the width of the base mip level of the texture or size of the buffer.

**height0** the height of the base mip level of the texture
(1 for 1D or 1D array textures).

**depth0** the depth of the base mip level of the texture
(1 for everything else).

**array_size** the array size for 1D and 2D array textures.
For cube maps this must be 6, for other textures 1.

**last_level** the last mip map level present.

**nr_samples**: Number of samples determining quality, driving the rasterizer,
shading, and framebuffer. It is the number of samples seen by the whole
graphics pipeline. 0 and 1 specify a resource which isn't multisampled.

**nr_storage_samples**: Only color buffers can set this lower than nr_samples.
Multiple samples within a pixel can have the same color. ``nr_storage_samples``
determines how many slots for different colors there are per pixel.
If there are not enough slots to store all sample colors, some samples will
have an undefined color (called "undefined samples").

The resolve blit behavior is driver-specific, but can be one of these two:

1. Only defined samples will be averaged. Undefined samples will be ignored.
2. Undefined samples will be approximated by looking at surrounding defined
   samples (even in different pixels).

Blits and MSAA texturing: If the sample being fetched is undefined, one of
the defined samples is returned instead.

Sample shading (``set_min_samples``) will operate at a sample frequency that
is at most ``nr_storage_samples``. Greater ``min_samples`` values will be
replaced by ``nr_storage_samples``.

**usage** one of the :ref:`PIPE_USAGE` flags.

**bind** bitmask of the :ref:`PIPE_BIND` flags.

**flags** bitmask of PIPE_RESOURCE_FLAG flags.

**next**: Pointer to the next plane for resources that consist of multiple
memory planes.

As a corollary, this mean resources for an image with multiple planes have
to be created starting from the highest plane.

resource_changed
^^^^^^^^^^^^^^^^

Mark a resource as changed so derived internal resources will be recreated
on next use.

When importing external images that can't be directly used as texture sampler
source, internal copies may have to be created that the hardware can sample
from. When those resources are reimported, the image data may have changed, and
the previously derived internal resources must be invalidated to avoid sampling
from old copies.



resource_destroy
^^^^^^^^^^^^^^^^

Destroy a resource. A resource is destroyed if it has no more references.



get_timestamp
^^^^^^^^^^^^^

Query a timestamp in nanoseconds. The returned value should match
PIPE_QUERY_TIMESTAMP. This function returns immediately and doesn't
wait for rendering to complete (which cannot be achieved with queries).



get_driver_query_info
^^^^^^^^^^^^^^^^^^^^^

Return a driver-specific query. If the **info** parameter is NULL,
the number of available queries is returned.  Otherwise, the driver
query at the specified **index** is returned in **info**.
The function returns non-zero on success.
The driver-specific query is described with the pipe_driver_query_info
structure.

get_driver_query_group_info
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Return a driver-specific query group. If the **info** parameter is NULL,
the number of available groups is returned.  Otherwise, the driver
query group at the specified **index** is returned in **info**.
The function returns non-zero on success.
The driver-specific query group is described with the
pipe_driver_query_group_info structure.



get_disk_shader_cache
^^^^^^^^^^^^^^^^^^^^^

Returns a pointer to a driver-specific on-disk shader cache. If the driver
failed to create the cache or does not support an on-disk shader cache NULL is
returned. The callback itself may also be NULL if the driver doesn't support
an on-disk shader cache.


is_dmabuf_modifier_supported
^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Query whether the driver supports a **modifier** in combination with a
**format**, and whether it is only supported with "external" texture targets.
If the combination is supported in any fashion, true is returned.  If the
**external_only** parameter is not NULL, the bool it points to is set to
false if non-external texture targets are supported with the specified modifier+
format, or true if only external texture targets are supported.


get_dmabuf_modifier_planes
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Query the number of planes required by the image layout specified by the
**modifier** and **format** parameters.  The value returned includes both planes
dictated by **format** and any additional planes required for driver-specific
auxiliary data necessary for the layout defined by **modifier**.
If the proc is NULL, no auxiliary planes are required for any layout supported by
**screen** and the number of planes can be derived directly from **format**.


Thread safety
-------------

Screen methods are required to be thread safe. While gallium rendering
contexts are not required to be thread safe, it is required to be safe to use
different contexts created with the same screen in different threads without
locks. It is also required to be safe using screen methods in a thread, while
using one of its contexts in another (without locks).
