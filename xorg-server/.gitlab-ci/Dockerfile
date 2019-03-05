FROM debian:testing-slim

WORKDIR /tmp

RUN export DEBIAN_FRONTEND=noninteractive; \
    echo 'path-exclude=/usr/share/doc/*' > /etc/dpkg/dpkg.cfg.d/99-exclude-cruft && \
    echo 'path-exclude=/usr/share/locale/*' >> /etc/dpkg/dpkg.cfg.d/99-exclude-cruft && \
    echo 'path-exclude=/usr/share/man/*' >> /etc/dpkg/dpkg.cfg.d/99-exclude-cruft && \
    echo 'APT::Install-Recommends "false";' > /etc/apt/apt.conf && \
    echo '#!/bin/sh' > /usr/sbin/policy-rc.d && \
    echo 'exit 101' >> /usr/sbin/policy-rc.d && \
    chmod +x /usr/sbin/policy-rc.d && \
    echo 'deb-src https://deb.debian.org/debian testing main' >/etc/apt/sources.list.d/deb-src.list && \
    apt-get update && \
    apt-get install -y meson git ca-certificates ccache cmake automake autoconf libtool libwaffle-dev \
        libxkbcommon-dev python3-mako python3-numpy python3-six x11-utils x11-xserver-utils xauth xvfb && \
    apt-get build-dep -y xorg-server && \
    \
    cd /root && \
    git clone https://gitlab.freedesktop.org/mesa/piglit.git && cd piglit && \
    cmake -G Ninja -DPIGLIT_BUILD_GL_TESTS=OFF -DPIGLIT_BUILD_GLES1_TESTS=OFF \
        -DPIGLIT_BUILD_GLES2_TESTS=OFF -DPIGLIT_BUILD_GLES3_TESTS=OFF \
        -DPIGLIT_BUILD_DMA_BUF_TESTS=OFF -DPIGLIT_BUILD_GLX_TESTS=OFF && \
    ninja && \
    cd .. && \
    git clone https://gitlab.freedesktop.org/xorg/test/xts && \
    cd xts && ./autogen.sh && xvfb-run make -j$(nproc) && \
    cd .. && rm -rf piglit/.git xts/.git && \
    echo '[xts]' > piglit/piglit.conf && echo 'path=/root/xts' >> piglit/piglit.conf && \
    find -name \*.a -o -name \*.o | xargs rm && \
    \
    apt-get purge -y git cmake libwaffle-dev libxkbcommon-dev \
        x11-utils x11-xserver-utils xauth xvfb && \
    apt-get autoremove -y --purge && apt-get clean && \
    rm -f /var/lib/apt/lists/deb.debian.org_debian_dists_testing_*
