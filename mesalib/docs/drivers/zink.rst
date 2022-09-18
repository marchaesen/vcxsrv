Zink
====

Overview
--------

The Zink driver is a Gallium driver that emits Vulkan API calls instead
of targeting a specific GPU architecture. This can be used to get full
desktop OpenGL support on devices that only support Vulkan.

Features
--------

The feature-level of Zink depends on two things; what's implemented in Zink,
as well as the features of the Vulkan driver.

OpenGL 2.1
^^^^^^^^^^

OpenGL 2.1 is the minimum version Zink can support, and will always be
exposed, given Vulkan support. There's a few features that are required
for correct behavior, but not all of these are validated; instead you'll
see rendering-issues and likely validation error, or even crashes.

Here's a list of those requirements:

* Vulkan 1.0
* ``VkPhysicalDeviceFeatures``:

  * ``logicOp``
  * ``fillModeNonSolid``
  * ``alphaToOne``
  * ``shaderClipDistance``

* Device extensions:

  * `VK_KHR_maintenance1`_
  * `VK_KHR_create_renderpass2`_
  * `VK_KHR_imageless_framebuffer`_
  * `VK_KHR_timeline_semaphore`_
  * `VK_EXT_custom_border_color`_ with ``customBorderColorWithoutFormat``
  * `VK_EXT_provoking_vertex`_
  * `VK_EXT_line_rasterization`_, with the following ``VkPhysicalDeviceLineRasterizationFeaturesEXT``:

    * ``rectangularLines``
    * ``bresenhamLines``
    * ``smoothLines``
    * ``stippledRectangularLines``
    * ``stippledBresenhamLines``
    * ``stippledSmoothLines``

  * `VK_KHR_swapchain_mutable_format`_
  * `VK_EXT_border_color_swizzle`_
  * `VK_KHR_descriptor_update_template`_

In addition to this, `VK_KHR_external_memory`_ is required to support the
DRI code-path.

We also require either the `VK_EXT_scalar_block_layout`_ extension or
Vulkan 1.2, with the ``scalarBlockLayout`` feature.

OpenGL 3.0
^^^^^^^^^^


For OpenGL 3.0 support, the following additional requirements must be
supported:

* ``VkPhysicalDeviceFeatures``:

  * ``independentBlend``

* Device extensions:

  * `VK_EXT_transform_feedback`_
  * `VK_EXT_conditional_rendering`_

OpenGL 3.1
^^^^^^^^^^

For OpenGL 3.1 support, the following additional ``VkPhysicalDeviceLimits``
are required:

* ``maxPerStageDescriptorSamplers`` ≥ 16

OpenGL 3.2
^^^^^^^^^^

For OpenGL 3.2 support, the following additional requirements must be
supported, although some of these might not actually get verified:

* ``VkPhysicalDeviceFeatures``:

  * ``depthClamp``
  * ``geometryShader``
  * ``shaderTessellationAndGeometryPointSize``

* Device extensions:

  * `VK_EXT_depth_clip_enable`_

OpenGL 3.3
^^^^^^^^^^

For OpenGL 3.3 support, the following additional requirements must be
supported, although some of these might not actually get verified:

* ``VkPhysicalDeviceFeatures``:

  * ``occlusionQueryPrecise``

* Device extensions:

  * `VK_EXT_vertex_attribute_divisor`_

OpenGL 4.0
^^^^^^^^^^

For OpenGL 4.0 support, the following additional requirements must be
supported:

* ``VkPhysicalDeviceFeatures``:

  * ``sampleRateShading``
  * ``tessellationShader``
  * ``imageCubeArray``

* Device extensions:

  * `VK_KHR_maintenance2`_

* Formats requiring ``VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT``:

      * ``VK_FORMAT_R32G32B32_SFLOAT``
      * ``VK_FORMAT_R32G32B32_SINT``
      * ``VK_FORMAT_R32G32B32_UINT``

OpenGL 4.1
^^^^^^^^^^

For OpenGL 4.1 support, the following additional requirements must be
supported:

* ``VkPhysicalDeviceFeatures``:

  * ``multiViewport``

* ``VkPhysicalDeviceLimits``

  * ``maxImageDimension2D`` ≥ 16384
  * ``maxViewports`` ≥ 16

OpenGL 4.2
^^^^^^^^^^

For OpenGL 4.2 support, the following additional requirements must be
supported:

* Device extensions:
    * `VK_EXT_image_2d_view_of_3d`_

* ``VkPhysicalDeviceLimits``:

  * ``shaderStorageImageExtendedFormats``
  * ``shaderStorageImageWriteWithoutFormat``

* For Vulkan 1.2 and above:

  * ``VkPhysicalDeviceVulkan11Features``:

    * ``shaderDrawParameters``
    * ``vertexPipelineStoresAndAtomics``
    * ``fragmentStoresAndAtomics``
    * ``textureCompressionBC``

* For Vulkan 1.1 and below:

  * Device extensions:

    * `VK_KHR_shader_draw_parameters`_

OpenGL 4.3
^^^^^^^^^^

For OpenGL 4.3 support, the following additional requirements must be
supported:

