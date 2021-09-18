#!/bin/bash

set -e
set -o xtrace

############### Install packages for baremetal testing
apt-get install -y ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
apt-get update

apt-get install -y --no-remove \
        abootimg \
        cpio \
        fastboot \
        netcat \
        procps \
        python3-distutils \
        python3-minimal \
        python3-serial \
        rsync \
        snmp \
        wget

# setup SNMPv2 SMI MIB
wget https://raw.githubusercontent.com/net-snmp/net-snmp/master/mibs/SNMPv2-SMI.txt \
    -O /usr/share/snmp/mibs/SNMPv2-SMI.txt

arch=arm64 . .gitlab-ci/container/baremetal_build.sh
arch=armhf . .gitlab-ci/container/baremetal_build.sh

# This firmware file from Debian bullseye causes hangs
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/plain/qcom/a530_pfp.fw?id=d5f9eea5a251d43412b07f5295d03e97b89ac4a5 \
     -O /rootfs-arm64/lib/firmware/qcom/a530_pfp.fw
