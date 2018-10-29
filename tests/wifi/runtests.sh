#!/usr/bin/env bash

if [[ "$1" == "--help" ]]; then
  cat <<END
Usage for $0

	<no-args>			run all tests
	-r				print raw results
	-e class <class>[#<test>]	run test/s specified by class

Example:
$ $0 -r -e class com.android.cuttlefish.wifi.WifiE2eTests#testWifiConnects
Run just the specified test, and show the raw output.
END
  exit 0
fi

if [ -z $ANDROID_BUILD_TOP ]; then
  echo "You need to source and lunch before you can use this script"
  exit 1
fi

set -e # fail early
make -j32 -C $ANDROID_BUILD_TOP -f build/core/main.mk CuttlefishWifiTests
adb wait-for-device
# Same as 'package' in manifest file
adb uninstall com.android.cuttlefish.wifi.tests || true
adb install -r -g "$OUT/data/app/CuttlefishWifiTests/CuttlefishWifiTests.apk"
# optionally: -e class com.android.cuttlefish.wifi.WifiE2eTests#testName
adb shell am instrument -w "$@" 'com.android.cuttlefish.wifi.tests/android.support.test.runner.AndroidJUnitRunner'
