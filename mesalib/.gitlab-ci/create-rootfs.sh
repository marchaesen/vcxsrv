#!/bin/sh

set -ex

apt-get -y install --no-install-recommends initramfs-tools libpng16-16 strace libsensors5 libexpat1 libdrm2
passwd root -d
chsh -s /bin/sh
ln -s /bin/sh /init

#######################################################################
# Strip the image to a small minimal system without removing the debian
# toolchain.

# Copy timezone file and remove tzdata package
rm -rf /etc/localtime
cp /usr/share/zoneinfo/Etc/UTC /etc/localtime

UNNEEDED_PACKAGES="libfdisk1
                   tzdata
                   diffutils"

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

UNNEEDED_PACKAGES="apt libapt-pkg5.0 "\
"ncurses-bin ncurses-base libncursesw5 libncurses5 "\
"perl-base "\
"debconf libdebconfclient0 "\
"e2fsprogs e2fslibs libfdisk1 "\
"insserv "\
"udev "\
"init-system-helpers "\
"bash "\
"cpio "\
"passwd "\
"libsemanage1 libsemanage-common "\
"libsepol1 "\
"gzip "\
"gnupg "\
"gpgv "\
"hostname "\
"adduser "\
"debian-archive-keyring "\
"libgl1 libgl1-mesa-dri libglapi-mesa libglvnd0 libglx-mesa0 libegl-mesa0 libgles2 "\
"libllvm7 "\
"libx11-data libthai-data "\
"systemd dbus "\

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
rm usr/sbin/*fdisk

# local compiler
rm usr/bin/localedef

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
rm usr/sbin/ldconfig*

# Games, unused
rmdir usr/games

# Remove pam module to authenticate against a DB
# plus libdb-5.3.so that is only used by this pam module
rm usr/lib/*/security/pam_userdb.so
rm usr/lib/*/libdb-5.3.so

# remove NSS support for nis, nisplus and hesiod
rm usr/lib/*/libnss_hesiod*
rm usr/lib/*/libnss_nis*

rm bin/tar
