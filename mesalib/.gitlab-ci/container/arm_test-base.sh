#!/bin/bash

set -e
set -o xtrace

############### Install packages for building
apt-get install -y ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list
apt-get update

apt-get install -y --no-remove \
        abootimg \
        android-sdk-ext4-utils \
        bc \
        bison \
        bzip2 \
        ccache \
        cmake \
        cpio \
        g++ \
        debootstrap \
        fastboot \
        flex \
        git \
        netcat \
        nginx-full \
        python3-distutils \
        python3-minimal \
        python3-serial \
        python3.7 \
        pkg-config \
        procps \
        rsync \
        u-boot-tools \
        unzip

apt install -t buster-backports -y --no-remove \
    meson

# setup nginx
sed -i '/gzip_/ s/#\ //g' /etc/nginx/nginx.conf
cp .gitlab-ci/bare-metal/nginx-default-site  /etc/nginx/sites-enabled/default

. .gitlab-ci/container/container_post_build.sh
