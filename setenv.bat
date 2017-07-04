set PATH=%PATH%;c:\windows\system32\Wbem
set > env_before.txt
call "c:\Program Files (x86)\Microsoft Visual Studio\2017\Community\VC\Auxiliary\Build\vcvarsall.bat" %1
set > env_after.txt

