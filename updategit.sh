#!/bin/bash
if [ ! -d xserver          ]; then git clone https://gitlab.freedesktop.org/xorg/xserver.git                         ; fi
if [ ! -d libxcb           ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libxcb.git                      ; fi
if [ ! -d libxcb/xcb-proto ]; then git clone https://gitlab.freedesktop.org/xorg/proto/xcbproto.git libxcb/xcb-proto ; fi
if [ ! -d xkeyboard-config ]; then git clone https://gitlab.freedesktop.org/xkeyboard-config/xkeyboard-config.git    ; fi
if [ ! -d libX11           ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libx11.git                      ; fi
if [ ! -d libXdmcp         ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libxdmcp.git                    ; fi
if [ ! -d libXext          ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libxext.git                     ; fi
if [ ! -d libfontenc       ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libfontenc.git                  ; fi
if [ ! -d libXinerama      ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libxinerama.git                 ; fi
if [ ! -d libXau           ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libxau.git                      ; fi
if [ ! -d xkbcomp          ]; then git clone https://gitlab.freedesktop.org/xorg/app/xkbcomp.git                     ; fi
if [ ! -d pixman           ]; then git clone https://gitlab.freedesktop.org/pixman/pixman.git                        ; fi
if [ ! -d mkfontscale      ]; then git clone https://gitlab.freedesktop.org/xorg/app/mkfontscale.git                 ; fi
if [ ! -d xwininfo         ]; then git clone https://gitlab.freedesktop.org/xorg/app/xwininfo.git                    ; fi
if [ ! -d libXft           ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libxft.git                      ; fi
if [ ! -d libXmu           ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libxmu.git                      ; fi
if [ ! -d libxtrans        ]; then git clone https://gitlab.freedesktop.org/xorg/lib/libxtrans.git                   ; fi
if [ ! -d fontconfig       ]; then git clone https://gitlab.freedesktop.org/fontconfig/fontconfig.git                ; fi
if [ ! -d mesa             ]; then git clone https://gitlab.freedesktop.org/mesa/mesa.git                            ; fi
if [ ! -d putty            ]; then git clone git://git.tartarus.org/simon/putty.git                                  ; fi
if [ ! -d pthreads         ]; then git clone git://git.code.sf.net/p/pthreads4w/code pthreads                        ; fi
if [ ! -d EGL-Registry     ]; then git clone https://github.com/KhronosGroup/EGL-Registry.git                        ; fi
if [ ! -d xorgproto        ]; then git clone https://gitlab.freedesktop.org/xorg/proto/xorgproto.git                 ; fi

if [ -d xserver          ]; then echo Updating xserver          ; pushd xserver         > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libxcb           ]; then echo Updating libxcb           ; pushd libxcb          > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libxcb/xcb-proto ]; then echo Updating libxcb/xcb-proto ; pushd libxcb/xcb-proto> /dev/null ; git pull; popd > /dev/null ; fi
if [ -d xkeyboard-config ]; then echo Updating xkeyboard-config ; pushd xkeyboard-config> /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libX11           ]; then echo Updating libX11           ; pushd libX11          > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libXdmcp         ]; then echo Updating libXdmcp         ; pushd libXdmcp        > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libXext          ]; then echo Updating libXext          ; pushd libXext         > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libfontenc       ]; then echo Updating libfontenc       ; pushd libfontenc      > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libXinerama      ]; then echo Updating libXinerama      ; pushd libXinerama     > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libXau           ]; then echo Updating libXau           ; pushd libXau          > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d xkbcomp          ]; then echo Updating xkbcomp          ; pushd xkbcomp         > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d pixman           ]; then echo Updating pixman           ; pushd pixman          > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d mkfontscale      ]; then echo Updating mkfontscale      ; pushd mkfontscale     > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d xwininfo         ]; then echo Updating xwininfo         ; pushd xwininfo        > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libXft           ]; then echo Updating libXft           ; pushd libXft          > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libXmu           ]; then echo Updating libXmu           ; pushd libXmu          > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libxtrans        ]; then echo Updating libxtrans        ; pushd libxtrans       > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d fontconfig       ]; then echo Updating fontconfig       ; pushd fontconfig      > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d mesa             ]; then echo Updating mesa             ; pushd mesa            > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d putty            ]; then echo Updating putty            ; pushd putty           > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d pthreads         ]; then echo Updating pthreads         ; pushd pthreads        > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d EGL-Registry     ]; then echo Updating EGL-Registry     ; pushd EGL-Registry    > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d xorgproto        ]; then echo Updating xorgproto        ; pushd xorgproto       > /dev/null ; git pull; popd > /dev/null ; fi

