================================
Structured Tagging for CI Builds
================================

This document explains the new structural tagging system integrated into our CI pipeline. Structural
tagging ensures that every build and test job is tied to a unique, reproducible build state by
computing a deterministic tag from each component's build script (and its relevant inputs, such as
patches) and verifying that tag at both build and test time.

Overview
--------
Structural tagging enhances container and rootfs tag management with an automated, end-to-end
process. Key aspects include:

Deterministic Tag Generation
~~~~~~~~~~~~~~~~~~~~~~~~~~~~
During the build, the system calculates a tag (an MD5 hash) from the contents of the build script
plus any extra files that affect the build. This tag represents the "structure" of the build.

YAML-Declared Tags
~~~~~~~~~~~~~~~~~~
For each component, a tag is declared in the YAML configuration file (located at
``.gitlab-ci/conditional-build-image-tags.yml``). The declared value is used to verify that the
build script has not changed without an accompanying update to the tag.

Dual-Phase Verification
~~~~~~~~~~~~~~~~~~~~~~~
* **Build Time:** The build script calls the helper function ``ci_build_time_tag_check`` immediately after calculating the tag. This function compares the calculated tag against the declared tag and writes the computed tag to a file (located at ``/mesa-ci-build-tag/``), ensuring early detection of mismatches.

* **Test Time:** Later, test scripts invoke ``ci_tag_test_time_check`` to read the stored tag and confirm that the tests run against the expected build version. A mismatch here will cause the test job to fail immediately.

Automated Tag Updates
~~~~~~~~~~~~~~~~~~~~~
A helper script ``bin/ci/update_tag.py`` is provided to list, check, and update tags for all or
individual components. This tool is intended to simplify the process of keeping the declared tags
synchronized with the build scripts.

How it works
------------

The mechanism considers a "component" any build script that follows the
``.gitlab-ci/container/build-*.sh`` pattern and also uses the ``ci_tag_*`` prefixed functions provided by
``.gitlab-ci/setup_test_env.sh``.

Suppose that SkQP just received the structured tagging support.
Let's look how the build and test phases work.

   .. graphviz::
      :caption: Structured Tagging

      digraph StructuredTagging {
         rankdir=TD;
         node [style=filled, fillcolor=lightgray];

         // =========================
         // Build Phase Subgraph
         // =========================
         subgraph cluster_build {
            label="Build Phase";
            style=dashed;

            // Define nodes with descriptive IDs and labels.
            tag_decl [
                  label="SKQP_TAG declared in\n.gitlab-ci/conditional-build-image-tags.yml",
                  shape=note, fillcolor=white
            ];
            calc_tag [
                  label="build-skqp.sh:\nCalculate tag (build script content + patches)",
                  shape=box, style="rounded,filled", fillcolor=lightgray
            ];
            early_check [
                  label="container_pre_build.sh:\nCheck all structured tags early",
                  shape=box, style="rounded,filled", fillcolor=lightgray
            ];
            validate_tag [
                  label="Validate calculated tag\nagainst declared SKQP_TAG",
                  shape=box, style="rounded,filled", fillcolor=lightgray
            ];
            build_decision [
                  label="Is calculated tag equal\nto declared SKQP_TAG?",
                  shape=diamond, fillcolor=lightyellow
            ];
            fail_build [
                  label="Fail build",
                  shape=rounded, fillcolor=salmon
            ];
            write_tag [
                  label="Write calculated tag to\n/var/tmp/mesa-ci-build-tag/SKQP_TAG",
                  shape=box, style="rounded,filled", fillcolor=lightblue
            ];
            compile_skqp [
                  label="Compile SkQP",
                  shape=box, fillcolor=palegreen
            ];

            // Define edges for the build phase.
            tag_decl -> calc_tag;
            calc_tag -> validate_tag;
            early_check -> validate_tag;
            validate_tag -> build_decision;
            build_decision -> fail_build [label="No"];
            build_decision -> write_tag [label="Yes"];
            write_tag -> compile_skqp;
         }

         // =========================
         // Test Phase Subgraph
         // =========================
         subgraph cluster_test {
            label="Test Phase";
            style=dashed;

            // Define nodes with descriptive IDs and labels.
            skqp_running [
                  label="Is SKQP running?",
                  shape=diamond, fillcolor=lightyellow
            ];
            ci_var_include [
                  label="image-tags.yml:\nincludes\ncontainer-builds-image-tags.yml",
                  shape=note, fillcolor=white
            ];
            ci_var [
                  label="CI Variable\n(CONDITIONAL_BUILD_SKQP_TAG)\nfrom container-builds-image-tags.yml",
                  shape=note, fillcolor=white
            ];
            ci_var_extends [
                  label="This job extends\n.container-builds-skqp\nMaking SKQP_TAG=CONDITIONAL_BUILD_SKQP_TAG",
                  shape=note, fillcolor=white
            ];
            check_tag [
                  label="deqp-runner.sh:\nPull tag in /var/tmp/mesa-ci-build-tag/SKQP_TAG",
                  shape=box, style="rounded,filled", fillcolor=lightblue
            ];
            decision [
                  label="Is calculated tag equal\nto declared SKQP_TAG?",
                  shape=diamond, fillcolor=lightyellow
            ];
            proceed_test [
                  label="Proceed the test job",
                  shape=box, fillcolor=palegreen
            ];
            fail_test [
                  label="Fail test",
                  shape=box, fillcolor=salmon
            ];

            // Define edges for the test phase.
            skqp_running -> check_tag [label="Yes"];
            skqp_running -> proceed_test [label="No"];
            check_tag -> decision;
            decision -> proceed_test [label="Yes"];
            decision -> fail_test [label="Mismatch"];
            ci_var_extends -> decision;
            ci_var -> ci_var_extends;
            ci_var_include -> ci_var;
         }
      }


