### radeon_noop backend

This implements the minimum of the radeon kernel driver in order to make shader-db work.
The submit ioctl is stubbed out to not execute anything.

Export `MESA_LOADER_DRIVER_OVERRIDE=r300
LD_PRELOAD=$prefix/lib/libradeon_noop_drm_shim.so`. (or r600 for r600-class HW)

By default, rv515 is exposed.  The chip can be selected an enviornment
variable like `RADEON_GPU_ID=CAYMAN` or `RADEON_GPU_ID=0x6740`.
