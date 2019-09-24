## Mesa testing using gitlab-runner

The goal of the "test" stage of the .gitlab-ci.yml is to do pre-merge
testing of Mesa drivers on various platforms, so that we can ensure no
regressions are merged, as long as developers are merging code using
the "Merge when pipeline completes" button.

This document only covers the CI from .gitlab-ci.yml and this
directory.  For other CI systems, see Intel's [Mesa
CI](https://gitlab.freedesktop.org/Mesa_CI) or panfrost's LAVA-based
CI (`src/gallium/drivers/panfrost/ci/`)

### Software architecture

For freedreno and llvmpipe CI, we're using gitlab-runner on the test
devices (DUTs), cached docker containers with VK-GL-CTS, and the
normal shared x86_64 runners to build the Mesa drivers to be run
inside of those containers on the DUTs.

The docker containers are rebuilt from the debian-install.sh script
when DEBIAN\_TAG is changed in .gitlab-ci.yml, and
debian-test-install.sh when DEBIAN\_ARM64\_TAG is changed in
.gitlab-ci.yml.  The resulting images are around 500MB, and are
expected to change approximately weekly (though an individual
developer working on them may produce many more images while trying to
come up with a working MR!).

gitlab-runner is a client that polls gitlab.freedesktop.org for
available jobs, with no inbound networking requirements.  Jobs can
have tags, so we can have DUT-specific jobs that only run on runners
with that tag marked in the gitlab UI.

Since dEQP takes a long time to run, we mark the job as "parallel" at
some level, which spawns multiple jobs from one definition, and then
deqp-runner.sh takes the corresponding fraction of the test list for
that job.

To reduce dEQP runtime (or avoid tests with unreliable results), a
deqp-runner.sh invocation can provide a list of tests to skip.  If
your driver is not yet conformant, you can pass a list of expected
failures, and the job will only fail on tests that aren't listed (look
at the job's log for which specific tests failed).

### DUT requirements

#### DUTs must have a stable kernel and GPU reset.

If the system goes down during a test run, that job will eventually
time out and fail (default 1 hour).  However, if the kernel can't
reliably reset the GPU on failure, bugs in one MR may leak into
spurious failures in another MR.  This would be an unacceptable impact
on Mesa developers working on other drivers.

#### DUTs must be able to run docker

The Mesa gitlab-runner based test architecture is built around docker,
so that we can cache the debian package installation and CTS build
step across multiple test runs.  Since the images are large and change
approximately weekly, the DUTs also need to be running some script to
prune stale docker images periodically in order to not run out of disk
space as we rev those containers (perhaps [this
script](https://gitlab.com/gitlab-org/gitlab-runner/issues/2980#note_169233611)).

Note that docker doesn't allow containers to be stored on NFS, and
doesn't allow multiple docker daemons to interact with the same
network block device, so you will probably need some sort of physical
storage on your DUTs.

#### DUTs must be public

By including your device in .gitlab-ci.yml, you're effectively letting
anyone on the internet run code on your device.  docker containers may
provide some limited protection, but how much you trust that and what
you do to mitigate hostile access is up to you.

#### DUTs must expose the dri device nodes to the containers.

Obviously, to get access to the HW, we need to pass the render node
through.  This is done by adding `devices = ["/dev/dri"]` to the
`runners.docker` section of /etc/gitlab-runner/config.toml.

### HW CI farm expectations

To make sure that testing of one vendor's drivers doesn't block
unrelated work by other vendors, we require that a given driver's test
farm produces a spurious failure no more than once a week.  If every
driver had CI and failed once a week, we would be seeing someone's
code getting blocked on a spurious failure daily, which is an
unacceptable cost to the project.

Additionally, the test farm needs to be able to provide a short enough
turnaround time that people can regularly use the "Merge when pipeline
succeeds" button successfully (until we get
[marge-bot](https://github.com/smarkets/marge-bot) in place on
freedesktop.org).  As a result, we require that the test farm be able
to handle a whole pipeline's worth of jobs in less than 5 minutes (to
compare, the build stage is about 10 minutes, if you could get all
your jobs scheduled on the shared runners in time.).

If a test farm is short the HW to provide these guarantees, consider
dropping tests to reduce runtime.
`VK-GL-CTS/scripts/log/bottleneck_report.py` can help you find what
tests were slow in a `results.qpa` file.  Or, you can have a job with
no `parallel` field set and:

```
  variables:
    CI_NODE_INDEX: 1
    CI_NODE_TOTAL: 10
```

to just run 1/10th of the test list.

If a HW CI farm goes offline (network dies and all CI pipelines end up
stalled) or its runners are consistenly spuriously failing (disk
full?), and the maintainer is not immediately available to fix the
issue, please push through an MR disabling that farm's jobs by adding
'.' to the front of the jobs names until the maintainer can bring
things back up.  If this happens, the farm maintainer should provide a
report to mesa-dev@lists.freedesktop.org after the fact explaining
what happened and what the mitigation plan is for that failure next
time.