* ``VkPhysicalDeviceFeatures``:

  * ``robustBufferAccess``

* Formats requiring ``VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT``:

   * ``VK_FORMAT_R8G8B8A8_UNORM``
   * ``VK_FORMAT_R8G8B8A8_SRGB``
   * ``VK_FORMAT_R16_UNORM``
   * ``VK_FORMAT_R16G16_UNORM``
   * ``VK_FORMAT_R16_SNORM``
   * ``VK_FORMAT_R16G16_SNORM``
   * ``VK_FORMAT_D32_SFLOAT_S8_UINT``

OpenGL 4.4
^^^^^^^^^^

For OpenGL 4.4 support, the following additional requirements must be
supported:

* Formats requiring ``VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT``:

  * ``VK_FORMAT_B10G11R11_UFLOAT_PACK32``

* Device extensions:

  * `VK_KHR_sampler_mirror_clamp_to_edge`_

OpenGL 4.5
^^^^^^^^^^

For OpenGL 4.5 support, the following additional ``VkPhysicalDeviceFeatures``
are required to be supported

* ``shaderCullDistance``

OpenGL 4.6
^^^^^^^^^^

For OpenGL 4.6 support, the following additional ``VkPhysicalDeviceFeatures``
are required to be supported

* ``VkPhysicalDeviceFeatures``:

  * ``samplerAnisotropy``
  * ``pipelineStatisticsQuery``
  * ``depthBiasClamp``

* Device extensions:

  * `VK_KHR_draw_indirect_count`_

Performance
-----------

If you notice poor performance and high CPU usage while running an application,
changing the descriptor manager may improve performance:

.. envvar:: ZINK_DESCRIPTORS <mode> ("auto")

``auto``
   Automatically detect best mode. This is the default.
``lazy``
   Attempt to use the least amount of CPU by binding descriptors opportunistically.

Debugging
---------

There's a few tools that are useful for debugging Zink, like this environment
variable:

.. envvar:: ZINK_DEBUG <flags> ("")

``nir``
   Print the NIR form of all shaders to stderr.
``spirv``
   Write the binary SPIR-V form of all compiled shaders to a file in the
   current directory, and print a message with the filename to stderr.
``tgsi``
   Print the TGSI form of TGSI shaders to stderr.
``validation``
   Dump Validation layer output.
``sync``
   Emit full synchronization barriers before every draw and dispatch.
``compact``
   Use a maximum of 4 descriptor sets
``noreorder``
   Do not reorder or optimize GL command streams

Vulkan Validation Layers
^^^^^^^^^^^^^^^^^^^^^^^^

Another useful tool for debugging is the `Vulkan Validation Layers
<https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/master/README.md>`_.

The validation layers effectively insert extra checking between Zink and the
Vulkan driver, pointing out incorrect usage of the Vulkan API. The layers can
be enabled by setting the environment variable :envvar:`VK_INSTANCE_LAYERS` to
"VK_LAYER_KHRONOS_validation". You can read more about the Validation Layers
in the link above.

IRC
---

In order to make things a bit easier to follow, we have decided to create our
own IRC channel. If you're interested in contributing, or have any technical
questions, don't hesitate to visit `#zink on OFTC
<irc://irc.oftc.net/zink>`__ and say hi!


.. _VK_KHR_maintenance1: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_maintenance1.html
.. _VK_KHR_create_renderpass2: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_create_renderpass2.html
.. _VK_KHR_imageless_framebuffer: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_imageless_framebuffer.html
.. _VK_KHR_timeline_semaphore: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_timeline_semaphore.html
.. _VK_KHR_external_memory: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_memory.html
.. _VK_EXT_scalar_block_layout: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_scalar_block_layout.html
.. _VK_EXT_transform_feedback: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_transform_feedback.html
.. _VK_EXT_conditional_rendering: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_conditional_rendering.html
.. _VK_EXT_vertex_attribute_divisor: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_vertex_attribute_divisor.html
.. _VK_EXT_image_2d_view_of_3d: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VK_EXT_image_2d_view_of_3d.html
.. _VK_KHR_maintenance2: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_maintenance2.html
.. _VK_KHR_shader_draw_parameters: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_shader_draw_parameters.html
.. _VK_KHR_draw_indirect_count: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_draw_indirect_count.html
.. _VK_KHR_sampler_mirror_clamp_to_edge: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_sampler_mirror_clamp_to_edge.html
.. _VK_EXT_custom_border_color: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_custom_border_color.html
.. _VK_EXT_provoking_vertex: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_provoking_vertex.html
.. _VK_EXT_line_rasterization: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_line_rasterization.html
.. _VK_KHR_swapchain_mutable_format: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_swapchain_mutable_format.html
.. _VK_EXT_border_color_swizzle: https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_EXT_border_color_swizzle.html
.. _VK_EXT_depth_clip_enable: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VK_EXT_depth_clip_enable.html
.. _VK_KHR_descriptor_update_template: https://www.khronos.org/registry/vulkan/specs/1.3-extensions/man/html/VK_KHR_descriptor_update_template.html
