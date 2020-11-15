Releasing Process
=================

Overview
--------

This document uses the convention X.Y.Z for the release number with X.Y
being the stable branch name.

Mesa provides feature and bugfix releases. Former use zero as patch
version (Z), while the latter have a non-zero one.

For example:

::

   Mesa 10.1.0 - 10.1 branch, feature
   Mesa 10.1.4 - 10.1 branch, bugfix
   Mesa 12.0.0 - 12.0 branch, feature
   Mesa 12.0.2 - 12.0 branch, bugfix

.. _schedule:

Release schedule
----------------

Releases should happen on Wednesdays. Delays can occur although those
should be kept to a minimum.

See our :doc:`calendar <release-calendar>` for information about how
the release schedule is planned, and the date and other details for
individual releases.

Feature releases
----------------

-  Available approximately every three months.
-  Feature releases are branched on or around the second Wednesday of
   January, April, July, and October.
-  Initial time plan available 2-4 weeks before the planned branchpoint
   (rc1) on the mesa-announce@ mailing list.
-  Typically, the final release will happen after 4 candidates.
   Additional ones may be needed in order to resolve blocking
   regressions, though.

Stable releases
---------------

-  Normally available once every two weeks.
-  Only the latest branch has releases. See note below.

.. note::

   There is one or two releases overlap when changing branches. For
   example:

   The final release from the 12.0 series Mesa 12.0.5 will be out around
   the same time (or shortly after) 13.0.1 is out.

   This also involves that, as a final release may be delayed due to the
   need of additional candidates to solve some blocking regression(s), the
   release manager might have to update the
   :doc:`calendar <release-calendar>` with additional bug fix releases of
   the current stable branch.

.. _pickntest:

Cherry-picking and testing
--------------------------

Commits nominated for the active branch are picked as based on the
:ref:`criteria <criteria>` as described in the same
section.

Nominations happen via special tags in the commit messages, and via
GitLab merge requests against the staging branches. There are special
scripts used to read the tags.

The maintainer should watch or be in contact with the Intel CI team, as
well as watch the GitLab CI for regressions.

Cherry picking should be done with the '-x' switch (to automatically add
"cherry picked from ..." to the commit message):

``git cherry-pick -x abcdef12345667890``

Developers can request, *as an exception*, patches to be applied up-to
the last one hour before the actual release. This is made **only** with
explicit permission/request, and the patch **must** be very well
contained. Thus it cannot affect more than one driver/subsystem.

Following developers have requested permanent exception

-  *Ilia Mirkin*
-  *AMD team*

The GitLab CI must pass.

For Windows related changes, the main contact point is Brian Paul. Jose
Fonseca can also help as a fallback contact.

For Android related changes, the main contact is Tapani Pälli. Mauro
Rossi is collaborating with Android-x86 and may provide feedback about
the build status in that project.

For MacOSX related changes, Jeremy Huddleston Sequoia is currently a
good contact point.

.. note::

   If a patch in the current queue needs any additional fix(es),
   then they should be squashed together. The commit messages and the
   "``cherry picked from``"-tags must be preserved.

::

   git show b10859ec41d09c57663a258f43fe57c12332698e

   commit b10859ec41d09c57663a258f43fe57c12332698e
   Author: Jonas Pfeil <pfeiljonas@gmx.de>
   Date:   Wed Mar 1 18:11:10 2017 +0100

       ralloc: Make sure ralloc() allocations match malloc()'s alignment.

       The header of ralloc needs to be aligned, because the compiler assumes
       ...

       (cherry picked from commit cd2b55e536dc806f9358f71db438dd9c246cdb14)

       Squashed with commit:

       ralloc: don't leave out the alignment factor

       Experimentation shows that without alignment factor GCC and Clang choose
       ...

       (cherry picked from commit ff494fe999510ea40e3ed5827e7818550b6de126)

Regression/functionality testing
--------------------------------

-  *no regressions should be observed for Piglit/dEQP/CTS/Vulkan on
   Intel platforms*
-  *no regressions should be observed for Piglit using the swrast,
   softpipe and llvmpipe drivers*

.. _stagingbranch:

Staging branch
--------------

A live branch, which contains the currently merge/rejected patches is
available in the main repository under ``staging/X.Y``. For example:

::

   staging/18.1 - WIP branch for the 18.1 series
   staging/18.2 - WIP branch for the 18.2 series

Notes:

-  People are encouraged to test the staging branch and report
   regressions.
-  The branch history is not stable and it **will** be rebased,

Making a branchpoint
--------------------

A branchpoint is made such that new development can continue in parallel
to stabilization and bugfixing.

.. note::

   Before doing a branch ensure that basic build and ``meson test``
   testing is done and there are little to-no issues. Ideally all of those
   should be tackled already.

Check if the version number is going to remain as, alternatively
``git mv docs/relnotes/{current,new}.rst`` as appropriate.

To setup the branchpoint:

