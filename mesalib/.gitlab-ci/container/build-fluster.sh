#!/usr/bin/env bash

# shellcheck disable=SC1091 # The relative paths in this file only become valid at runtime.
# shellcheck disable=SC2034 # Variables are used in scripts called from here
# shellcheck disable=SC2086 # we want word splitting

# Install fluster in /usr/local.

FLUSTER_REVISION="e997402978f62428fffc8e5a4a709690d9ca9bc5"

git clone https://github.com/fluendo/fluster.git --single-branch --no-checkout

pushd fluster || exit
git checkout ${FLUSTER_REVISION}
popd || exit

if [ "${SKIP_UPDATE_FLUSTER_VECTORS}" != 1 ]; then
    # Download the necessary vectors: H264, H265 and VP9
    # When updating FLUSTER_REVISION, make sure to update the vectors if necessary or
    # fluster-runner will report Missing results.
    fluster/fluster.py download \
	JVT-AVC_V1 JVT-FR-EXT JVT-MVC JVT-SVC_V1 \
	JCT-VC-3D-HEVC JCT-VC-HEVC_V1 JCT-VC-MV-HEVC JCT-VC-RExt JCT-VC-SCC JCT-VC-SHVC \
	VP9-TEST-VECTORS-HIGH VP9-TEST-VECTORS

    # Build fluster vectors archive and upload it
    tar --zstd -cf "vectors.tar.zst" fluster/resources/
    ci-fairy s3cp --token-file "${S3_JWT_FILE}" "vectors.tar.zst" \
          "https://${S3_PATH_FLUSTER}/vectors.tar.zst"

    touch /lava-files/done
    ci-fairy s3cp --token-file "${S3_JWT_FILE}" /lava-files/done "https://${S3_PATH_FLUSTER}/done"

    # Don't include the vectors in the rootfs
    rm -fr fluster/resources/*
fi

mkdir -p "${ROOTFS}/usr/local/"
mv fluster "${ROOTFS}/usr/local/"

