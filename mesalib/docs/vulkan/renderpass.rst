Render Passes
=============

The Vulkan runtime code in Mesa provides several helpful utilities ot make
managing render passes easier.


VK_KHR_create_renderpass2
-------------------------

It is strongly recommended that drivers implement VK_KHR_create_renderpass2
directly and not bother implementing the old Vulkan 1.0 entrypoints.  If a
driver does not implement them, the following will be implemented in common
code in terms of their VK_KHR_create_renderpass2 counterparts:

 - :cpp:func:`vkCreateRenderPass`
 - :cpp:func:`vkCmdBeginRenderPass`
 - :cpp:func:`vkCmdNextSubpass`
 - :cpp:func:`vkCmdEndRenderPass`


Common VkRenderPass implementation
----------------------------------

The Vulkan runtime code in Mesa provides a common implementation of
:cpp:type:`VkRenderPass` called :cpp:struct:`vk_render_pass` which drivers
can optionally use.  Unlike most Vulkan runtime structs, it's not really
designed to be used as a base for a driver-specific struct.  It does,
however, contain all the information passed to
:cpp:func:`vkCreateRenderPass2` so it can be used in a driver so long as
that driver doesn't need to do any additional compilation at
:cpp:func:`vkCreateRenderPass2` time.  If a driver chooses to use
:cpp:struct:`vk_render_pass`, the Vulkan runtime provides implementations
of :cpp:func:`vkCreateRenderPass2` and :cpp:func:`vkDestroyRenderPass`.


VK_KHR_dynamic_rendering
------------------------

For drivers which don't need to do subpass combining, it is recommended
that they skip implementing render passess entirely and implement
VK_KHR_dynamic_rendering instead.  If they choose to do so, the runtime
will provide the following, implemented in terms of
:cpp:func:`vkCmdBeginRendering` and :cpp:func:`vkCmdEndRendering`:

 - :cpp:func:`vkCmdBeginRenderPass2`
 - :cpp:func:`vkCmdNextSubpass2`
 - :cpp:func:`vkCmdEndRenderPass2`

We also provide a no-op implementation of
:cpp:func:`vkGetRenderAreaGranularity` which returns a render area
granularity of 1x1.

Drivers which wish to use the common render pass imlementation in this way
**must** also support a Mesa-specific pseudo-extension which optionally
provides an initial image layout for each attachment at
:cpp:func:`vkCmdBeginRendering` time.  This is required for us to combine
render pass clears with layout transitions, often from
:cpp:enum:`VK_IMAGE_LAYOUT_UNDEFINED`.  On at least Intel and AMD,
combining these transitions with clears is important for performance.

.. doxygenstruct:: VkRenderingAttachmentInitialLayoutInfoMESA
   :members:

Because render passes and subpass indices are also passed into
:cpp:func:`vkCmdCreateGraphicsPipelines` and
:cpp:func:`vkCmdExecuteCommands` which we can't implement on the driver's
behalf, we provide a couple of helpers for getting the render pass
information in terms of the relevant VK_KHR_dynamic_rendering:

.. doxygenfunction:: vk_get_pipeline_rendering_create_info

.. doxygenfunction:: vk_get_command_buffer_inheritance_rendering_info

Apart from handling layout transitions, the common render pass
implementation mostly ignores input attachments.  It is expected that the
driver call :cpp:func:`nir_lower_input_attachments` to turn them into
texturing operations.  The driver **must** support texturing from an input
attachment at the same time as rendering to it in order to support Vulkan
subpass self-dependencies.  To assist drivers, we provide self-dependency
information through another Mesa-specific pseudo-extension:

.. doxygenstruct:: VkRenderingSelfDependencyInfoMESA
   :members:

vk_render_pass reference
------------------------

The following is a reference for the :cpp:struct:`vk_render_pass` structure
and its substructures.

.. doxygenstruct:: vk_render_pass
   :members:

.. doxygenstruct:: vk_render_pass_attachment
   :members:

.. doxygenstruct:: vk_subpass
   :members:

.. doxygenstruct:: vk_subpass_attachment
   :members:

.. doxygenstruct:: vk_subpass_dependency
   :members:
