set -e
set -v

ARTIFACTSDIR=`pwd`/shader-db
mkdir -p $ARTIFACTSDIR
export DRM_SHIM_DEBUG=true

LIBDIR=`pwd`/install/lib
export LD_LIBRARY_PATH=$LIBDIR

cd /usr/local/shader-db

for driver in freedreno v3d; do
    env LD_PRELOAD=$LIBDIR/lib${driver}_noop_drm_shim.so \
        ./run -j 4 ./shaders \
            > $ARTIFACTSDIR/${driver}-shader-db.txt
done
