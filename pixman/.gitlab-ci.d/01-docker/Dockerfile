ARG BASE_IMAGE=docker.io/debian
ARG BASE_IMAGE_TAG=bookworm-slim
FROM ${BASE_IMAGE}:${BASE_IMAGE_TAG} AS base

LABEL org.opencontainers.image.title="Pixman build environment for platform coverage" \
      org.opencontainers.image.authors="Marek Pikuła <m.pikula@partner.samsung.com>"

ARG DEBIAN_FRONTEND=noninteractive
ENV APT_UPDATE="apt-get update" \
    APT_INSTALL="apt-get install -y --no-install-recommends" \
    APT_CLEANUP="rm -rf /var/lib/apt/lists/* /var/cache/apt/archives/*"

RUN ${APT_UPDATE} \
    && ${APT_INSTALL} \
        # Build dependencies.
        build-essential \
        ninja-build \
        pkg-config \
        qemu-user \
        # pipx dependencies.
        python3-argcomplete \
        python3-packaging \
        python3-pip \
        python3-platformdirs \
        python3-userpath \
        python3-venv \
        # gcovr dependencies.
        libxml2-dev \
        libxslt-dev \
        python3-dev \
    && ${APT_CLEANUP} \
    # Install pipx using pip to have a more recent version of pipx, which
    # supports the `--global` flag.
    && pip install pipx --break-system-packages \
    # Install a recent version of meson and gcovr using pipx to have the same
    # version across all variants regardless of base.
    && pipx install --global \
        gcovr \
        meson \
    && gcovr --version \
    && echo Meson version: \
    && meson --version

FROM base AS llvm-base
# LLVM 16 is the highest available in Bookworm. Preferably, we should use the
# same version for all platforms, but it's not possible at the moment.
ARG LLVM_VERSION=16
RUN ${APT_UPDATE} \
    && ${APT_INSTALL} \
        clang-${LLVM_VERSION} \
        libclang-rt-${LLVM_VERSION}-dev \
        lld-${LLVM_VERSION} \
        llvm-${LLVM_VERSION} \
    && ${APT_CLEANUP} \
    && ln -f /usr/bin/clang-${LLVM_VERSION} /usr/bin/clang \
    && ln -f /usr/bin/lld-${LLVM_VERSION} /usr/bin/lld \
    && ln -f /usr/bin/llvm-ar-${LLVM_VERSION} /usr/bin/llvm-ar \
    && ln -f /usr/bin/llvm-strip-${LLVM_VERSION} /usr/bin/llvm-strip

FROM llvm-base AS native-base
ARG LLVM_VERSION=16
RUN ${APT_UPDATE} \
    && ${APT_INSTALL} \
        # Runtime library dependencies.
        libglib2.0-dev \
        libgtk-3-dev \
        libpng-dev \
    # Install libomp-dev if available (OpenMP support for LLVM). It's done only
    # for the native images, as OpenMP support in cross-build environment is
    # tricky for LLVM.
    && (${APT_INSTALL} libomp-${LLVM_VERSION}-dev \
        || echo "OpenMP not available on this platform.") \
    && ${APT_CLEANUP}

# The following targets differ in BASE_IMAGE.
FROM native-base AS linux-386
FROM native-base AS linux-amd64
FROM native-base AS linux-arm-v5
FROM native-base AS linux-arm-v7
FROM native-base AS linux-arm64-v8
FROM native-base AS linux-mips64el
FROM native-base AS linux-mipsel
FROM native-base AS linux-ppc64le
FROM native-base AS linux-riscv64

# The following targets should have a common BASE_IMAGE.
FROM llvm-base AS linux-mips
RUN ${APT_UPDATE} \
    && ${APT_INSTALL} gcc-multilib-mips-linux-gnu \
    && ${APT_CLEANUP}

FROM llvm-base AS linux-ppc
RUN ${APT_UPDATE} \
    && ${APT_INSTALL} gcc-multilib-powerpc-linux-gnu \
    && ${APT_CLEANUP}

FROM llvm-base AS linux-ppc64
RUN ${APT_UPDATE} \
    && ${APT_INSTALL} gcc-multilib-powerpc64-linux-gnu \
    && ${APT_CLEANUP}

# We use a common image for Windows i686 and amd64, as it doesn't make sense to
# make them separate in terms of build time and image size. After two runs they
# should use the same cache layers, so in the end it makes the collective image
# size smaller.
FROM base AS windows-base
ARG LLVM_MINGW_RELEASE=20240619
ARG LLVM_MINGW_VARIANT=llvm-mingw-${LLVM_MINGW_RELEASE}-msvcrt-ubuntu-20.04-x86_64
RUN ${APT_UPDATE} \
    && ${APT_INSTALL} wget \
    && ${APT_CLEANUP} \
    && cd /opt \
    && wget https://github.com/mstorsjo/llvm-mingw/releases/download/${LLVM_MINGW_RELEASE}/${LLVM_MINGW_VARIANT}.tar.xz \
    && tar -xf ${LLVM_MINGW_VARIANT}.tar.xz \
    && rm -f ${LLVM_MINGW_VARIANT}.tar.xz
ENV PATH=${PATH}:/opt/${LLVM_MINGW_VARIANT}/bin

FROM windows-base AS windows-x86-base
RUN dpkg --add-architecture i386 \
    && ${APT_UPDATE} \
    && ${APT_INSTALL} \
        gcc-mingw-w64-i686 \
        gcc-mingw-w64-x86-64 \
        mingw-w64-tools \
        procps \
        wine \
        wine32 \
        wine64 \
    && ${APT_CLEANUP} \
    # Inspired by https://code.videolan.org/videolan/docker-images
    && wine wineboot --init \
    && while pgrep wineserver > /dev/null; do \
        echo "waiting ..."; \
        sleep 1; \
    done \
    && rm -rf /tmp/wine-*

FROM windows-x86-base AS windows-686
FROM windows-x86-base AS windows-amd64

# aarch64 image requires linaro/wine-arm64 as a base.
FROM windows-base AS windows-arm64-v8
RUN wine-arm64 wineboot --init \
    && while pgrep wineserver > /dev/null; do \
        echo "waiting ..."; \
        sleep 1; \
    done \
    && rm -rf /tmp/wine-*
