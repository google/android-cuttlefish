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

source "${ANDROID_BUILD_TOP}/external/shflags/src/shflags"

DEFINE_string kernel \
  "" "Path to kernel build dir" "k"

FLAGS_HELP="USAGE: $0 [flags]"

FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

if [ -z ${FLAGS_kernel} ]; then
	flags_help
	exit 1
fi

cd "${ANDROID_BUILD_TOP}/device/google/cuttlefish"
Sha=`git rev-parse HEAD`
cd - >/dev/null
cd "${ANDROID_BUILD_TOP}/external/u-boot"
Sha="$Sha,`git rev-parse HEAD`"
cd - >/dev/null
cd "${ANDROID_BUILD_TOP}/external/arm-trusted-firmware"
Sha="$Sha,`git rev-parse HEAD`"
cd - >/dev/null
cd "${FLAGS_kernel}"
Sha="$Sha,`git rev-parse HEAD`"
cd - >/dev/null
echo $Sha
