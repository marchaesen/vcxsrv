#! /bin/bash

set -ex

# workaround to avoid conflict between systemd and systemd-standalone-sysusers
dnf -y swap systemd-standalone-sysusers systemd
dnf -y install wine
