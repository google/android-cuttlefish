#!/usr/bin/env bash

# Copyright 2018 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

if [[ "$1" == "--help" ]]; then
  cat <<END
Usage for $0

	<no-args>			run all tests
	-r				print raw results
	-e class <class>[#<test>]	run test/s specified by class

Example:
$ $0 -r -e class com.android.cuttlefish.ril.RilE2eTests#testRilConnects
Run just the specified test, and show the raw output.
END
  exit 0
fi

if [ -z $ANDROID_BUILD_TOP ]; then
  echo "You need to source and lunch before you can use this script"
  exit 1
fi

set -e # fail early
make -j32 -C $ANDROID_BUILD_TOP -f build/core/main.mk MODULES-IN-device-google-cuttlefish-tests-ril
adb wait-for-device
# Same as 'package' in manifest file
adb uninstall com.android.cuttlefish.ril.tests || true
adb install -r -g "$OUT/data/app/CuttlefishRilTests/CuttlefishRilTests.apk"
# optionally: -e class com.android.cuttlefish.ril.RilE2eTests#testName
adb shell am instrument -w "$@" 'com.android.cuttlefish.ril.tests/androidx.test.runner.AndroidJUnitRunner'
