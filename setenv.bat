@echo off
set PATH=%PATH%;c:\windows\system32\Wbem
set > env_before.txt

pushd "c:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build"
CALL vcvarsall.bat %1 > nul
popd

set > env_after.txt

