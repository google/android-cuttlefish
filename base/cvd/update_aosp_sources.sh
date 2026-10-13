#!/bin/bash

set -e

usage() {
  echo "usage: $0 [/path/to/android/source/tree]"
  exit
}
[ -z "$1" ] && usage

# $1 = array name to update with checked path
# $2 = suffix applied to variable name set to checked path
# $3 = check this path is an existing directory
check_dir() {
  local -n array="${1}"
  if [ ! -d "${3}" ]; then
    echo "${3} does not exist!"
    exit
  fi
  eval ${3##*/}${2}="${3}"
  array+=("${3}")
}

script_dir="$(dirname $(readlink -f "$0"))"
cd ${script_dir}

source_dirs=()
add_source_dir() {
  check_dir source_dirs "_source" "${1}"
}
add_source_dir "$1/packages/modules/adb"
add_source_dir "$1/system/core/diagnose_usb"
add_source_dir "$1/system/core/fastboot"
add_source_dir "$1/system/core/fs_mgr/libstorage_literals"
add_source_dir "$1/system/core/libcrypto_utils"
add_source_dir "$1/system/core/mkbootfs"
add_source_dir "$1/system/extras/ext4_utils"
add_source_dir "$1/system/extras/libfec"
add_source_dir "$1/system/extras/verity"
add_source_dir "$1/system/teeui/libteeui"
# Can now access with e.g. $adb_source or $source_dirs[x]

dest_dirs=()
add_dest_dir() {
  check_dir dest_dirs "" "${1}"
}
add_dest_dir "adb"
add_dest_dir "fastboot"
add_dest_dir "fec"
add_dest_dir "libext4_utils"
add_dest_dir "mkbootfs"
add_dest_dir "teeui/libteeui"
# Can now access with e.g. $adb or $dest_dirs[x]

echo "WARNING: This script MUST be run against an AOSP / publicly released Android tree."
echo "WARNING: Do not run it against an internal Android tree under any circumstance!"
echo
while true; do 
  read -p "Do you understand the above instructions? " yn
  case $yn in
    [Yy]* ) break;;
    [Nn]* ) exit;;
    * ) echo "Please answer yes or no.";;
  esac
done
echo

# $1 = prefix for excluded paths
# $2 = array of paths to exclude from cleaning
excluded_files=()
clean_dest_excluded() {
  local -n array="${2}"
  find -type f \( \
    -not -name BUILD.bazel \
    -and -not -name "*.patch" \
    $(for file in ${array[@]}; do echo -and -not -path ./${file}; done) \
    \) -exec rm -f '{}' ';'
  excluded_files+=("${array[@]/#/$1/}")
}
clean_dest() {
  local empty=()
  clean_dest_excluded "" empty
}

