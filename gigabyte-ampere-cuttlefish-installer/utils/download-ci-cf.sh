#!/bin/bash

# Download Cuttlefish

set -o errexit

BRANCH=aosp-android-latest-release
TARGET=aosp_cf_arm64_only_phone-userdebug
BUILD_ID=$(curl -fsS \
    "https://ci.android.com/builds/branches/${BRANCH}/targets/${TARGET}/status.json" \
    | sed -n 's/.*[{,[:space:]]"last_known_good_build"[[:space:]]*:[[:space:]]*"\([0-9][0-9]*\)".*/\1/p')
echo "BUILD_ID = ${BUILD_ID}"

if [[ -z "${BUILD_ID}" ]]; then
    echo "Error: BUILD_ID empty."
    exit 1
fi

FILENAME="aosp_cf_arm64_only_phone-img-${BUILD_ID}.zip"
echo "FILENAME = ${FILENAME}"

RAWURL="https://ci.android.com/builds/submitted/${BUILD_ID}/${TARGET}/latest/raw"

wget -nv -c ${RAWURL}/${FILENAME}
wget -nv -c ${RAWURL}/cvd-host_package.tar.gz

exit 0
