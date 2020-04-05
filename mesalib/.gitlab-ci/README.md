# Mesa testing

The goal of the "test" stage of the .gitlab-ci.yml is to do pre-merge
testing of Mesa drivers on various platforms, so that we can ensure no
regressions are merged, as long as developers are merging code using
marge-bot.

There are currently 4 automated testing systems deployed for Mesa.
LAVA and gitlab-runner on the DUTs are used in pre-merge testing and
are described in this document.  Managing bare metal using
gitlab-runner is described under [bare-metal/README.md].  Intel also
has a jenkins-based CI system with restricted access that isn't
connected to gitlab.

## Mesa testing using LAVA

[LAVA](https://lavasoftware.org/) is a system for functional testing
of boards including deploying custom bootloaders and kernels.  This is
particularly relevant to testing Mesa because we often need to change
kernels for UAPI changes (and this lets us do full testing of a new
kernel during development), and our workloads can easily take down
boards when mistakes are made (kernel oopses, OOMs that take out
critical system services).

### Mesa-LAVA software architecture

The gitlab-runner will run on some host that has access to the LAVA
lab, with tags like "lava-mesa-boardname" to control only taking in
jobs for the hardware that the LAVA lab contains.  The gitlab-runner
spawns a docker container with lava-cli in it, and connects to the
LAVA lab using a predefined token to submit jobs under a specific
device type.

The LAVA instance manages scheduling those jobs to the boards present.
For a job, it will deploy the kernel, device tree, and the ramdisk
containing the CTS.

### Deploying a new Mesa-LAVA lab

You'll want to start with setting up your LAVA instance and getting
some boards booting using test jobs.  Start with the stock QEMU
examples to make sure your instance works at all.  Then, you'll need
to define your actual boards.

The device type in lava-gitlab-ci.yml is the device type you create in
your LAVA instance, which doesn't have to match the board's name in
`/etc/lava-dispatcher/device-types`.  You create your boards under
that device type and the Mesa jobs will be scheduled to any of them.
Instantiate your boards by creating them in the UI or at the command
line attached to that device type, then populate their dictionary
(using an "extends" line probably referencing the board's template in
`/etc/lava-dispatcher/device-types`).  Now, go find a relevant
healthcheck job for your board as a test job definition, or cobble
something together from a board that boots using the same boot_method
and some public images, and figure out how to get your boards booting.

Once you can boot your board using a custom job definition, it's time
to connect Mesa CI to it.  Install gitlab-runner and register as a
shared runner (you'll need a gitlab admin for help with this).  The
runner *must* have a tag (like "mesa-lava-db410c") to restrict the
jobs it takes or it will grab random jobs from tasks across fd.o, and
your runner isn't ready for that.

The runner will be running an ARM docker image (we haven't done any
x86 LAVA yet, so that isn't documented).  If your host for the
gitlab-runner is x86, then you'll need to install qemu-user-static and
the binfmt support.

The docker image will need access to the lava instance.  If it's on a
public network it should be fine.  If you're running the LAVA instance
on localhost, you'll need to set `network_mode="host"` in
`/etc/gitlab-runner/config.toml` so it can access localhost.  Create a
gitlab-runner user in your LAVA instance, log in under that user on
the web interface, and create an API token.  Copy that into a
`lavacli.yaml`:

```
default:
  token: <token contents>
  uri: <url to the instance>
  username: gitlab-runner
```

Add a volume mount of that `lavacli.yaml` to
`/etc/gitlab-runner/config.toml` so that the docker container can
access it.  You probably have a `volumes = ["/cache"]` already, so now it would be

```
  volumes = ["/home/anholt/lava-config/lavacli.yaml:/root/.config/lavacli.yaml", "/cache"]
```

Note that this token is visible to anybody that can submit MRs to
Mesa!  It is not an actual secret.  We could just bake it into the
gitlab CI yml, but this way the current method of connecting to the
LAVA instance is separated from the Mesa branches (particularly
relevant as we have many stable branches all using CI).

Now it's time to define your test runner in
`.gitlab-ci/lava-gitlab-ci.yml`.

## Mesa testing using gitlab-runner on DUTs

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
