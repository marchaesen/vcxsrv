#!/usr/bin/env bash

# Check to see if we need a separate repo to install LLVM.

case "${FDO_DISTRIBUTION_VERSION%-*},${LLVM_VERSION}" in
  bookworm,15)
    NEED_LLVM_REPO="false"
    ;;
  *)
    NEED_LLVM_REPO="true"
    ;;
esac

if [ "$NEED_LLVM_REPO" = "true" ]; then
  curl -s https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
  export LLVM_APT_REPO="deb [trusted=yes] https://apt.llvm.org/${FDO_DISTRIBUTION_VERSION%-*}/ llvm-toolchain-${FDO_DISTRIBUTION_VERSION%-*}-${LLVM_VERSION} main"
  echo "$LLVM_APT_REPO" | tee /etc/apt/sources.list.d/llvm.list
fi
