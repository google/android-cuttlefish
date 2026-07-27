#!/bin/bash

# Download Cuttlefish

set -o errexit

URL=https://ci.android.com/builds/latest/branches/aosp-android-latest-release/targets/aosp_cf_arm64_only_phone-userdebug/view/BUILD_INFO
RURL=$(curl -Ls -o /dev/null -w %{url_effective} ${URL})
echo "RURL = ${RURL}"

BUILD_ID=$(echo "${RURL}" | sed -n 's/.*\/builds\/submitted\/\([^\/]*\)\/.*/\1/p')
echo "BUILD_ID = ${BUILD_ID}"

if [ x"${BUILD_ID}" = x"" ]; then
    echo "Error: BUILD_ID empty."
    exit 1
fi

FILENAME="aosp_cf_arm64_only_phone-img-${BUILD_ID}.zip"
echo "FILENAME = ${FILENAME}"

if [ x"${FILENAME}" = x"" ]; then
    echo "Error: FILENAME empty."
    exit 1
fi

wget -nv -c ${RURL%/view/BUILD_INFO}/raw/${FILENAME}
wget -nv -c ${RURL%/view/BUILD_INFO}/raw/cvd-host_package.tar.gz

exit 0
