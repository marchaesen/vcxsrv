Prerequisits:
- Visual Studio 2017 Community Edition
- Strawberry perl portable edition installed in c:\perl
- Cygwin (install the required tools when errors seen: nasm, flex, bison, python, ....)
- To build the installer: nsis

Run buildall.sh 1 9 to build the 64-bit version and buildall.sh 0 9 for the 32-bit.
(The second number on the command line is the number of parallel processes to use)

