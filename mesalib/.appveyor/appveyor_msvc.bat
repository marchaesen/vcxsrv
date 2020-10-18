goto %1

:install
rem Check pip
python --version
python -m pip install --upgrade pip
python -m pip --version
if "%buildsystem%" == "scons" (
    rem Install Mako
    python -m pip install Mako==1.1.3
    rem Install pywin32 extensions, needed by SCons
    python -m pip install pypiwin32
    rem Install python wheels, necessary to install SCons via pip
    python -m pip install wheel
    rem Install SCons
    python -m pip install scons==3.1.2
    call scons --version
) else (
    python -m pip install Mako meson
    meson --version

    rem Install pkg-config, which meson requires even on windows
    cinst -y pkgconfiglite
)

rem Install flex/bison
set WINFLEXBISON_ARCHIVE=win_flex_bison-%WINFLEXBISON_VERSION%.zip
if not exist "%WINFLEXBISON_ARCHIVE%" appveyor DownloadFile "https://github.com/lexxmark/winflexbison/releases/download/v%WINFLEXBISON_VERSION%/%WINFLEXBISON_ARCHIVE%"
7z x -y -owinflexbison\ "%WINFLEXBISON_ARCHIVE%" > nul
set Path=%CD%\winflexbison;%Path%
win_flex --version
win_bison --version
rem Download and extract LLVM
if not exist "%LLVM_ARCHIVE%" appveyor DownloadFile "https://people.freedesktop.org/~jrfonseca/llvm/%LLVM_ARCHIVE%"
7z x -y "%LLVM_ARCHIVE%" > nul
if "%buildsystem%" == "scons" (
    mkdir llvm\bin
    set LLVM=%CD%\llvm
) else (
    move llvm subprojects\
    copy .appveyor\llvm-wrap.meson subprojects\llvm\meson.build
)
goto :eof

:build_script
if "%buildsystem%" == "scons" (
    call scons -j%NUMBER_OF_PROCESSORS% MSVC_VERSION=14.2 machine=x86 llvm=1
) else (
    call "C:\Program Files (x86)\Microsoft Visual Studio\2017\Community\Common7\Tools\VsDevCmd.bat" -arch=x86
    rem We use default-library as static to affect any wraps (such as expat and zlib)
    rem it would be better if we could set subprojects buildtype independently,
    rem but I haven't written that patch yet :)
    call meson builddir --backend=vs2017 --default-library=static -Dbuild-tests=true -Db_vscrt=mtd --buildtype=release -Dllvm=true -Dgallium-drivers=swrast -Dosmesa=gallium
    pushd builddir
    call msbuild mesa.sln /m
    popd
)
goto :eof

:test_script
if "%buildsystem%" == "scons" (
    call scons -j%NUMBER_OF_PROCESSORS% MSVC_VERSION=14.2 machine=x86 llvm=1 check
) else (
    call meson test -C builddir
)
goto :eof
