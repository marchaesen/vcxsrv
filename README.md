Windows X-server based on the xorg git sources (like xming or cygwin's xwin), but compiled with Visual Studio 2012 Community Edition.

Branches:

- released: contains original sources of all used packages.
- master: contains all necessary changes to be able to compile with Visual Studio. From this branch the binary released are build.

Currently compilation scripts assume they are run from a WSL terminal inside a windows folder (because a case insensitive filesystem is needed).
