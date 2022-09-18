Running traces on a local machine
=================================

Prerequisites
-------------
- Install `Apitrace <https://apitrace.github.io/>`_
- Install `Renderdoc <https://renderdoc.org/>`_ (only needed for some traces)
- Download and compile `Piglit <https://gitlab.freedesktop.org/mesa/piglit>`_ and install his `dependencies <https://gitlab.freedesktop.org/mesa/piglit#2-setup>`_
- Download traces you want to replay from `traces-db <https://gitlab.freedesktop.org/gfx-ci/tracie/traces-db/>`_

Running single trace
--------------------
A simple run to see the output of the trace can be done with

.. code-block:: console

    apitrace replay -w name_of_trace.trace

For more information, look into the `Apitrace documentation <https://github.com/apitrace/apitrace/blob/master/docs/USAGE.markdown>`_.

For comparing checksums use:

.. code-block:: console

    cd piglit/replayer
    export PIGLIT_SOURCE_DIR="../"
    ./replayer.py compare trace -d test path/name_of_trace.trace 0 # replace with expected checksum


Simulating CI trace job
-----------------------

Sometimes it's useful to be able to test traces on your local machine instead of the Mesa CI runner. To simulate the CI environment as closely as possible.

Download the yml file from your driver's `ci/` directory and then change the path in the yml file from local proxy or MinIO to the local directory (url-like format ``file://``)

.. code-block:: console

    # The PIGLIT_REPLAY_DEVICE_NAME has to match name in the yml file.
    export PIGLIT_REPLAY_DEVICE_NAME='your_device_name'
    export PIGLIT_REPLAY_DESCRIPTION_FILE='path_to_mesa_traces_file.yml'
    ./piglit run -l verbose --timeout 300 -j10 replay ~/results/


Note: For replaying traces, you may need to allow higher GL and GLSL versions. You can achieve that by setting  ``MESA_GLSL_VERSION_OVERRIDE`` and ``MESA_GL_VERSION_OVERRIDE``.
