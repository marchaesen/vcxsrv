#!/bin/bash

set -ex

if [ $DEBIAN_ARCH = arm64 ]; then
    ARCH_PACKAGES="firmware-qcom-media"
elif [ $DEBIAN_ARCH = amd64 ]; then
    # Upstream LLVM package repository
    apt-get -y install --no-install-recommends gnupg ca-certificates
    apt-key add /llvm-snapshot.gpg.key
    echo "deb https://apt.llvm.org/buster/ llvm-toolchain-buster-10 main" >/etc/apt/sources.list.d/llvm10.list
    apt-get update

    ARCH_PACKAGES="firmware-amd-graphics
                   libelf1
                   libllvm10
                  "
fi

if [ -n "$INCLUDE_PIGLIT" ]; then
    PIGLIT_PACKAGES="libpython3.7
                     libwaffle-1-0
                     libx11-6
                     libx11-xcb1
                     libxcb-glx0
                     libxcb-shm0
                     libxdamage1
                     libxext6
                     libxfixes3
                     libxkbcommon0
                     libxxf86vm1
                     python3
                     python3-lxml
                     python3-mako
                     python3-numpy
                     python3-packaging
                     python3-pil
                     python3-requests
                     python3-simplejson
                     python3-yaml
                    "
    INSTALL_CI_FAIRY_PACKAGES="git
                               python3-dev
                               python3-pip
                               python3-setuptools
                               python3-wheel
                              "
fi

apt-get -y install --no-install-recommends \
    $ARCH_PACKAGES \
    $CI_FAIRY_PACKAGES \
    $INSTALL_CI_FAIRY_PACKAGES \
    $PIGLIT_PACKAGES \
    $VK_CTS_PACKAGES \
    ca-certificates \
    curl \
    initramfs-tools \
    libasan5 \
    libexpat1 \
    libpng16-16 \
    libsensors5 \
    libvulkan1 \
    libxcb-dri2-0 \
    libxcb-dri3-0 \
    libxcb-present0 \
    libxcb-randr0 \
    libxcb-sync1 \
    libxcb-xfixes0 \
    libxshmfence1 \
    netcat-openbsd \
    sntp \
    strace \
    wget \
    xinit \
    xserver-xorg-core \
    xz-utils

if [ -n "$INCLUDE_PIGLIT" ]; then
    # Needed for ci-fairy, this revision is able to upload files to
    # MinIO and doesn't depend on git
    pip3 install git+http://gitlab.freedesktop.org/freedesktop/ci-templates@0f1abc24c043e63894085a6bd12f14263e8b29eb

    apt-get purge -y \
        $INSTALL_CI_FAIRY_PACKAGES
fi

passwd root -d
chsh -s /bin/sh

cat > /init <<EOF
#!/bin/sh
export PS1=lava-shell:
exec sh
EOF
chmod +x  /init

mkdir -p /lib/firmware/rtl_nic
wget https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git/tree/rtl_nic/rtl8153a-3.fw -O /lib/firmware/rtl_nic/rtl8153a-3.fw

#######################################################################
# Strip the image to a small minimal system without removing the debian
# toolchain.

# xz compress firmware so it doesn't waste RAM at runtime.  Except db820c's
# GPU firmware, due to using a precompiled kernel without compression support.
find /lib/firmware -type f -print0 | \
    grep -vz a530 | \
    xargs -0r -P4 -n4 xz -T1 -C crc32
ln -s /lib/firmware/qcom/a530* /lib/firmware/

# Copy timezone file and remove tzdata package
rm -rf /etc/localtime
cp /usr/share/zoneinfo/Etc/UTC /etc/localtime

UNNEEDED_PACKAGES="libfdisk1
                   tzdata
                   diffutils
                   gnupg"

export DEBIAN_FRONTEND=noninteractive

# Removing unused packages
for PACKAGE in ${UNNEEDED_PACKAGES}
do
	echo ${PACKAGE}
	if ! apt-get remove --purge --yes "${PACKAGE}"
	then
		echo "WARNING: ${PACKAGE} isn't installed"
	fi
done

apt-get autoremove --yes || true

