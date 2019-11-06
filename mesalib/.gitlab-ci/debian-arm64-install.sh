#!/bin/bash

set -e
set -o xtrace

############### Install packages for building
apt-get -y install ca-certificates
sed -i -e 's/http:\/\/deb/https:\/\/deb/g' /etc/apt/sources.list
echo 'deb https://deb.debian.org/debian buster-backports main' >/etc/apt/sources.list.d/backports.list
dpkg --add-architecture armhf
apt-get update
apt-get -y install \
	bc \
	bison \
	ccache \
	crossbuild-essential-armhf \
	flex \
	g++ \
	gettext \
	libdrm-dev \
	libdrm-dev:armhf \
	libelf-dev \
	libelf-dev:armhf \
	libexpat1-dev \
	libexpat1-dev:armhf \
	libssl-dev \
	llvm-7-dev:armhf \
	llvm-8-dev \
	meson \
	pkg-config \
	python \
	python3-mako \
	wget \
	zlib1g-dev

############### Generate cross build file for Meson

cross_file="/cross_file-armhf.txt"
/usr/share/meson/debcrossgen --arch armhf -o "$cross_file"
# Explicitly set ccache path for cross compilers
sed -i "s|/usr/bin/\([^-]*\)-linux-gnu\([^-]*\)-g|/usr/lib/ccache/\\1-linux-gnu\\2-g|g" "$cross_file"
# Don't need wrapper for armhf executables
sed -i -e '/\[properties\]/a\' -e "needs_exe_wrapper = False" "$cross_file"

apt-get purge -y \
        wget

apt-get autoremove -y --purge
