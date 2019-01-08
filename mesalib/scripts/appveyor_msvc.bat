goto %1

:install
rem Check git config
git config core.autocrlf
rem Check pip
python --version
python -m pip --version
rem Install Mako
python -m pip install Mako==1.0.7
rem Install pywin32 extensions, needed by SCons
python -m pip install pypiwin32
rem Install python wheels, necessary to install SCons via pip
python -m pip install wheel
rem Install SCons
python -m pip install scons==3.0.1
call scons --version
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
mkdir llvm\bin
set LLVM=%CD%\llvm
goto :eof

:build_script
call scons -j%NUMBER_OF_PROCESSORS% MSVC_VERSION=14.1 llvm=1
goto :eof

:after_build
call scons -j%NUMBER_OF_PROCESSORS% MSVC_VERSION=14.1 llvm=1 check
goto :eof
