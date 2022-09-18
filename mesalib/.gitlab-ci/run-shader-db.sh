set -e
set -v

ARTIFACTSDIR=`pwd`/shader-db
mkdir -p $ARTIFACTSDIR
export DRM_SHIM_DEBUG=true

LIBDIR=`pwd`/install/lib
export LD_LIBRARY_PATH=$LIBDIR

cd /usr/local/shader-db

for driver in freedreno intel v3d; do
    echo "Running drm-shim for $driver"
    env LD_PRELOAD=$LIBDIR/lib${driver}_noop_drm_shim.so \
        ./run -j${FDO_CI_CONCURRENT:-4} ./shaders \
            > $ARTIFACTSDIR/${driver}-shader-db.txt
done

# Run shader-db over a number of supported chipsets for nouveau
for chipset in 40 a3 c0 e4 f0 134 162; do
    echo "Running drm-shim for nouveau - $chipset"
    env LD_PRELOAD=$LIBDIR/libnouveau_noop_drm_shim.so \
        NOUVEAU_CHIPSET=${chipset} \
        ./run -j${FDO_CI_CONCURRENT:-4} ./shaders \
            > $ARTIFACTSDIR/nouveau-${chipset}-shader-db.txt
done