../vcxsrv.released/synchronise.py -e xserver ../vcxsrv.released/xorg-server --skip-dir=fonts.src --skip-dir=bitmaps --skip-dir=xkeyboard-config
../vcxsrv.released/synchronise.py -e libxcb ../vcxsrv.released/libxcb
../vcxsrv.released/synchronise.py -e xkeyboard-config ../vcxsrv.released/xorg-server/xkeyboard-config
../vcxsrv.released/synchronise.py -e libX11 ../vcxsrv.released/libX11
../vcxsrv.released/synchronise.py -e libXdmcp ../vcxsrv.released/libXdmcp
../vcxsrv.released/synchronise.py -e libXext ../vcxsrv.released/libXext
../vcxsrv.released/synchronise.py -e libfontenc ../vcxsrv.released/libfontenc
../vcxsrv.released/synchronise.py -e libXinerama ../vcxsrv.released/libXinerama
../vcxsrv.released/synchronise.py -e libXau ../vcxsrv.released/libXau
../vcxsrv.released/synchronise.py -e xkbcomp ../vcxsrv.released/xkbcomp
../vcxsrv.released/synchronise.py -e pixman ../vcxsrv.released/pixman
../vcxsrv.released/synchronise.py -e mkfontscale        ../vcxsrv.released/mkfontscale
../vcxsrv.released/synchronise.py -e xwininfo           ../vcxsrv.released/apps/xwininfo
../vcxsrv.released/synchronise.py -e fontconfig         ../vcxsrv.released/fontconfig
../vcxsrv.released/synchronise.py -e libXft             ../vcxsrv.released/libXft
../vcxsrv.released/synchronise.py -e libXmu             ../vcxsrv.released/libXmu
../vcxsrv.released/synchronise.py -e libxtrans          ../vcxsrv.released/X11/xtrans
../vcxsrv.released/synchronise.py -e mesa ../vcxsrv.released/mesalib --skip-dir=tests  --skip-dir=gtest --skip-dir=x86-64 --skip-dir=tnl_dd --skip-dir=sparc --skip-dir=tools --skip-dir=libdricore --skip-dir=x11 --skip-dir=osmesa --skip-dir=radeon --skip-dir=r200 --skip-dir=nouveau --skip-dir=intel --skip-dir=i965 --skip-dir=i915 --skip-dir=vgapi --skip-dir=shared-glapi --skip-dir=es1api  --skip-dir=es2api --skip-dir=gtest --skip-dir=glx --skip-dir=builtins --skip-dir=gbm --skip-dir=getopt --skip-dir=egl --skip-dir=gallivm --skip-dir=doxygen --skip-dir=OLD --skip-dir=CL --skip-dir=c99
../vcxsrv.released/synchronise.py -e putty ../vcxsrv.released/tools/plink
../vcxsrv.released/synchronise.py -e pthreads          ../vcxsrv.released/pthreads
../vcxsrv.released/synchronise.py -e EGL-Registry/api/KHR ../vcxsrv.released/include/KHR
../vcxsrv.released/synchronise.py -e xorgproto/include/X11 ../vcxsrv.released/X11 --skip-file=meson.build --skip-dir=xtrans
../vcxsrv.released/synchronise.py -e xorgproto/include/GL ../vcxsrv.released/gl/include/GL --skip-file=meson.build

# show all latest commit hashes
if [ -d xserver          ]; then  pushd xserver         > /dev/null ; echo "xserver         " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libxcb           ]; then  pushd libxcb          > /dev/null ; echo "libxcb          " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libxcb/xcb-proto ]; then  pushd libxcb/xcb-proto> /dev/null ; echo "libxcb/xcb-proto" `git log | head -n1`; popd > /dev/null ; fi
if [ -d xkeyboard-config ]; then  pushd xkeyboard-config> /dev/null ; echo "xkeyboard-config" `git log | head -n1`; popd > /dev/null ; fi
if [ -d libX11           ]; then  pushd libX11          > /dev/null ; echo "libX11          " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libXdmcp         ]; then  pushd libXdmcp        > /dev/null ; echo "libXdmcp        " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libXext          ]; then  pushd libXext         > /dev/null ; echo "libXext         " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libfontenc       ]; then  pushd libfontenc      > /dev/null ; echo "libfontenc      " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libXinerama      ]; then  pushd libXinerama     > /dev/null ; echo "libXinerama     " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libXau           ]; then  pushd libXau          > /dev/null ; echo "libXau          " `git log | head -n1`; popd > /dev/null ; fi
if [ -d xkbcomp          ]; then  pushd xkbcomp         > /dev/null ; echo "xkbcomp         " `git log | head -n1`; popd > /dev/null ; fi
if [ -d pixman           ]; then  pushd pixman          > /dev/null ; echo "pixman          " `git log | head -n1`; popd > /dev/null ; fi
if [ -d mkfontscale      ]; then  pushd mkfontscale     > /dev/null ; echo "mkfontscale     " `git log | head -n1`; popd > /dev/null ; fi
if [ -d xwininfo         ]; then  pushd xwininfo        > /dev/null ; echo "xwininfo        " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libXft           ]; then  pushd libXft          > /dev/null ; echo "libXft          " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libXmu           ]; then  pushd libXmu          > /dev/null ; echo "libXmu          " `git log | head -n1`; popd > /dev/null ; fi
if [ -d libxtrans        ]; then  pushd libxtrans       > /dev/null ; echo "libxtrans       " `git log | head -n1`; popd > /dev/null ; fi
if [ -d fontconfig       ]; then  pushd fontconfig      > /dev/null ; echo "fontconfig      " `git log | head -n1`; popd > /dev/null ; fi
if [ -d mesa             ]; then  pushd mesa            > /dev/null ; echo "mesa            " `git log | head -n1`; popd > /dev/null ; fi
if [ -d putty            ]; then  pushd putty           > /dev/null ; echo "putty           " `git log | head -n1`; popd > /dev/null ; fi
if [ -d pthreads         ]; then  pushd pthreads        > /dev/null ; echo "pthreads        " `git log | head -n1`; popd > /dev/null ; fi
if [ -d EGL-Registry     ]; then  pushd EGL-Registry    > /dev/null ; echo "EGL-Registry    " `git log | head -n1`; popd > /dev/null ; fi
if [ -d xorgproto        ]; then  pushd xorgproto       > /dev/null ; echo "xorgproto       " `git log | head -n1`; popd > /dev/null ; fi


