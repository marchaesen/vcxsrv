@echo off
set drive=%~d0%
set pathpart=%~p0%
set map=%drive%%pathpart:~0,-1%
echo on

docker run -m 8G -v%map%:c:\src -it vcxb
