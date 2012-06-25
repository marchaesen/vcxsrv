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
echo BuildDirs[\$e]=$PWD/build,$PWD/build.dbg >> .kdev4/mhmake.kdev4

