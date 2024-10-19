A Vulkan layer to display information about the running application using an overlay.

Building
========

The overlay layer will be built if :code:`screenshot` is passed as a :code:`vulkan-layers` argument. For example:

.. code-block:: sh

  meson -Dvulkan-layers=device-select,screenshot builddir/
  ninja -C builddir/
  sudo ninja -C builddir/ install

See `docs/install.rst <https://gitlab.freedesktop.org/mesa/mesa/-/blob/master/docs/install.rst>`__ for more information.

Basic Usage
===========

Turn on the layer:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot /path/to/my_vulkan_app


List the help menu:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG=help /path/to/my_vulkan_app

Enable log output in stdout/stderr:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG=log_type=<info|debug> /path/to/my_vulkan_app

Redirect screenshots taken to a different directory:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG=output_dir=/path/to/new_dir /path/to/my_vulkan_app

Capture pre-determined screenshots:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG=frames=1/5/7/15-4-5 /path/to/my_vulkan_app

Note:
 - Individual frames are separated by '/' and must be listed before the frame range
 - The frame range is determined by <range start>-<range count>-<range interval>
 - Example: '1/5/7/15-4-5' gives individual frames [1,5,7], then the frame range gives [15,20,25,30], combining into [1,5,7,15,20,25,30]

To capture all frames:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG=frames=all /path/to/my_vulkan_app

Direct Socket Control
---------------------

Enabling communication with the client can be done with the following setup:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG=comms /path/to/my_vulkan_app

The Unix socket may be used directly if needed. Once a client connects to the socket, the overlay layer will immediately
send the following commands to the client:

.. code-block:: sh

  :MesaOverlayControlVersion=1;
  :DeviceName=<device name>;
  :MesaVersion=<mesa version>;

The client connected to the overlay layer can trigger a screenshot to be taken by sending the command:

.. code-block:: sh

  :capture=<screenshot_name.png>;

Note that the screenshot name must include '.png', other image types are not supported.

.. _docs/install.rst: ../../docs/install.rst
