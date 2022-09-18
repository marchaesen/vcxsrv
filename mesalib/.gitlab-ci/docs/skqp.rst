skqp
====

`skqp <https://skia.org/docs/dev/testing/skqp/>`_ stands for SKIA Quality
Program conformance tests.  Basically, it has sets of rendering tests and unit
tests to ensure that `SKIA <https://skia.org/>`_ is meeting its design specifications on a specific
device.

The rendering tests have support for GL, GLES and Vulkan backends and test some
rendering scenarios.
And the unit tests check the GPU behavior without rendering images.

Tests
-----

Render tests design
^^^^^^^^^^^^^^^^^^^

It is worth noting that `rendertests.txt` can bring some detail about each test
expectation, so each test can have a max pixel error count, to tell skqp that it
is OK to have at most that number of errors for that test. See also:
https://github.com/google/skia/blob/c29454d1c9ebed4758a54a69798869fa2e7a36e0/tools/skqp/README_ALGORITHM.md

.. _test-location:

Location
^^^^^^^^

Each `rendertests.txt` and `unittest.txt` file must be located inside a specific
subdirectory inside skqp assets directory.

+--------------+--------------------------------------------+
| Test type    | Location                                   |
+==============+============================================+
| Render tests |  `${SKQP_ASSETS_DIR}/skqp/rendertests.txt` |
+--------------+--------------------------------------------+
| Unit tests   |  `${SKQP_ASSETS_DIR}/skqp/unittests.txt`   |
+--------------+--------------------------------------------+

The `skqp-runner.sh` script will make the necessary modifications to separate
`rendertests.txt` for each backend-driver combination. As long as the test files are located in the expected place:

+--------------+----------------------------------------------------------------------------------------------+
| Test type    | Location                                                                                     |
+==============+==============================================================================================+
| Render tests | `${MESA_REPOSITORY_DIR}/src/${GPU_DRIVER}/ci/${GPU_VERSION}-${SKQP_BACKEND}_rendertests.txt` |
+--------------+----------------------------------------------------------------------------------------------+
| Unit tests   | `${MESA_REPOSITORY_DIR}/src/${GPU_DRIVER}/ci/${GPU_VERSION}_unittests.txt`                   |
+--------------+----------------------------------------------------------------------------------------------+

Where `SKQP_BACKEND` can be:

- gl: for GL backend
- gles: for GLES backend
- vk: for Vulkan backend

Example file
""""""""""""

.. code-block:: console

  src/freedreno/ci/freedreno-a630-skqp-gl_rendertests.txt

- GPU_DRIVER: `freedreno`
- GPU_VERSION: `freedreno-a630`
- SKQP_BACKEND: `gl`

.. _rendertests-design:

skqp reports
------------

skqp generates reports after finishing its execution, they are located at the job
artifacts results directory and are divided in subdirectories by rendering tests
backends and unit
tests. The job log has links to every generated report in order to facilitate
the skqp debugging.

Maintaining skqp on Mesa CI
---------------------------

skqp is built alongside with another binary, namely `list_gpu_unit_tests`, it is
located in the same folder where `skqp` binary is.

This binary will generate the expected `unittests.txt` for the target GPU, so
ideally it should be executed on every skqp update and when a new device
receives skqp CI jobs.

1. Generate target unit tests for the current GPU with :code:`./list_gpu_unit_tests > unittests.txt`

2. Run skqp job

3. If there is a failing or crashing unit test, remove it from the corresponding `unittests.txt`

4. If there is a crashing render test, remove it from the corresponding `rendertests.txt`

5. If there is a failing render test, visually inspect the result from the HTML report
    - If the render result is OK, update the max error count for that test
    - Otherwise, or put `-1` in the same threshold, as seen in :ref:`rendertests-design`

6. Remember to put the new tests files to the locations cited in :ref:`test-location`
