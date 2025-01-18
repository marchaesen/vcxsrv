A Vulkan layer to take efficient screenshots to reduce performance loss in Vulkan applications.

Building
========

The screenshot layer will be built if :code:`screenshot` is passed as a :code:`vulkan-layers` argument. For example:

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

To capture a ImageRegion:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG=region=0.20/0.25/0.60/0.75 /path/to/my_vulkan_app

Note:
 - Using a region will capture a portion of the image on the GPU, meaning the copy time to the CPU memory can be reduced significantly if using a small enough region size, relative to the original image.
 - The regions are percentages, represented by floating point values. the syntax for a region is 'region=<startX>/<startY>/<endX>/<endY>', where percentages are given for the starting width and starting height, along with ending width and ending height.
 - - Example with vkcube:

- - - Original size: 500x500 image

.. code-block:: sh

  mesa-screenshot: DEBUG: Screenshot Authorized!
  mesa-screenshot: DEBUG: Needs 2 steps
  mesa-screenshot: DEBUG: Time to copy: 123530 nanoseconds


- - - Using '0.4/0.4/0.6/0.6' region: 100x100 image

.. code-block:: sh

  mesa-screenshot: DEBUG: Screenshot Authorized!
  mesa-screenshot: DEBUG: Using region: startX = 40% (200), startY = 40% (200), endX = 60% (300), endY = 60% (300)
  mesa-screenshot: DEBUG: Needs 2 steps
  mesa-screenshot: DEBUG: Time to copy: 12679 nanoseconds

Reducing a 500x500 image to a 100x100 image caused a reduction in the copy time by a factor of 10, meaning that we spend less time impacting the frame time and are able to run the workload with a negligible performance impact from the screenshot layer.

Direct Socket Control
---------------------

Enabling communication with the client can be done with the following setup:

.. code-block:: sh

  VK_LOADER_LAYERS_ENABLE=VK_LAYER_MESA_screenshot VK_LAYER_MESA_SCREENSHOT_CONFIG=comms /path/to/my_vulkan_app

The Unix socket may be used directly if needed. Once a client connects to the socket, the screenshot layer will immediately
send the following commands to the client:

.. code-block:: sh

  :MesaScreenshotControlVersion=1;
  :DeviceName=<device name>;
  :MesaVersion=<mesa version>;

The client connected to the screenshot layer can trigger a screenshot to be taken by sending the command:

.. code-block:: sh

  :capture=<screenshot_name.png>;

Note that the screenshot name must include '.png', other image types are not supported.

To capture a region, the region information must be added to the 'region' command, along with the 'capture' command, separated by a comma:

.. code-block:: sh

  :region=0.25/0.25/0.75/0.75,capture=<screenshot_name.png>;

.. _docs/install.rst: ../../docs/install.rst