adb() {
  # version.h and platform_tools_version.h are generated files in AOSP
  local adb_excluded_files=("version/build/version.h"
                            "version/platform_tools_version.h")
  clean_dest_excluded adb adb_excluded_files

  cp -a "${adb_source}"/* .
  cp -a "${diagnose_usb_source}"/* diagnose_usb/
  cp -a "${libcrypto_utils_source}"/* libcrypto_utils/

  # Remove Android build system and metadata
  find \( -name Android.bp -or -name OWNERS \) -exec rm -f '{}' ';'
  rm -f adbd_flags.aconfig METADATA MODULE_LICENSE_* NOTICE PREUPLOAD.cfg TEST_MAPPING
  rm -rf apex

  # Remove coverage instrumentation
  rm -rf coverage

  # Remove documentation
  rm -rf docs/ README.md sockets.dia

  # Remove tools
  rm -rf adb.bash tools/

  # Remove symbol map text files, as the modules using them are not built
  find -name '*.map.txt' -exec rm -f '{}' ';'

  # Remove osx and windows support
  find \( -name '*_osx*' -or -name 'fdevent_poll*' -or -name '*_windows*' -or -name '*win32*' \) -exec rm -rf '{}' ';' 2>/dev/null || true

  # Remove all tests, as they are not built and have additional dependencies
  find \( -name '*_test.*' -or -name tests -or -name 'test_*' \) -exec rm -rf '{}' ';' 2>/dev/null || true
  rm -f adb_integration_test_* run-device-tests.sh

  # Remove all benchmarks, as they are not built and have additional dependencies
  find \( -name '*_benchmark.*' -or -name 'benchmark_*' -or -name trace.sh \) -exec rm -rf '{}' ';'

  # Remove fastdeploy
  rm -rf client/fastdeploy.* fastdeploy/ proto/jarjar-rules.txt

  # Remove adb device-side daemon sources
  rm -rf daemon/ libs/ security_log_tags.h transfer_id.h
  find pairing_connection \( -name 'pairing_server*' -or -name internal \) -exec rm -rf '{}' ';' 2>/dev/null || true
}

fastboot() {
  # version.h and platform_tools_version.h are generated files in AOSP
  local fastboot_excluded_files=("version/build/version.h"
                                 "version/platform_tools_version.h")
  clean_dest_excluded fastboot fastboot_excluded_files

  cp -a "${fastboot_source}"/* .
  cp -a "${diagnose_usb_source}"/* diagnose_usb/
  cp -a "${libstorage_literals_source}"/* libstorage_literals/

  # Remove fuzzers
  rm -rf fuzzer fuzzy_fastboot

  # Remove osx and windows support
  find \( -name '*_osx*' -or -name '*_windows*' \) -exec rm -rf '{}' ';' 2>/dev/null || true

  # Remove Android build system and metadata
  find \( -name Android.bp -or -name OWNERS \) -exec rm -f '{}' ';'
  rm -f LICENSE README.md TEST_MAPPING

  # Remove device-side implementation
  rm -rf device

  # Remove tools
  rm -rf fastboot.bash

  # Remove all tests, as they are not built and have additional dependencies
  find \( \
    -name '*_test.*' -or -name 'test_*' -or -name tests -or \
    -name '*_mock.*' -or -name 'mock_*' \
    \) -exec rm -rf '{}' ';' 2>/dev/null || true
  rm -rf testdata
}

fec() {
  # Compat.h is stubbed as it is not used
  # squashfs_utils.h is stubbed to avoid a libsquashfs_utils dependency
  local fec_excluded_files=("libutils/include/utils/Compat.h"
                            "squashfs_utils/squashfs_utils.h")
  clean_dest_excluded fec fec_excluded_files

  cp -a "${libcrypto_utils_source}"/include/* libcrypto_utils/include/
  cp -a "${libfec_source}"/* libfec/
  cp -a "${verity_source}"/fec/* verity/fec/

  # Remove Android build system and metadata
  find \( -name Android.bp -or -name OWNERS -or -name NOTICE \) -exec rm -f '{}' ';'

  # Remove all tests, as they are not built and have additional dependencies
  find \( -name test -or -name tests \) -exec rm -rf '{}' ';' 2>/dev/null || true
}

libext4_utils() {
  clean_dest

  cp -a "${ext4_utils_source}"/* .
  chmod a+x mkuserimg_mke2fs.py

  # Remove Android build system and metadata
  rm -f Android.bp MODULE_LICENSE_* NOTICE OWNERS

  # Remove unused blk_allow_to_base_fs and "wipe" APIs
  rm -f blk_alloc_to_base_fs.cpp include/ext4_utils/wipe.h wipe.cpp

  # Remove all tests, as they are not built and have additional dependencies
  find -name 'test_*' -exec rm -f '{}' ';' 2>/dev/null
}

libteeui() {
  clean_dest

  cp -a "${libteeui_source}"/* .

  # Remove Android build system
  find -name Android.bp -exec rm -f '{}' ';'

  # Remove generic operation/messages support
  rm -f \
    include/teeui/cbor.h \
    include/teeui/generic_messages.h \
    include/teeui/generic_operation.h \
    src/cbor.cpp \
    src/generic_messages.cpp \
    src/msg_formatting.cpp

  # Remove input device support
  rm -rf include/secure_input/ src/evdev.cpp src/weak_secure_input_device.cpp

  # Remove example code
  rm -rf example/ include/teeui/example/ include/teeui/incfont.h
}

mkbootfs() {
  clean_dest

  cp -a "${mkbootfs_source}"/* .

  # Remove Android build system
  rm -f Android.bp
}

# Copy, prune and optionally patch the upstream sources
for dir in ${dest_dirs[@]}; do
  pushd "${dir}" >/dev/null
    ${dir##*/}
    if [ -d patches ]; then
      for patch in patches/*; do
        echo Applying patch "$dir/$patch"...
	level=$((4 + $(echo $dir | tr -cd / | wc -c)))
        patch --no-backup-if-mismatch -Np${level} -i $patch || true
        echo
      done
    fi
  popd >/dev/null
done

echo "WARNING: If necessary, please update:"
echo
for file in ${excluded_files[@]}; do echo "$file"; done
echo
echo "Done!"
