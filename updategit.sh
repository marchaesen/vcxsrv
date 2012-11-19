if [ ! -d xserver          ]; then git clone git://anongit.freedesktop.org/xorg/xserver               ; fi
if [ ! -d libxcb           ]; then git clone git://anongit.freedesktop.org/xcb/libxcb                 ; fi
if [ ! -d libxcb/xcb-proto ]; then git clone git://anongit.freedesktop.org/xcb/proto libxcb/xcb-proto ; fi
if [ ! -d xkeyboard-config ]; then git clone git://anongit.freedesktop.org/xkeyboard-config           ; fi
if [ ! -d libX11           ]; then git clone git://anongit.freedesktop.org/xorg/lib/libX11            ; fi
if [ ! -d libXdmcp         ]; then git clone git://anongit.freedesktop.org/xorg/lib/libXdmcp          ; fi
if [ ! -d libXext          ]; then git clone git://anongit.freedesktop.org/xorg/lib/libXext           ; fi
if [ ! -d libfontenc       ]; then git clone git://anongit.freedesktop.org/xorg/lib/libfontenc        ; fi
if [ ! -d libXinerama      ]; then git clone git://anongit.freedesktop.org/xorg/lib/libXinerama       ; fi
if [ ! -d libXau           ]; then git clone git://anongit.freedesktop.org/xorg/lib/libXau            ; fi
if [ ! -d xkbcomp          ]; then git clone git://anongit.freedesktop.org/xorg/app/xkbcomp           ; fi
if [ ! -d pixman           ]; then git clone git://anongit.freedesktop.org/pixman                     ; fi
if [ ! -d xextproto        ]; then git clone git://anongit.freedesktop.org/xorg/proto/xextproto       ; fi
if [ ! -d randrproto       ]; then git clone git://anongit.freedesktop.org/xorg/proto/randrproto      ; fi
if [ ! -d glproto          ]; then git clone git://anongit.freedesktop.org/xorg/proto/glproto         ; fi
if [ ! -d mkfontscale      ]; then git clone git://anongit.freedesktop.org/xorg/app/mkfontscale       ; fi
if [ ! -d xwininfo         ]; then git clone git://anongit.freedesktop.org/xorg/app/xwininfo          ; fi
if [ ! -d libXft           ]; then git clone git://anongit.freedesktop.org/xorg/lib/libXft            ; fi
if [ ! -d libXmu           ]; then git clone git://anongit.freedesktop.org/xorg/lib/libXmu            ; fi
if [ ! -d libxtrans        ]; then git clone git://anongit.freedesktop.org/xorg/lib/libxtrans         ; fi
if [ ! -d fontconfig       ]; then git clone git://anongit.freedesktop.org/fontconfig                 ; fi
if [ ! -d mesa             ]; then git clone git://anongit.freedesktop.org/git/mesa/mesa              ; fi

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
if [ -d xextproto        ]; then echo Updating xextproto        ; pushd xextproto       > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d randrproto       ]; then echo Updating randrproto       ; pushd randrproto      > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d glproto          ]; then echo Updating glproto          ; pushd glproto         > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d mkfontscale      ]; then echo Updating mkfontscale      ; pushd mkfontscale     > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d xwininfo         ]; then echo Updating xwininfo         ; pushd xwininfo        > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libXft           ]; then echo Updating libXft           ; pushd libXft          > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libXmu           ]; then echo Updating libXmu           ; pushd libXmu          > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d libxtrans        ]; then echo Updating libxtrans        ; pushd libxtrans       > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d fontconfig       ]; then echo Updating fontconfig       ; pushd fontconfig      > /dev/null ; git pull; popd > /dev/null ; fi
if [ -d mesa             ]; then echo Updating mesa             ; pushd mesa            > /dev/null ; git pull; popd > /dev/null ; fi

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
../vcxsrv.released/synchronise.py xextproto ../vcxsrv.released/X11/extensions --skip-dir=specs --skip-file=COPYING --skip-file=docbook.am --skip-file=autogen.sh --skip-file=configure.ac --skip-file=Makefile.am --skip-file=README --skip-file=.gitignore
../vcxsrv.released/synchronise.py randrproto ../vcxsrv.released/X11/extensions --skip-file=autogen.sh --skip-file=configure.ac --skip-file=COPYING --skip-file=Makefile.am --skip-file=README --skip-file=.gitignore
../vcxsrv.released/synchronise.py -e glproto            ../vcxsrv.released/gl
../vcxsrv.released/synchronise.py -e mkfontscale        ../vcxsrv.released/mkfontscale
../vcxsrv.released/synchronise.py -e xwininfo           ../vcxsrv.released/apps/xwininfo
../vcxsrv.released/synchronise.py -e fontconfig         ../vcxsrv.released/fontconfig
../vcxsrv.released/synchronise.py -e libXft             ../vcxsrv.released/libXft
../vcxsrv.released/synchronise.py -e libXmu             ../vcxsrv.released/libXmu
../vcxsrv.released/synchronise.py -e libxtrans          ../vcxsrv.released/X11/xtrans
../vcxsrv.released/synchronise.py -e mesa ../vcxsrv.released/mesalib --skip-dir=tests  --skip-dir=gtest --skip-dir=x86-64 --skip-dir=tnl_dd --skip-dir=sparc --skip-dir=tools --skip-dir=libdricore --skip-dir=x11 --skip-dir=osmesa --skip-dir=radeon --skip-dir=r200 --skip-dir=nouveau --skip-dir=intel --skip-dir=i965 --skip-dir=i915 --skip-dir=vgapi --skip-dir=shared-glapi --skip-dir=es1api  --skip-dir=es2api --skip-dir=gtest --skip-dir=glx --skip-dir=builtins --skip-dir=vl --skip-dir=gallium/docs --skip-dir=gallium/drivers --skip-dir=gallium/include --skip-dir=gallium/state_trackers --skip-dir=gallium/targets --skip-dir=gallium/winsys --skip-dir=gbm --skip-dir=getopt --skip-dir=egl --skip-dir=cso_cache --skip-dir=target-helpers --skip-dir=tgsi --skip-dir=translate --skip-dir=rtasm --skip-dir=pipebuffer --skip-dir=postprocess --skip-dir=rbug --skip-dir=pipe-loader --skip-dir=os --skip-dir=indices --skip-dir=gallivm --skip-dir=draw --skip-dir=pci_ids --skip-dir=doxygen --skip-dir=OLD --skip-dir=CL --skip-dir=c99

