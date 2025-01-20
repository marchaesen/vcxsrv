Writing and running NIR unit tests
==================================

NIR uses `gtest <https://github.com/google/googletest>`__
for unit testing lowering and optimization passes. Tests
should declare a class to use for all test cases:

.. code:: c++

   class nir_my_pass_test : public nir_test {
   protected:
      nir_my_pass_test();

      void run_pass(nir_reference_shader expected);

      /* Resources used by the test */
   }

   nir_my_pass_test::nir_my_pass_test()
      : nir_test::nir_test("nir_my_pass_test")
   {
      /* Create resources used by the test */
   }

   void nir_my_pass_test::run_pass(nir_reference_shader expected)
   {
      nir_validate_shader(b->shader, "before nir_my_pass");
      NIR_PASS(_, b->shader, nir_my_pass);
      check_nir_string(expected);
   }

With this setup, the individual test cases can use ``nir_builder``
to initialize a shader, run the pass that should be tested on it
and compare it against a string, containing the expected pass
output:

.. code:: c++

   TEST_F(nir_my_pass_test, basic)
   {
      run_pass(NIR_REFERENCE_SHADER(R"(
         shader: MESA_SHADER_COMPUTE
         name: nir_my_pass_test
         workgroup_size: 1, 1, 1
         subgroup_size: 0
         decl_function main () (entrypoint)

         impl main {
             block b0:  // preds:
         }
      )"));
   }

The expected string can be managed using the
``bin/nir-test-runner.py`` script which builds and runs NIR
unit tests. The output of the tests are compared against the
expectations. The runner can then optionally update the
expectations.

.. code::

   user@distro:~/mesa$ python bin/nir-test-runner.py -Bbuild
   INFO: autodetecting backend as ninja
   INFO: calculating backend command to run: /usr/bin/ninja -C /home/konstantin/dev/mesa/build
   ninja: Entering directory `/home/konstantin/dev/mesa/build'
   [1/1] Generating src/git_sha1.h with a custom command
   diff --git a/src/compiler/nir/tests/opt_loop_tests.cpp b/src/compiler/nir/tests/opt_loop_tests.cpp
   index 05d3c6357c6..0d4810c5f85 100644
   --- a/src/compiler/nir/tests/opt_loop_tests.cpp
   +++ b/src/compiler/nir/tests/opt_loop_tests.cpp
   @@ -136,6 +136,7 @@ TEST_F(nir_opt_loop_test, opt_loop_merge_terminators_break_in_then)
       check_nir_string(NIR_REFERENCE_SHADER(R"(
          shader: MESA_SHADER_FRAGMENT
          name: nir_opt_loop_test
   +      subgroup_size: 0
          decl_var shader_in INTERP_MODE_SMOOTH none int in (VARYING_SLOT_POS.x, 0, 0)
          decl_var shader_out INTERP_MODE_NONE none int out (FRAG_RESULT_DEPTH.x, 0, 0)
          decl_var ubo INTERP_MODE_NONE none int ubo1 (0, 0, 0)

   Apply the changes listed above? [Y/n]y
