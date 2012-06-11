synchronise -e xserver ..\released\xorg-server --skip-dir=fonts.src --skip-dir=bitmaps --skip-dir=xkeyboard-config
synchronise -e libxcb ..\released\libxcb
synchronise -e xkeyboard-config ..\released\xorg-server\xkeyboard-config
synchronise -e libX11 ..\released\libX11
synchronise -e libXdmcp ..\released\libXdmcp
synchronise -e libXext ..\released\libXext
synchronise -e libfontenc ..\released\libfontenc
synchronise -e libXinerama ..\released\libXinerama
synchronise -e libXau ..\released\libXau
synchronise -e xkbcomp ..\released\xkbcomp
synchronise -e pixman ..\released\pixman
synchronise xextproto ..\released\X11\extensions --skip-dir=specs --skip-file=COPYING --skip-file=docbook.am --skip-file=autogen.sh --skip-file=configure.ac --skip-file=Makefile.am --skip-file=README --skip-file=.gitignore
synchronise randrproto ..\released\X11\extensions --skip-file=autogen.sh --skip-file=configure.ac --skip-file=COPYING --skip-file=Makefile.am --skip-file=README --skip-file=.gitignore
synchronise -e glproto            ..\released\gl
synchronise -e mkfontscale        ..\released\mkfontscale
synchronise -e xwininfo           ..\released\apps\xwininfo
synchronise -e fontconfig         ..\released\fontconfig
synchronise -e libXft             ..\released\libXft                                        
synchronise -e libXmu             ..\released\libXmu
synchronise -e libxtrans          ..\released\X11\xtrans
synchronise -e mesa ..\released\mesalib --skip-dir=tests  --skip-dir=gtest --skip-dir=x86-64 --skip-dir=tnl_dd --skip-dir=sparc --skip-dir=tools --skip-dir=libdricore --skip-dir=x11 --skip-dir=osmesa --skip-dir=radeon --skip-dir=r200 --skip-dir=nouveau --skip-dir=intel --skip-dir=i965 --skip-dir=i915 --skip-dir=vgapi --skip-dir=shared-glapi --skip-dir=es1api  --skip-dir=es2api --skip-dir=gtest --skip-dir=glx --skip-dir=builtins --skip-dir=vl --skip-dir=gallium\docs --skip-dir=gallium\drivers --skip-dir=gallium\include --skip-dir=gallium\state_trackers --skip-dir=gallium\targets --skip-dir=gallium\winsys --skip-dir=gbm --skip-dir=getopt --skip-dir=egl --skip-dir=cso_cache --skip-dir=target-helpers --skip-dir=tgsi --skip-dir=translate --skip-dir=rtasm --skip-dir=pipebuffer --skip-dir=postprocess --skip-dir=rbug --skip-dir=pipe-loader --skip-dir=os --skip-dir=indices --skip-dir=gallivm --skip-dir=draw --skip-dir=pci_ids --skip-dir=doxygen --skip-dir=OLD --skip-dir=CL --skip-dir=c99
