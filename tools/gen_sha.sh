#!/bin/bash

# Copyright 2019 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -e
set -u

source "${ANDROID_BUILD_TOP}/external/shflags/shflags"

DEFINE_string kernel \
  "" "Path to kernel repo checkout" "k"
DEFINE_string uboot \
  "" "Path to u-boot repo checkout" "u"

FLAGS_HELP="USAGE: $0 [flags]"

FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

if [ -z ${FLAGS_kernel} -o -z ${FLAGS_uboot} ]; then
	flags_help
	exit 1
fi

cd "${ANDROID_BUILD_TOP}/device/google/cuttlefish"
Sha=`git rev-parse HEAD`
cd - >/dev/null
# cd "${FLAGS_uboot}/u-boot"
cd "${ANDROID_BUILD_TOP}/device/google/cuttlefish_prebuilts"
Sha="$Sha,`git rev-parse HEAD`"
cd - >/dev/null
if [ -d "${FLAGS_uboot}/external/arm-trusted-firmware/.git" ]; then
    cd "${FLAGS_uboot}/external/arm-trusted-firmware"
    Sha="$Sha,`git rev-parse HEAD`"
    cd - >/dev/null
else
    Sha="$Sha,"'!TFA'
fi
if [ -d "${FLAGS_kernel}/common/.git" ]; then
    cd "${FLAGS_kernel}/common"
    Sha="$Sha,`git rev-parse HEAD`"
    cd - >/dev/null
else
    Sha="$Sha,"'!kernelcommon'
fi
echo $Sha
