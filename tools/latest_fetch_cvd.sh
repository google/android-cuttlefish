#!/bin/bash

LATEST_BUILD_ID=`curl "https://www.googleapis.com/android/internal/build/v3/builds?branch=aosp-master&buildAttemptStatus=complete&buildType=submitted&maxResults=1&successful=true&target=aosp_cf_x86_64_phone-userdebug" 2>/dev/null | \
  python3 -c "import sys, json; print(json.load(sys.stdin)['builds'][0]['buildId'])"`
LATEST_BUILD_URL=`curl "https://www.googleapis.com/android/internal/build/v3/builds/$LATEST_BUILD_ID/aosp_cf_x86_64_phone-userdebug/attempts/latest/artifacts/fetch_cvd/url" 2>/dev/null | \
  python3 -c "import sys, json; print(json.load(sys.stdin)['signedUrl'])"`

DOWNLOAD_TARGET=`mktemp`

curl "${LATEST_BUILD_URL}" -o $DOWNLOAD_TARGET

chmod +x $DOWNLOAD_TARGET

exec $DOWNLOAD_TARGET $@