Build-Time Checks
~~~~~~~~~~~~~~~~~
During the build phase:

* **Tag Calculation:**
   In the component's build script (named following the convention ``build-<component>.sh``), the
   function ``_ci_calculate_tag`` computes an MD5 hash based on:

   - The build script's contents.
   - Any additional files (e.g. patches) that affect the build.

* **Validation:**
   The build script calls ``ci_tag_build_time_check`` to verify that the current value of the component's
   tag (passed in as an environment variable) matches the tag calculated by the build script.

* **Failure on Mismatch:**
   If the tags do not match, the build is aborted. This prevents any accidental use of stale or mismatched artifacts.

* **Early checks:**
   Right now, the `container_pre_build.sh` script is responsible for checking the structured tagging
   in all registered components. So, we can check quickly, before the component's build starts, if
   the tag is correct.

* **Tag writing:**
   The build script writes the computed tag into a new file the structured tagging directory, namely
   ``/mesa-ci-build-tag/<component>_TAG``.

Test-Time Checks
~~~~~~~~~~~~~~~~
In the test scripts (for example, in ``.gitlab-ci/deqp-runner.sh``):

* **Verification:**
   The test job retrieves the tag written into the artifact (e.g. from ``/mesa-ci-build-tag/DEQP_RUNNER_TAG``) and then calls:

   .. code-block:: bash

      ci_tag_test_time_check "DEQP_RUNNER_TAG"

* **Purpose:**
   This check ensures that the tests are run against the exact build that was produced. If a mismatch is found, the test job fails immediately.

   .. note::
      Even when the developer forgets to update the ``image-tags.yml`` file when needed, the test job
      will fail if the tag is not correct, given that the ``conditional-build-image-tags.yml``
      file is properly updated.


Adding a New Component Tag
--------------------------
To integrate structured tagging for a new component (for example, ``my_component``), follow these steps:

1. **Modify the Build Script:**

   - In your build script (e.g. ``.gitlab-ci/container/build-my-component.sh``), map out the external files that can affect the build output.
     *Tip:* You can mimic the approach in ``build-angle.sh`` early variable declaration to get the tag.
   - Immediately after calculating the tag, add a validation step:

      .. code-block:: bash

         PATCH_FILES=("...")

         ci_tag_build_time_check "MY_COMPONENT_TAG" "${PATCH_FILES[@]}"

2. **If the component is run in a DUT job, update the passthrough script:**

   On ``.gitlab-ci/common/generate-env.sh``:

   .. code-block:: bash

      VARS=(
         ...
         MY_COMPONENT_TAG
         ...
      )

