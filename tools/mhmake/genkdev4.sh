[ ! -d build ] && mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Release ..
cd ..
[ ! -d build.dbg ] && mkdir build.dbg
cd build.dbg
cmake -DCMAKE_INSTALL_PREFIX=/usr/local -DCMAKE_BUILD_TYPE=Debug ..
cd ..
[ ! -d .kdev4 ] && mkdir .kdev4
echo [CMake] > .kdev4/mhmake.kdev4
echo Build Directory Count=2 >> .kdev4/mhmake.kdev4
echo >> .kdev4/mhmake.kdev4

echo [CMake][CMake Build Directory 0] >> .kdev4/mhmake.kdev4
echo Build Directory Path=file://$PWD/build/build.dbg >> .kdev4/mhmake.kdev4
echo Build Type=Debug >> .kdev4/mhmake.kdev4
echo Install Directory=file:///usr/local >> .kdev4/mhmake.kdev4
echo >> .kdev4/mhmake.kdev4

echo [CMake][CMake Build Directory 1] >> .kdev4/mhmake.kdev4
echo Build Directory Path=file://$PWD/build/build >> .kdev4/mhmake.kdev4
echo Build Type=Release >> .kdev4/mhmake.kdev4
echo Install Directory=file:///usr/local >> .kdev4/mhmake.kdev4
