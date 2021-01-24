@echo off
pushd docker
echo Go for a nice two hour walk while this builds
docker build -m 4G -t vcxb .
