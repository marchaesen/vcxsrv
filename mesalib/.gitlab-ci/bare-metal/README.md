# bare-metal Mesa testing

Testing Mesa with gitlab-runner running on the devices being tested
(DUTs) proved to be too unstable, so this set of scripts is for
running Mesa testing on bare-metal boards connected to a separate
system using gitlab-runner.  Currently only "fastboot" devices are
supported.

In comparison with LAVA, this doesn't involve maintaining a separate
webservice with its own job scheduler and replicating jobs between the
two.  It also places more of the board support in git, instead of
webservice configuration.  Most importantly, it doesn't download the
rootfs as artifacts on each job, so we can avoid traffic to
freedesktop.org.  On the other hand, the serial interactions and
bootloader support are more primitive.

## Requirements

This testing requires power control of the DUTs by the gitlab-runner
machine, since this is what we use to reset the system and get back to
a pristine state at the start of testing.

We require access to the console output from the gitlb-runner system,
since that is how we get the final results back from te tests.  You
should probably have the console on a serial connection, so that you
can see bootloader progress.

The boards need to be able to have a kernel/initramfs supplied by the
gitlab-runner system, since the initramfs is what contains the Mesa
testing payload.  Currently only "fastboot" devices are supported.

The boards should have networking, so that (in a future iteration of
this code) we can extract the dEQP .xml results to artifacts on
gitlab.

## Setup

Each board will be registered in fd.o gitlab.  You'll want something
like this to register:

```
sudo gitlab-runner register \
     --url https://gitlab.freedesktop.org \
     --registration-token $1 \
     --name MY_BOARD_NAME \
     --tag-list MY_BOARD_TAG \
     --executor docker \
     --docker-image "alpine:latest" \
     --docker-volumes "/dev:/dev" \
     --docker-network-mode "host" \
     --docker-privileged \
     --non-interactive
```

The registration token has to come from a fd.o gitlab admin going to
https://gitlab.freedesktop.org/admin/runners

The name scheme for Google's lab is google-freedreno-boardname-nn, and
our tag is google-freedreno-db410c.  The tag is what identifies a
board type so that board-specific jobs can be dispatched into that
pool.

We need privileged mode and the /dev bind mount in order to get at the
serial console and fastboot USB devices (--device arguments don't
apply to devices that show up after container start, which is the case
with fastboot).  We use host network mode so that we can (in the
future) spin up a server to collect XML results.

Once you've added your boards, you're going to need to specify the
board-specific env vars, adding something like this `environment` line
to each runner in `/etc/gitlab-runner/config.toml`

```
[[runners]]
  name = "google-freedreno-db410c-01"
  environment = ["BM_SERIAL=/dev/ttyDB410c8", "BM_POWERUP=google-power-up.sh 8", "BM_FASTBOOT_SERIAL=15e9e390"]
```

Once you've updated your runners' configs, restart with `sudo service
gitlab-runner restart`
