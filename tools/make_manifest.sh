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

DEFINE_string loader1 \
  "" "Path to loader1 image (partition 1)" "l"
DEFINE_string env \
  "" "Path to env image (partition 2)" "e"
DEFINE_string loader2 \
  "" "Path to loader2 image (partition 3)" "u"
DEFINE_string trust \
  "" "Path to trust image (partition 4)" "t"
DEFINE_string rootfs \
  "" "Path to rootfs image (partition 5)" "r"
DEFINE_string tftp \
  "192.168.0.1" "TFTP server address" "f"
DEFINE_string tftpdir \
  "/tftpboot" "TFTP server directory" "d"

FLAGS_HELP="USAGE: $0 [flags]"

FLAGS "$@" || exit $?
eval set -- "${FLAGS_ARGV}"

for arg in "$@" ; do
	flags_help
	exit 1
done

confirm() {
    read -r -p "${1:-Are you sure you want to continue? [y/N]} " response
    case "$response" in
        [yY][eE][sS]|[yY])
            true
            ;;
        *)
            false
            ;;
    esac
}

createManifest() {
if [ ! -e manifest.txt ]; then
	cat > manifest.txt << EOF
ManifestVersion=1
EOF
fi
}

addKVToManifest() {
	key=$1
	value=$2
	grep -q "^${key}=" manifest.txt && \
		sed -i "s/^${key}=.*/${key}=${value}/" manifest.txt || \
		echo "${key}=${value}" >> manifest.txt
}

addSHAToManifest() {
	key="SHA"
	cd "${ANDROID_BUILD_TOP}/device/google/cuttlefish_common"
	SHA=`git rev-parse HEAD`
	cd -
	cd "${ANDROID_BUILD_TOP}/external/u-boot"
	SHA="$SHA,`git rev-parse HEAD`"
	cd -
	cd "${ANDROID_BUILD_TOP}/external/arm-trusted-firmware"
	SHA="$SHA,`git rev-parse HEAD`"
	cd -

	addKVToManifest "${key}" "${SHA}"
}

addPathToManifest() {
	key=$1
	path=$2

	if [ "${path}" != "" ]; then
		filename=$(basename $path)
		filetype=`file -b --mime-type "${path}"`
		if [ "$key" == "UbootEnv" ] && [ "${filetype}" == "application/gzip" ]; then
			echo "error: gzip not supported for env images"
		fi
		if [ "$key" != "UbootEnv" ] && [ "${filetype}" != "application/gzip" ]; then
			echo "warning: gzip recommended for all non-env images"
			confirm || exit 1
		fi
		if [ ! "${path}" -ef "${FLAGS_tftpdir}/${filename}" ]; then
			cp "${path}" "${FLAGS_tftpdir}/"
		fi
	else
		unset filename
	fi

	addKVToManifest "${key}" "${filename}"
}

createManifest
addKVToManifest TftpServer ${FLAGS_tftp}
addPathToManifest RootfsImg ${FLAGS_rootfs}
addPathToManifest UbootEnv ${FLAGS_env}
addPathToManifest TplSplImg ${FLAGS_loader1}
addPathToManifest UbootItb ${FLAGS_loader2}
addSHAToManifest
