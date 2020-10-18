Continuous Integration
======================

In addition to a build step, the CI setup has a basic test stage
to ensure that we don't break functionality of various tools.  The
basic idea is to decode various files and compare the output to a
reference.  This means that some changes, like renaming registers
or bitfields in the rnndb xml requires updating the reference
output.

Layout:
 - .gitlab-ci/

   - traces/ - reference devcoredump and cmdstream traces.  The trace files should be kept small, and .rd files (which are already binary) should be compressed.

   - reference/ - reference output

   - genoutput.sh - script to generate output from the traces, used both by the CI test job, but it can also be used to update the reference output

Note on paths:
--------------

Gitlab CI uses an install-path of \`pwd\`/install.  If you use something
different, then setup a symlink.  Once that is done, to update reference
decodes (ie. to account for register .xml changes) run:

  ./src/freedreno/.gitlab-ci/genoutput.sh --update

TODO
----
- Maybe we could filter out some differences, like a new definition of a previously unknown register?
- It would be nice to add a test for afuc.. we probably cannot add a "real" fw file to this tree, but maybe could either fetch it from the linux-firmware git tree, or create our own dummy fw.

