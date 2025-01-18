#!/usr/bin/env bash
# shellcheck disable=SC2086 # we want word splitting

# When changing this file, you need to bump the following
# .gitlab-ci/image-tags.yml tags:
# DEBIAN_TEST_ANDROID_TAG
# DEBIAN_BASE_TAG
# KERNEL_ROOTFS_TAG

set -uex

DEQP_RUNNER_VERSION=0.20.2

commits_to_backport=(
)

patch_files=(
)

DEQP_RUNNER_GIT_URL="${DEQP_RUNNER_GIT_URL:-https://gitlab.freedesktop.org/mesa/deqp-runner.git}"

if [ -n "${DEQP_RUNNER_GIT_TAG:-}" ]; then
    DEQP_RUNNER_GIT_CHECKOUT="$DEQP_RUNNER_GIT_TAG"
elif [ -n "${DEQP_RUNNER_GIT_REV:-}" ]; then
    DEQP_RUNNER_GIT_CHECKOUT="$DEQP_RUNNER_GIT_REV"
else
    DEQP_RUNNER_GIT_CHECKOUT="v$DEQP_RUNNER_VERSION"
fi

BASE_PWD=$PWD

mkdir -p /deqp-runner
pushd /deqp-runner
mkdir deqp-runner-git
pushd deqp-runner-git
git init
git remote add origin "$DEQP_RUNNER_GIT_URL"
git fetch --depth 1 origin "$DEQP_RUNNER_GIT_CHECKOUT"
git checkout FETCH_HEAD

for commit in "${commits_to_backport[@]}"
do
  PATCH_URL="https://gitlab.freedesktop.org/mesa/deqp-runner/-/commit/$commit.patch"
  echo "Backport deqp-runner commit $commit from $PATCH_URL"
  curl -L --retry 4 -f --retry-all-errors --retry-delay 60 $PATCH_URL | git am
done

for patch in "${patch_files[@]}"
do
  echo "Apply patch to deqp-runner from $patch"
  git am "$BASE_PWD/.gitlab-ci/container/patches/$patch"
done

if [ -z "${RUST_TARGET:-}" ]; then
    RUST_TARGET=""
fi

if [[ "$RUST_TARGET" != *-android ]]; then
    # When CC (/usr/lib/ccache/gcc) variable is set, the rust compiler uses
    # this variable when cross-compiling arm32 and build fails for zsys-sys.
    # So unset the CC variable when cross-compiling for arm32.
    SAVEDCC=${CC:-}
    if [ "$RUST_TARGET" = "armv7-unknown-linux-gnueabihf" ]; then
        unset CC
    fi
    cargo install --locked  \
        -j ${FDO_CI_CONCURRENT:-4} \
        --root /usr/local \
        ${EXTRA_CARGO_ARGS:-} \
        --path .
    CC=$SAVEDCC
else
    cargo install --locked  \
        -j ${FDO_CI_CONCURRENT:-4} \
        --root /usr/local --version 2.10.0 \
        cargo-ndk

    rustup target add $RUST_TARGET
    RUSTFLAGS='-C target-feature=+crt-static' cargo ndk --target $RUST_TARGET build --release

    mv target/$RUST_TARGET/release/deqp-runner /deqp-runner

    cargo uninstall --locked  \
        --root /usr/local \
        cargo-ndk
fi

popd
rm -rf deqp-runner-git
popd

# remove unused test runners to shrink images for the Mesa CI build (not kernel,
# which chooses its own deqp branch)
if [ -z "${DEQP_RUNNER_GIT_TAG:-}${DEQP_RUNNER_GIT_REV:-}" ]; then
    rm -f /usr/local/bin/igt-runner
fi
