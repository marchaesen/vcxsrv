call "C:\Program Files (x86)\Microsoft Visual Studio\%VERSION%\Common7\Tools\VsDevCmd.bat" -arch=%ARCH%

del /Q /S _build
meson _build ^
        -Dbuild-tests=true ^
        -Db_vscrt=mtd ^
        -Dbuildtype=release ^
        -Dllvm=false ^
        -Dgallium-drivers=swrast ^
        -Dosmesa=gallium
meson configure _build
ninja -C _build
ninja -C _build test
