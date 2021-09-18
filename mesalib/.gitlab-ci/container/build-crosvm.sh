#!/bin/bash

set -ex

# Pull down repositories that crosvm depends on to cros checkout-like locations.
CROS_ROOT=/
THIRD_PARTY_ROOT=$CROS_ROOT/third_party
mkdir -p $THIRD_PARTY_ROOT
AOSP_EXTERNAL_ROOT=$CROS_ROOT/aosp/external
mkdir -p $AOSP_EXTERNAL_ROOT
PLATFORM2_ROOT=/platform2

PLATFORM2_COMMIT=72e56e66ccf3d2ea48f5686bd1f772379c43628b
git clone --single-branch --no-checkout https://chromium.googlesource.com/chromiumos/platform2 $PLATFORM2_ROOT
pushd $PLATFORM2_ROOT
git checkout $PLATFORM2_COMMIT
popd

# minijail does not exist in upstream linux distros.
MINIJAIL_COMMIT=debdf5de5a0ae3b667bee2f8fb1f755b0b3f5a6c
git clone --single-branch --no-checkout https://android.googlesource.com/platform/external/minijail $AOSP_EXTERNAL_ROOT/minijail
pushd $AOSP_EXTERNAL_ROOT/minijail
git checkout $MINIJAIL_COMMIT
make
cp libminijail.so /usr/lib/x86_64-linux-gnu/
popd

# Pull the cras library for audio access.
ADHD_COMMIT=a1e0869b95c845c4fe6234a7b92fdfa6acc1e809
git clone --single-branch --no-checkout https://chromium.googlesource.com/chromiumos/third_party/adhd $THIRD_PARTY_ROOT/adhd
pushd $THIRD_PARTY_ROOT/adhd
git checkout $ADHD_COMMIT
popd

# Pull vHost (dataplane for virtio backend drivers)
VHOST_COMMIT=3091854e27242d09453004b011f701fa29c0b8e8
git clone --single-branch --no-checkout https://chromium.googlesource.com/chromiumos/third_party/rust-vmm/vhost $THIRD_PARTY_ROOT/rust-vmm/vhost
pushd $THIRD_PARTY_ROOT/rust-vmm/vhost
git checkout $VHOST_COMMIT
popd

CROSVM_VERSION=e42a43d880b0364b55559dbeade3af174f929001
git clone --single-branch --no-checkout https://chromium.googlesource.com/chromiumos/platform/crosvm /platform/crosvm
pushd /platform/crosvm
git checkout "$CROSVM_VERSION"

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  bindgen \
  -j ${FDO_CI_CONCURRENT:-4} \
  --root /usr/local \
  $EXTRA_CARGO_ARGS

RUSTFLAGS='-L native=/usr/local/lib' cargo install \
  -j ${FDO_CI_CONCURRENT:-4} \
  --locked \
  --features 'default-no-sandbox gpu x virgl_renderer virgl_renderer_next' \
  --path . \
  --root /usr/local \
  $EXTRA_CARGO_ARGS

popd

rm -rf $PLATFORM2_ROOT $AOSP_EXTERNAL_ROOT/minijail $THIRD_PARTY_ROOT/adhd $THIRD_PARTY_ROOT/rust-vmm /platform/crosvm
