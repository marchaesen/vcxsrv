set -e
set -v

ARTIFACTSDIR=`pwd`/shader-db
mkdir -p $ARTIFACTSDIR
export DRM_SHIM_DEBUG=true

LIBDIR=`pwd`/install/usr/local/lib
export LIBGL_DRIVERS_PATH=$LIBDIR/dri

cd /usr/local/shader-db

env LD_PRELOAD=$LIBDIR/libv3d_noop_drm_shim.so \
    ./run -j 4 ./shaders \
        > $ARTIFACTSDIR/v3d-shader-db.txt
