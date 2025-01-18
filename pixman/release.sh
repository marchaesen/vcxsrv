#!/bin/bash

set -eu

build_dir=build-release

case "$(git rev-parse --abbrev-ref HEAD)" in
master | [0-9]*.[0-9]*)
	;;
*)
	echo "Not on the master or a stable branch"
	exit 1
esac

if [[ -n "$(git log origin..)" ]]; then
	echo "The main branch has unpushed commits"
	exit 1
fi

meson_options=""
if [[ -e "$build_dir" ]]; then
	meson_options="$meson_options --wipe"
fi
meson setup "$build_dir" $meson_options

prev_version="$(git describe --tags --abbrev=0)"
version="$(meson introspect "$build_dir" --projectinfo | jq -r .version)"
if [[ "pixman-$version" == "$prev_version" ]]; then
	echo "Version not bumped"
	exit 1
fi

cairo_dest=cairographics.org:/srv/cairo.freedesktop.org/www/releases
xorg_dest=xorg.freedesktop.org:/srv/xorg.freedesktop.org/archive/individual/lib

cairo_url=https://cairographics.org/releases
xorg_url=https://www.x.org/releases/individual/lib

tar_gz="pixman-$version.tar.gz"
sha512_tgz="$tar_gz.sha512"
pgp_sig_tgz="$sha512_tgz.asc"

tar_xz="pixman-$version.tar.xz"
sha512_txz="$tar_xz.sha512"
pgp_sig_txz="$sha512_txz.asc"

announce="pixman-$version.eml"

distdir="${build_dir}/meson-dist"

git tag -m "pixman $version release" "pixman-$version"

meson setup "${build_dir}"
meson dist -C "${build_dir}" --formats xztar,gztar
pushd "$distdir" >&/dev/null
sha512sum "$tar_gz" >"$sha512_tgz"
sha512sum "$tar_xz" >"$sha512_txz"
gpg --armor --sign "$sha512_tgz"
gpg --armor --sign "$sha512_txz"

cat >"$announce" <<EOF
To: cairo-announce@cairographics.org, xorg-announce@lists.freedesktop.org, pixman@lists.freedesktop.org
Subject: [ANNOUNCE] pixman release $version now available

A new pixman release $version is now available.

tar.gz:
	$cairo_url/$tar_gz
	$xorg_url/$tar_gz

tar.xz:
	$cairo_url/$tar_xz
	$xorg_url/$tar_xz

Hashes:
	SHA512: $(sha512sum "$tar_gz")
	SHA512: $(sha512sum "$tar_xz")

PGP signature:
	$cairo_url/$pgp_sig_tgz
	$cairo_url/$pgp_sig_txz
	$xorg_url/$pgp_sig_tgz
	$xorg_url/$pgp_sig_txz

Git:
	https://gitlab.freedesktop.org/pixman/pixman.git
	tag: pixman-$version

Log:
$(git log --no-merges "${prev_version}..pixman-${version}" | git shortlog | awk '{ printf "\t"; print ; }' | cut -b1-80)
EOF

scp "$tar_gz" "$sha512_tgz" "$pgp_sig_tgz" "$tar_xz" "$sha512_txz" "$pgp_sig_txz" "$cairo_dest"
scp "$tar_gz" "$sha512_tgz" "$pgp_sig_tgz" "$tar_xz" "$sha512_txz" "$pgp_sig_txz" "$xorg_dest"
popd >& /dev/null

git push --follow-tags

echo "[ANNOUNCE] template generated in \"$distdir/$announce\" file."
echo "      Please pgp sign and send it."
