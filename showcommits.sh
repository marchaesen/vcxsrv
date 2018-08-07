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


