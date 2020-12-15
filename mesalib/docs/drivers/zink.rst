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
  * ``wideLines``
  * ``largePoints``
  * ``alphaToOne``
  * ``shaderClipDistance``

* Instance extensions:

  * `VK_KHR_get_physical_device_properties2`_
  * `VK_KHR_external_memory_capabilities`_

* Device extensions:

  * `VK_KHR_maintenance1`_
  * `VK_KHR_external_memory`_

OpenGL 3.0
^^^^^^^^^^

For OpenGL 3.0 support, the following additional device extensions are
required to be exposed and fully supported:

* `VK_EXT_transform_feedback`_
* `VK_EXT_conditional_rendering`_


OpenGL 3.1
^^^^^^^^^^

For OpenGL 3.1 support, the following additional ``VkPhysicalDeviceLimits``
are required:

* ``maxPerStageDescriptorSamplers`` ≥ 16

OpenGL 3.2
^^^^^^^^^^

For OpenGL 3.2 support, the following additional ``VkPhysicalDeviceFeatures``
are required to be supported, although some of these might not actually get
verified:

* ``depthClamp``
* ``geometryShader``
* ``shaderTessellationAndGeometryPointSize``

OpenGL 3.3
^^^^^^^^^^

For OpenGL 3.3 support, the following additional ``VkPhysicalDeviceFeatures``
are required to be supported, although some of these might not actually get
verified:

* ``VkPhysicalDeviceFeatures``

  * ``occlusionQueryPrecise``

* Device extensions:

  * `VK_EXT_vertex_attribute_divisor`_

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
questions, don't hesitate to visit `#zink on FreeNode
<irc://irc.freenode.net/zink>`_ and say hi!


.. _VK_KHR_get_physical_device_properties2: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_get_physical_device_properties2.html
.. _VK_KHR_external_memory_capabilities: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_memory_capabilities.html
.. _VK_KHR_maintenance1: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_maintenance1.html
.. _VK_KHR_external_memory: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_KHR_external_memory.html
.. _VK_EXT_transform_feedback: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_transform_feedback.html
.. _VK_EXT_conditional_rendering: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_conditional_rendering.html
.. _VK_EXT_vertex_attribute_divisor: https://www.khronos.org/registry/vulkan/specs/1.2-extensions/man/html/VK_EXT_vertex_attribute_divisor.html
