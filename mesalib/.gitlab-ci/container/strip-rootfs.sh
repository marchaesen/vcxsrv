#!/usr/bin/env bash
# Strip the image to a small minimal system.
# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# KERNEL_ROOTFS_TAG
set -ex

section_start strip-rootfs "Stripping rootfs"

export DEBIAN_FRONTEND=noninteractive

UNNEEDED_PACKAGES=(
  libfdisk1 git
  python3-dev python3-pip python3-setuptools python3-wheel
)

# Removing unused packages
for PACKAGE in "${UNNEEDED_PACKAGES[@]}"
do
	if ! apt-get remove --purge --yes "${PACKAGE}"
	then
		echo "WARNING: ${PACKAGE} isn't installed"
	fi
done

apt-get autoremove --yes || true

UNNEEDED_PACKAGES=(
  apt libapt-pkg6.0
  ncurses-bin ncurses-base libncursesw6 libncurses6
  perl-base libperl5.36 perl-modules-5.36
  debconf libdebconfclient0
  e2fsprogs e2fslibs libfdisk1
  insserv
  udev
  init-system-helpers
  cpio
  passwd
  libsemanage1 libsemanage-common
  libsepol1
  gpgv
  hostname
  adduser
  debian-archive-keyring
  libgl1-mesa-dri mesa-vulkan-drivers mesa-va-drivers mesa-vdpau-drivers i965-va-driver
  intel-media-va-driver
  gnupg2
  software-properties-common
)

# Removing unneeded packages
for PACKAGE in "${UNNEEDED_PACKAGES[@]}"
do
	if ! dpkg --purge --force-remove-essential --force-depends "${PACKAGE}"
	then
		echo "WARNING: ${PACKAGE} isn't installed"
	fi
done

# Show what's left package-wise before dropping dpkg itself
COLUMNS=300 dpkg-query -W --showformat='${Installed-Size;10}\t${Package}\n' | sort -k1,1n

# Drop dpkg
dpkg --purge --force-remove-essential --force-depends dpkg

# directories for a removal

directories=(
  /var/log/* # logs
  /usr/share/doc/* # docs, i18n, etc.
  /usr/share/locale/*
  /usr/share/X11/locale/*
  /usr/share/man
  /usr/share/i18n/*
  /usr/share/info/*
  /usr/share/lintian/*
  /usr/share/common-licenses/*
  /usr/share/mime/*
  /usr/share/bug
  /lib/udev/hwdb.bin # udev hwdb not required on a stripped system
  /lib/udev/hwdb.d/*
  /usr/bin/iconv # gconv conversions && binaries
  /usr/sbin/iconvconfig
  /usr/lib/*/gconv/
  /usr/sbin/update-usbids # libusb db
  /usr/share/misc/usb.ids
  /var/lib/usbutils/usb.ids
  /root/.pip # pip cache
  /root/.cache
  /root/.cargo
  /etc/apt # configuration archives of apt and dpkg
  /etc/dpkg
  /var/* # drop non-ostree directories
  /srv
  /share
  /usr/share/ca-certificates # certificates are in /etc
  /usr/share/bash-completion # completions
  /usr/share/zsh/vendor-completions
  /usr/share/gcc # gcc python helpers
  /etc/inid.d # sysvinit leftovers
  /etc/rc[0-6S].d
  /etc/init
  /usr/lib/lsb
  /usr/lib/xtables # xtrables helpers
  /usr/lib/locale/* # should we keep C locale?
  /usr/sbin/*fdisk # partitioning
  /usr/bin/localedef # local compiler
  /usr/sbin/ldconfig* # only needed when adding libs
  /usr/games
  /usr/lib/*/security/pam_userdb.so # Remove pam module to authenticate against a DB
  /usr/lib/*/libdb-5.3.so # libdb-5.3.so that is only used by this pam module ^
  /usr/lib/*/libnss_hesiod* # remove NSS support for nis, nisplus and hesiod
  /usr/lib/*/libnss_nis*
  /usr/lib/*/wine # don't need Wine's implementation, using Proton instead
  /usr/local/bin/mold
  /usr/local/bin/bindgen
  /usr/local/bin/cargo*
  /usr/local/bin/clippy*
  /usr/local/bin/rust*
  /usr/local/bin/rls
  /usr/lib/*/dri
)

for directory in "${directories[@]}"; do
  rm -rf "$directory" || echo "Failed to remove $directory! Update scripts!"
done

files=(
  '*systemd-resolve*' # systemd dns resolver
  '*networkd*' # systemd network configuration
  '*timesyncd*' # systemd ntp
  'systemd-hwdb*' # systemd hw database
  '*fuse*' # FUSE
)

for files in "${files[@]}"; do
  find /usr /etc -name "$files" -prune -exec rm -r {} \;
done

# We purge apt and dpkg to save on space, which is great for runtime and
# bandwidth use etc, but less great for cbuild which wants to run apt-get clean
# when we're done. Install a stub which works for that and is apologetic for
# anyone else.
cat >/usr/bin/apt-get <<EOF
#!/bin/bash

if [ "\${1:-}" != "clean" ]; then
    echo "Couldn't run '\$0 \$*', because apt has been cleaned from this container."
    echo ""
    echo "After .gitlab-ci/container/strip-rootfs.sh has run, you cannot install"
    echo "new packages."
    echo ""
    echo "Sorry."
    exit 1
fi
EOF

chmod +x /usr/bin/apt-get
ln -s /usr/bin/apt-get /usr/bin/apt

section_end strip-rootfs
