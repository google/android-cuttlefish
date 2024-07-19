#!/bin/bash

# Download Cuttlefish

set -o errexit

URL=https://ci.android.com/builds/latest/branches/aosp-master/targets/aosp_cf_arm64_only_phone-userdebug/view/BUILD_INFO
RURL=$(curl -Ls -o /dev/null -w %{url_effective} ${URL})
echo $RURL

FILENAME=$(wget -nv -O - ${RURL%/view/BUILD_INFO}/ | grep aosp_cf_arm64_only_phone-img- | sed 's/.*\(aosp_cf_arm64_only_phone-img-[0-9]*[.]zip\).*/\1/g')

wget -nv -c ${RURL%/view/BUILD_INFO}/raw/${FILENAME}
wget -nv -c ${RURL%/view/BUILD_INFO}/raw/cvd-host_package.tar.gz

exit 0
