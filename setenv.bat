set PATH=%PATH%;c:\windows\system32\Wbem
set > env_before.txt

pushd "c:\Program Files (x86)\Microsoft Visual Studio\2019\Community\VC\Auxiliary\Build"
CALL vcvarsall.bat %1
popd

set > env_after.txt

