Bare-metal CI
=============

The bare-metal scripts run on a system with gitlab-runner and Docker,
connected to potentially multiple bare-metal boards that run tests of
Mesa.  Currently only "fastboot" and "ChromeOS Servo" devices are
supported.

In comparison with LAVA, this doesn't involve maintaining a separate
web service with its own job scheduler and replicating jobs between the
two.  It also places more of the board support in Git, instead of
web service configuration.  On the other hand, the serial interactions
and bootloader support are more primitive.

Requirements (fastboot)
-----------------------

This testing requires power control of the DUTs by the gitlab-runner
machine, since this is what we use to reset the system and get back to
a pristine state at the start of testing.

We require access to the console output from the gitlab-runner system,
since that is how we get the final results back from the tests.  You
should probably have the console on a serial connection, so that you
can see bootloader progress.

The boards need to be able to have a kernel/initramfs supplied by the
gitlab-runner system, since the initramfs is what contains the Mesa
testing payload.

The boards should have networking, so that we can extract the dEQP .xml
results to artifacts on GitLab.

Requirements (servo)
--------------------

For servo-connected boards, we can use the EC connection for power
control to reboot the board.  However, loading a kernel is not as easy
as fastboot, so we assume your bootloader can do TFTP, and that your
gitlab-runner mounts the runner's tftp directory specific to the board
at /tftp in the container.

Since we're going the TFTP route, we also use NFS root.  This avoids
packing the rootfs and sending it to the board as a ramdisk, which
means we can support larger rootfses (for piglit or tracie testing),
at the cost of needing more storage on the runner.

Telling the board about where its TFTP and NFS should come from is
done using dnsmasq on the runner host.  For example, this snippet in
the dnsmasq.conf.d in the google farm, with the gitlab-runner host we
call "servo"::

  dhcp-host=1c:69:7a:0d:a3:d3,10.42.0.10,set:servo

  # Fixed dhcp addresses for my sanity, and setting a tag for
  # specializing other DHCP options
  dhcp-host=a0:ce:c8:c8:d9:5d,10.42.0.11,set:cheza1
  dhcp-host=a0:ce:c8:c8:d8:81,10.42.0.12,set:cheza2

  # Specify the next server, watch out for the double ',,'.  The
  # filename didn't seem to get picked up by the bootloader, so we use
  # tftp-unique-root and mount directories like
  # /srv/tftp/10.42.0.11/jwerner/cheza as /tftp in the job containers.
  tftp-unique-root
  dhcp-boot=tag:cheza1,cheza1/vmlinuz,,10.42.0.10
  dhcp-boot=tag:cheza2,cheza2/vmlinuz,,10.42.0.10

  dhcp-option=tag:cheza1,option:root-path,/srv/nfs/cheza1
  dhcp-option=tag:cheza2,option:root-path,/srv/nfs/cheza2

Setup
-----

Each board will be registered in fd.o GitLab.  You'll want something
like this to register a fastboot board:

.. code-block:: console

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

For a servo board, you'll need to also volume mount the board's NFS
root dir at /nfs and TFTP kernel directory at /tftp.

The registration token has to come from a fd.o GitLab admin going to
https://gitlab.freedesktop.org/admin/runners

The name scheme for Google's lab is google-freedreno-boardname-n, and
our tag is something like google-freedreno-db410c.  The tag is what
identifies a board type so that board-specific jobs can be dispatched
into that pool.

We need privileged mode and the /dev bind mount in order to get at the
serial console and fastboot USB devices (--device arguments don't
apply to devices that show up after container start, which is the case
with fastboot, and the servo serial devices are actually links to
/dev/pts).  We use host network mode so that we can spin up a nginx
server to collect XML results for fastboot.

Once you've added your boards, you're going to need to add a little
more customization in ``/etc/gitlab-runner/config.toml``.  First, add
``concurrent = <number of boards>`` at the top ("we should have up to
this many jobs running managed by this gitlab-runner").  Then for each
board's runner, set ``limit = 1`` ("only 1 job served by this board at a
time").  Finally, add the board-specific environment variables
required by your bare-metal script, something like::

  [[runners]]
    name = "google-freedreno-db410c-1"
    environment = ["BM_SERIAL=/dev/ttyDB410c8", "BM_POWERUP=google-power-up.sh 8", "BM_FASTBOOT_SERIAL=15e9e390"]

If you want to collect the results for fastboot you need to add the following
two board-specific environment variables ``BM_WEBDAV_IP`` and ``BM_WEBDAV_PORT``.
These represent the IP address of the Docker host and the board specific port number
that gets used to start a nginx server.

Once you've updated your runners' configs, restart with ``sudo service
gitlab-runner restart``