# Dropping logs
rm -rf /var/log/*

# Dropping documentation, localization, i18n files, etc
rm -rf /usr/share/doc/*
rm -rf /usr/share/locale/*
rm -rf /usr/share/X11/locale/*
rm -rf /usr/share/man
rm -rf /usr/share/i18n/*
rm -rf /usr/share/info/*
rm -rf /usr/share/lintian/*
rm -rf /usr/share/common-licenses/*
rm -rf /usr/share/mime/*

# Dropping reportbug scripts
rm -rf /usr/share/bug

# Drop udev hwdb not required on a stripped system
rm -rf /lib/udev/hwdb.bin /lib/udev/hwdb.d/*

# Drop all gconv conversions && binaries
rm -rf usr/bin/iconv
rm -rf usr/sbin/iconvconfig
rm -rf usr/lib/*/gconv/

# Remove libusb database
rm -rf usr/sbin/update-usbids
rm -rf var/lib/usbutils/usb.ids
rm -rf usr/share/misc/usb.ids

#######################################################################
# Crush into a minimal production image to be deployed via some type of image
# updating system.
# IMPORTANT: The Debian system is not longer functional at this point,
# for example, apt and dpkg will stop working

UNNEEDED_PACKAGES="apt libapt-pkg6.0 "\
"ncurses-bin ncurses-base libncursesw6 libncurses6 "\
"perl-base "\
"debconf libdebconfclient0 "\
"e2fsprogs e2fslibs libfdisk1 "\
"insserv "\
"udev "\
"init-system-helpers "\
"bash "\
"cpio "\
"xz-utils "\
"passwd "\
"libsemanage1 libsemanage-common "\
"libsepol1 "\
"gpgv "\
"hostname "\
"adduser "\
"debian-archive-keyring "\
"libegl1-mesa-dev "\
"libegl-mesa0 "\
"libgl1-mesa-dev "\
"libgl1-mesa-dri "\
"libglapi-mesa "\
"libgles2-mesa-dev "\
"libglx-mesa0 "\
"mesa-common-dev "\
"libz3-4 "\

# Removing unneeded packages
for PACKAGE in ${UNNEEDED_PACKAGES}
do
	echo "Forcing removal of ${PACKAGE}"
	if ! dpkg --purge --force-remove-essential --force-depends "${PACKAGE}"
	then
		echo "WARNING: ${PACKAGE} isn't installed"
	fi
done

# Show what's left package-wise before dropping dpkg itself
COLUMNS=300 dpkg-query -W --showformat='${Installed-Size;10}\t${Package}\n' | sort -k1,1n

# Drop dpkg
dpkg --purge --force-remove-essential --force-depends  dpkg

# No apt or dpkg, no need for its configuration archives
rm -rf etc/apt
rm -rf etc/dpkg

# Drop directories not part of ostree
# Note that /var needs to exist as ostree bind mounts the deployment /var over
# it
rm -rf var/* opt srv share

# ca-certificates are in /etc drop the source
rm -rf usr/share/ca-certificates

# No bash, no need for completions
rm -rf usr/share/bash-completion

# No zsh, no need for comletions
rm -rf usr/share/zsh/vendor-completions

# drop gcc-6 python helpers
rm -rf usr/share/gcc-6

# Drop sysvinit leftovers
rm -rf etc/init.d
rm -rf etc/rc[0-6S].d

# Drop upstart helpers
rm -rf etc/init

# Various xtables helpers
rm -rf usr/lib/xtables

# Drop all locales
# TODO: only remaining locale is actually "C". Should we really remove it?
rm -rf usr/lib/locale/*

# partition helpers
rm -rf usr/sbin/*fdisk

# local compiler
rm -rf usr/bin/localedef

# Systemd dns resolver
find usr etc -name '*systemd-resolve*' -prune -exec rm -r {} \;

# Systemd network configuration
find usr etc -name '*networkd*' -prune -exec rm -r {} \;

# systemd ntp client
find usr etc -name '*timesyncd*' -prune -exec rm -r {} \;

# systemd hw database manager
find usr etc -name '*systemd-hwdb*' -prune -exec rm -r {} \;

# No need for fuse
find usr etc -name '*fuse*' -prune -exec rm -r {} \;

# lsb init function leftovers
rm -rf usr/lib/lsb

# Only needed when adding libraries
rm -rf usr/sbin/ldconfig*

# Games, unused
rmdir usr/games

# Remove pam module to authenticate against a DB
# plus libdb-5.3.so that is only used by this pam module
rm -rf usr/lib/*/security/pam_userdb.so
rm -rf usr/lib/*/libdb-5.3.so

# remove NSS support for nis, nisplus and hesiod
rm -rf usr/lib/*/libnss_hesiod*
rm -rf usr/lib/*/libnss_nis*
