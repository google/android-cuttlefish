#!/bin/bash

# Download Cuttlefish

set -o errexit

URL=https://ci.android.com/builds/latest/branches/aosp-android-latest-release/targets/aosp_cf_arm64_only_phone-userdebug/view/BUILD_INFO
RURL=$(curl -Ls -o /dev/null -w %{url_effective} ${URL})
echo "RURL: $RURL"

BASE_URL=${RURL%/view/BUILD_INFO}
BUILD_ID=$(echo "$RURL" | cut -d'/' -f6)
echo "BUILD_ID: $BUILD_ID"

if [[ ! "$BUILD_ID" =~ ^[0-9]+$ ]]; then
  echo "Error: Failed to resolve latest build ID. RURL was $RURL" >&2
  exit 1
fi

FILENAME="aosp_cf_arm64_only_phone-img-${BUILD_ID}.zip"

download_artifact() {
  local filename=$1
  local view_url="${BASE_URL}/view/${filename}"
  
  echo "Fetching signed URL for ${filename} from ${view_url}..."
  local html
  html=$(curl -s "${view_url}")
  
  local signed_url
  signed_url=$(echo "$html" | grep -oP '"artifactUrl":"[^"]+"' | sed 's/"artifactUrl":"\(.*\)"/\1/' | sed 's/\\u0026/\&/g')
  
  if [ -z "$signed_url" ]; then
    echo "Error: Failed to get signed URL for ${filename}" >&2
    exit 1
  fi
  
  echo "Downloading ${filename}..."
  wget -nv -c "$signed_url" -O "${filename}"
}

download_artifact "${FILENAME}"
download_artifact "cvd-host_package.tar.gz"

exit 0
