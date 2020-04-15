#!/bin/bash

set -o errexit
set -x

ARCH=$1
if [[ -z ${ARCH} ]]; then
  echo "You must specify an architecture (e.g., x86)" 1>&2
  exit 1
fi

case "${ARCH}" in
  'x86' )
    THROTTLED=
    ;;
  'x86_64' )
    THROTTLED='-throttled'
    ;;
  'arm64' | 'aarch64' )
    ARCH='arm64'
    THROTTLED='-throttled'
    ;;
  * )
    echo "Uknown architecture ${ARCH}" 1>&2
    exit 1
    ;;
esac
URL=https://ci.android.com/builds/latest/branches/aosp-master${THROTTLED}/targets/aosp_cf_${ARCH}_phone-userdebug/view/BUILD_INFO

RURL=$(curl -Ls -o /dev/null -w %{url_effective} ${URL})
IMG=aosp_cf_${ARCH}_phone-img-$(echo $RURL | awk -F\/ '{print $6}').zip
wget -nv ${RURL%/view/BUILD_INFO}/raw/${IMG}
wget -nv ${RURL%/view/BUILD_INFO}/raw/cvd-host_package.tar.gz

unzip "${IMG}"
rm -v "${IMG}"
tar xzvf cvd-host_package.tar.gz
rm -v cvd-host_package.tar.gz