3. **Update the CI YAMLs:**

   - In your conditional image tags file (e.g. ``.gitlab-ci/conditional-build-image-tags.yml``), add an entry for your component:

      .. code-block:: yaml

         variables:
               CONDITIONAL_BUILD_MY_COMPONENT_TAG: <initial-tag-value>

   - Now we need to update the build related YAMLs to include the new component tag. In ``.gitlab-ci/container/gitlab-ci.yml``, add a new hidden job:

      .. code-block:: yaml

         .container-builds-my-component:
            variables:
               MY_COMPONENT_TAG: "${CONDITIONAL_BUILD_MY_COMPONENT_TAG}"

   - It is time to modify the job that builds the component image to include the new component tag. Let's suppose that only the ``kernel+rootfs_x86_64`` job builds the component image. We need to add the new component tag to the job as an extension:

      .. code-block:: yaml

         kernel+rootfs_x86_64:
            extends:
               - .container-builds-my-component
               - .container-builds-my-component2
            variables:
               # CI_BUILD_COMPONENTS is a space-separated list of components used during early tag checks
               CI_BUILD_COMPONENTS: "my_component my_component2"

      - Now, ``MY_COMPONENT_TAG`` will be used by the ``ci_tag_build_time_check`` and ``ci_tag_test_time_check`` functions, only for jobs that extend the ``.container-builds-my-component`` job.
      - And the ``CI_BUILD_COMPONENTS`` variable will be swept to perform the early checks.

   .. warning::
      Do not forget to update your main image tags file (e.g. ``.gitlab-ci/image-tags.yml``) if necessary, check the header comments of the modified files for more details.

   .. note::
      Also, note that the main image tags file (``.gitlab-ci/image-tags.yml``) does not define the
      conditional build tags directly.
      Instead, it **retrieves** values such as ``MY_COMPONENT_TAG`` from the `includes` directive of the
      ``.gitlab/container-builds-image-tags.yml`` file. This setup ensures centralized management of
      tag values and maintains consistency across various components and jobs.

Updating Component Tags with the Helper Script
----------------------------------------------
The helper script ``bin/ci/update_tag.py`` assists with tag management. Its key functionalities include:

* **Listing Available Components:**

  .. code-block:: bash

      ./bin/ci/update_tag.py --list

* **Updating All Component Tags:**

  .. code-block:: bash

      ./bin/ci/update_tag.py --all

* **Updating a Specific Component Tag:**

  .. code-block:: bash

      ./bin/ci/update_tag.py --include my_component
      ./bin/ci/update_tag.py --include 'my_component.*'

* **Running a Check:**

  .. code-block:: bash

      ./bin/ci/update_tag.py --check my_component
      ./bin/ci/update_tag.py --check my_component1 --check my_component2

This script uses the same underlying functions as in the build scripts to generate the deterministic tag and then updates the YAML file accordingly.
Ensure that your python environment has the requirements installed, see ``bin/ci/requirements.txt`` for the list of dependencies.

Limitations
-----------
The current implementation has some known limitations:

* **Local Utility Script Constraints:**

   When running the update/tagging utility locally, the build inputs used by the build script (such
   as environment variables defined in the YAML) are not automatically applied. For example, if the
   tag calculation relies on a variable like ``EXTRA_MESON_ARGS``, you must manually set or mock its
   value locally to generate the correct tag. Otherwise, the computed tag may be incorrect, and you
   might need to run the actual build job (and extract the expected tag from the error message) to
   verify the value. Future improvements may leverage tools like gitlab-ci-local to better reproduce
   the YAML environment locally.

* **Timing Sensitivity:**

   If the build script is modified after the early check (performed by the utility script) but before the actual build job runs, the calculated tag will differ from the declared tag. This discrepancy will block the build consistently until the YAML declaration is updated.

* **Manual Update Requirement:**

      In this initial version, updating the ``image-tags.yml``  must be done manually. If this file is
      not updated, the build scripts will not be validated properly.
      However, the test-time check will still catch mismatches and abort the job, ensuring that any
      issues do not go unnoticed.

Troubleshooting and FAQ
-----------------------

* **Tag Mismatch Errors:**
   If you encounter a tag mismatch error, verify that:

   - The build script and its additional inputs (patches, environment variables, etc.) are current.
   - The declared tag in ``.gitlab-ci/conditional-build-image-tags.yml`` has been updated accordingly. Use the update helper if necessary.

* **Local Testing Challenges:**
   When running the update utility locally, ensure that you mock any YAML-dependent variables (e.g., EXTRA_MESON_ARGS) to simulate the CI environment.

Conclusion
----------
The new structural tagging system provides a robust, automated method to ensure that every CI build
is uniquely identified and that tests run against the correct build state. By integrating
deterministic tag calculation with dual-phase verification and a dedicated update helper script,
this system minimizes human error and streamlines the CI process.

.. note::
   Be aware of the current limitations, especially around local testing and the manual update
   requirement, as you integrate and use structural tagging. Future improvements are planned to
   address these issues.

Happy tagging!
