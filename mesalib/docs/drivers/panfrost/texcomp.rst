Compressed texture support
==========================

In the driver, Panfrost supports ASTC, ETC, and all BCn formats (e.g. RGTC,
S3TC, etc.) However, Panfrost depends on the hardware to support these formats
efficiently.  All supported Mali architectures support these formats, but not
every system-on-chip with a Mali GPU support all these formats. Many lower-end
systems lack support for some BCn formats, which can cause problems when playing
desktop games with Panfrost. To check whether this issue applies to your
system-on-chip, Panfrost includes a ``panfrost_texfeatures`` tool to query
supported formats.

To use this tool, include the option ``-Dtools=panfrost`` when configuring Mesa.
Then inside your Mesa build directory, the tool is located at
``src/panfrost/tools/panfrost_texfeatures``. Copy it to your target device,
set as executable as necessary, and run on the target device. A table of
supported formats will be printed to standard output.