::

   git checkout master # make sure we're in master first
   git tag -s X.Y-branchpoint -m "Mesa X.Y branchpoint"
   git checkout -b X.Y
   git checkout master
   $EDITOR VERSION # bump the version number
   git commit -as
   truncate docs/relnotes/new_features.txt
   git commit -a
   git push origin X.Y-branchpoint X.Y

Now go to
`GitLab <https://gitlab.freedesktop.org/mesa/mesa/-/milestones>`__ and
add the new Mesa version X.Y.

Check that there are no distribution breaking changes and revert them if
needed. For example: files being overwritten on install, etc. Happens
extremely rarely - we had only one case so far (see commit
2ced8eb136528914e1bf4e000dea06a9d53c7e04).

Making a new release
--------------------

These are the instructions for making a new Mesa release.

Get latest source files
~~~~~~~~~~~~~~~~~~~~~~~

Ensure the latest code is available - both in your local master and the
relevant branch.

Perform basic testing
~~~~~~~~~~~~~~~~~~~~~

Most of the testing should already be done during the
:ref:`cherry-pick <pickntest>` So we do a quick 'touch test'

-  meson dist
-  scons (from release tarball)
-  the produced binaries work

Here is one solution:

::

   __glxgears_cmd='glxgears 2>&1 | grep -v "configuration file"'
   __es2info_cmd='es2_info 2>&1 | egrep "GL_VERSION|GL_RENDERER|.*dri\.so"'
   __es2gears_cmd='es2gears_x11 2>&1 | grep -v "configuration file"'
   test "x$LD_LIBRARY_PATH" != 'x' && __old_ld="$LD_LIBRARY_PATH"
   export LD_LIBRARY_PATH=`pwd`/test/usr/local/lib/:"${__old_ld}"
   export LIBGL_DRIVERS_PATH=`pwd`/test/usr/local/lib/dri/
   export LIBGL_DEBUG=verbose
   eval $__glxinfo_cmd
   eval $__glxgears_cmd
   eval $__es2info_cmd
   eval $__es2gears_cmd
   export LIBGL_ALWAYS_SOFTWARE=true
   eval $__glxinfo_cmd
   eval $__glxgears_cmd
   eval $__es2info_cmd
   eval $__es2gears_cmd
   export LIBGL_ALWAYS_SOFTWARE=true
   export GALLIUM_DRIVER=softpipe
   eval $__glxinfo_cmd
   eval $__glxgears_cmd
   eval $__es2info_cmd
   eval $__es2gears_cmd
   # Smoke test DOTA2
   unset LD_LIBRARY_PATH
   test "x$__old_ld" != 'x' && export LD_LIBRARY_PATH="$__old_ld" && unset __old_ld
   unset LIBGL_DRIVERS_PATH
   unset LIBGL_DEBUG
   unset LIBGL_ALWAYS_SOFTWARE
   unset GALLIUM_DRIVER
   export VK_ICD_FILENAMES=`pwd`/test/usr/local/share/vulkan/icd.d/intel_icd.x86_64.json
   steam steam://rungameid/570  -vconsole -vulkan
   unset VK_ICD_FILENAMES

Create release notes for the new release
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The release notes are completely generated by the
``bin/gen_release_notes.py`` script. Simply run this script **before**
bumping the version. You'll need to come back to this file once the
tarball is generated to add its ``sha256sum``.

Increment the version contained in the file ``VERSION`` at Mesa's top-level,
then commit this change and **push the branch** (if you forget to do
this, ``release.sh`` below will fail).

Use the release.sh script from xorg `util-modular <https://cgit.freedesktop.org/xorg/util/modular/>`__
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Start the release process.

::

   ../relative/path/to/release.sh . # append --dist if you've already done distcheck above

Pay close attention to the prompts as you might be required to enter
your GPG and SSH passphrase(s) to sign and upload the files,
respectively.

Ensure that you do sign the tarballs, that your key is mentioned in the
release notes, and is published in `release-maintainers-keys.asc
<release-maintainers-keys.asc>`__.


Add the sha256sums to the release notes
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Edit ``docs/relnotes/X.Y.Z.rst`` to add the ``sha256sum`` as available in the
``mesa-X.Y.Z.announce`` template. Commit this change.

Back on mesa master, add the new release notes into the tree
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Something like the following steps will do the trick:

::

   git cherry-pick -x X.Y~1
   git cherry-pick -x X.Y

Then run the

::

   ./bin/post_version.py X.Y.Z

, where X.Y.Z is the version you just made. This will update
docs/relnotes.rst and docs/release-calendar.rst. It will then generate
a Git commit automatically. Check that everything looks correct and
push:

::

      git push origin master X.Y

Announce the release
--------------------

Use the generated template during the releasing process.

Again, pay attention to add a note to warn about a final release in a
series, if that is the case.

Update GitLab issues
--------------------

Parse through the bug reports as listed in the docs/relnotes/X.Y.Z.rst
document. If there's outstanding action, close the bug referencing the
commit ID which addresses the bug and mention the Mesa version that has
the fix.

.. note: the above is not applicable to all the reports, so use common sense.
