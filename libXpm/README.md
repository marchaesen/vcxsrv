libXpm - X Pixmap (XPM) image file format library
-------------------------------------------------

All questions regarding this software should be directed at the
Xorg mailing list:

  https://lists.x.org/mailman/listinfo/xorg

The primary development code repository can be found at:

  https://gitlab.freedesktop.org/xorg/lib/libXpm

Please submit bug reports and requests to merge patches there.

For patch submission instructions, see:

  https://www.x.org/wiki/Development/Documentation/SubmittingPatches

------------------------------------------------------------------------------

libXpm supports two optional features to handle compressed pixmap files.

--enable-open-zfile makes libXpm recognize file names ending in .Z and .gz
and open a pipe to the appropriate command to compress the file when writing
and uncompress the file when reading. This is enabled by default on platforms
other than MinGW and can be disabled by passing the --disable-open-zfile flag
to the configure script.

--enable-stat-zfile make libXpm search for a file name with .Z or .gz added
if it can't find the file it was asked to open.  It relies on the
--enable-open-zfile feature to open the file, and is enabled by default
when --enable-open-zfile is enabled, and can be disabled by passing the
--disable-stat-zfile flag to the configure script.

All of these commands will be executed with whatever userid & privileges the
function is called with, relying on the caller to ensure the correct euid,
egid, etc. are set before calling.

To reduce risk, the paths to these commands are now set at configure time to
the first version found in the PATH used to run configure, and do not depend
on the PATH environment variable set at runtime.

To specify paths to be used for these commands instead of searching $PATH, pass
the XPM_PATH_COMPRESS, XPM_PATH_UNCOMPRESS, and XPM_PATH_GZIP
variables to the configure command.
