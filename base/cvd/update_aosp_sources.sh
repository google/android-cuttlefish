#!/bin/bash

set -euo pipefail

usage() {
  echo "usage: $0 </path/to/android/source/tree>" >&2
  exit 1
}

if [[ $# -ne 1 || -z "$1" ]]; then
  usage
fi

android_root="$1"
if [[ ! -d "${android_root}" ]]; then
  echo "ERROR: ${android_root} is not a directory!" >&2
  exit 1
fi

# Guard against accidentally running against an internal Android tree.
if [[ -d "${android_root}/vendor/google_devices/cuttlefish_common" ]]; then
  echo "ERROR: ${android_root} appears to be an internal Android tree (found vendor/google_devices/cuttlefish_common)." >&2
  echo "This script must only be run against an AOSP / publicly released Android tree." >&2
  exit 1
fi

check_not_internal_repo() {
  local dir="$1"
  if command -v git >/dev/null 2>&1 && git -C "${dir}" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    if git -C "${dir}" remote -v 2>/dev/null | grep -qi "googleplex-android"; then
      echo "ERROR: ${dir} has a 'googleplex-android' git remote, which indicates an internal Android tree." >&2
      echo "This script must only be run against an AOSP / publicly released Android tree." >&2
      exit 1
    fi
  fi
}

script_dir="$(dirname "$(readlink -f "$0")")"
cd "${script_dir}"

declare -A SOURCE_DIRS
register_source_dir() {
  local key="$1"
  local path="$2"
  if [[ ! -d "${path}" ]]; then
    echo "ERROR: ${path} does not exist!" >&2
    exit 1
  fi
  check_not_internal_repo "${path}"
  SOURCE_DIRS["${key}"]="${path}"
}

register_source_dir adb "${android_root}/packages/modules/adb"
register_source_dir diagnose_usb "${android_root}/system/core/diagnose_usb"
register_source_dir fastboot "${android_root}/system/core/fastboot"
register_source_dir libstorage_literals "${android_root}/system/fs/fs_mgr/libstorage_literals"
register_source_dir libcrypto_utils "${android_root}/system/core/libcrypto_utils"
register_source_dir mkbootfs "${android_root}/system/core/mkbootfs"
register_source_dir ext4_utils "${android_root}/system/extras/ext4_utils"
register_source_dir libfec "${android_root}/system/extras/libfec"
register_source_dir verity "${android_root}/system/extras/verity"

DEST_DIRS=("adb" "fastboot" "fec" "libext4_utils" "mkbootfs")
for dir in "${DEST_DIRS[@]}"; do
  if [[ ! -d "${dir}" ]]; then
    echo "ERROR: ${dir} does not exist!" >&2
    exit 1
  fi
done

echo "WARNING: This script MUST be run against an AOSP / publicly released Android tree."
echo "WARNING: Do not run it against an internal Android tree under any circumstance!"
echo
while true; do
  read -r -p "Do you understand the above instructions? " yn
  case "${yn}" in
    [Yy]* ) break;;
    [Nn]* ) exit 1;;
    * ) echo "Please answer yes or no.";;
  esac
done
echo

work_dir="$(mktemp -d)"
cleanup() {
  rm -rf "${work_dir}"
}
trap cleanup EXIT

preserved_files=()

preserve_common() {
  local dest="$1"
  local staging="$2"
  if [[ -f "${dest}/BUILD.bazel" ]]; then
    cp -a "${dest}/BUILD.bazel" "${staging}/BUILD.bazel"
  fi
  if [[ -d "${dest}/patches" ]]; then
    cp -a "${dest}/patches" "${staging}/patches"
  fi
}

preserve_file() {
  local dest="$1"
  local staging="$2"
  local rel_path="$3"
  local src_path="${dest}/${rel_path}"
  if [[ ! -e "${src_path}" ]]; then
    echo "ERROR: Expected preserved file ${src_path} does not exist!" >&2
    exit 1
  fi
  mkdir -p "${staging}/$(dirname "${rel_path}")"
  cp -a "${src_path}" "${staging}/${rel_path}"
  preserved_files+=("${src_path}")
}

update_adb() {
  local staging="$1"

  mkdir -p "${staging}/diagnose_usb" "${staging}/libcrypto_utils"
  cp -a "${SOURCE_DIRS[adb]}/." "${staging}/"
  cp -a "${SOURCE_DIRS[diagnose_usb]}/." "${staging}/diagnose_usb/"
  cp -a "${SOURCE_DIRS[libcrypto_utils]}/." "${staging}/libcrypto_utils/"

  (
    cd "${staging}"

    # Remove Android build system and metadata
    find . \( -name Android.bp -o -name OWNERS \) -delete
    rm -f adbd_flags.aconfig METADATA MODULE_LICENSE_* NOTICE PREUPLOAD.cfg rustfmt.toml TEST_MAPPING
    rm -rf apex

    # Remove coverage instrumentation
    rm -rf coverage

    # Remove documentation
    rm -rf docs/ README.md sockets.dia

    # Remove tools
    rm -rf adb.bash tools/

    # Remove symbol map text files, as the modules using them are not built
    find . -name '*.map.txt' -delete

    # Remove osx and windows support
    find . \( -name '*_osx*' -o -name 'fdevent_poll*' -o -name '*_windows*' -o -name '*win32*' \) -exec rm -rf {} +

    # Remove all tests, as they are not built and have additional dependencies
    find . \( -name '*_test.*' -o -name tests -o -name 'test_*' \) -exec rm -rf {} +
    rm -f adb_integration_test_* run-device-tests.sh

    # Remove all benchmarks, as they are not built and have additional dependencies
    find . \( -name '*_benchmark.*' -o -name 'benchmark_*' -o -name trace.sh \) -exec rm -rf {} +

    # Remove fastdeploy
    rm -rf client/fastdeploy.* fastdeploy/ proto/jarjar-rules.txt

    # Remove adbmdns and openscreen mDNS backends
    rm -rf client/adbmdns/ client/openscreen/

    # Remove adb device-side daemon sources
    rm -rf daemon/ libs/ security_log_tags.h transfer_id.h
    if [[ -d pairing_connection ]]; then
      find pairing_connection \( -name 'pairing_server*' -o -name internal \) -exec rm -rf {} +
    fi
  )

  preserve_common "adb" "${staging}"
  # version.h and platform_tools_version.h are generated files in AOSP
  preserve_file "adb" "${staging}" "version/build/version.h"
  preserve_file "adb" "${staging}" "version/platform_tools_version.h"
}

update_fastboot() {
  local staging="$1"

  mkdir -p "${staging}/diagnose_usb" "${staging}/libstorage_literals"
  cp -a "${SOURCE_DIRS[fastboot]}/." "${staging}/"
  cp -a "${SOURCE_DIRS[diagnose_usb]}/." "${staging}/diagnose_usb/"
  cp -a "${SOURCE_DIRS[libstorage_literals]}/." "${staging}/libstorage_literals/"

  (
    cd "${staging}"

    # Remove fuzzers
    rm -rf fuzzer fuzzy_fastboot

    # Remove osx and windows support
    find . \( -name '*_osx*' -o -name '*_windows*' \) -exec rm -rf {} +

    # Remove Android build system and metadata
    find . \( -name Android.bp -o -name OWNERS \) -delete
    rm -f LICENSE README.md TEST_MAPPING

    # Remove device-side implementation
    rm -rf device

    # Remove tools
    rm -rf fastboot.bash

    # Remove all tests, as they are not built and have additional dependencies
    find . \( \
      -name '*_test.*' -o -name 'test_*' -o -name tests -o \
      -name '*_mock.*' -o -name 'mock_*' \
      \) -exec rm -rf {} +
    rm -rf testdata
  )

  preserve_common "fastboot" "${staging}"
  # version.h and platform_tools_version.h are generated files in AOSP
  preserve_file "fastboot" "${staging}" "version/build/version.h"
  preserve_file "fastboot" "${staging}" "version/platform_tools_version.h"
}

update_fec() {
  local staging="$1"

  mkdir -p "${staging}/libcrypto_utils/include" "${staging}/libfec" "${staging}/verity/fec"
  cp -a "${SOURCE_DIRS[libcrypto_utils]}/include/." "${staging}/libcrypto_utils/include/"
  cp -a "${SOURCE_DIRS[libfec]}/." "${staging}/libfec/"
  cp -a "${SOURCE_DIRS[verity]}/fec/." "${staging}/verity/fec/"

  (
    cd "${staging}"

    # Remove Android build system and metadata
    find . \( -name Android.bp -o -name OWNERS -o -name NOTICE \) -delete

    # Remove all tests, as they are not built and have additional dependencies
    find . \( -name test -o -name tests \) -exec rm -rf {} +
  )

  preserve_common "fec" "${staging}"
  # Compat.h is stubbed as it is not used
  preserve_file "fec" "${staging}" "libutils/include/utils/Compat.h"
  # squashfs_utils.h is stubbed to avoid a libsquashfs_utils dependency
  preserve_file "fec" "${staging}" "squashfs_utils/squashfs_utils.h"
}

update_libext4_utils() {
  local staging="$1"

  cp -a "${SOURCE_DIRS[ext4_utils]}/." "${staging}/"

  (
    cd "${staging}"
    chmod a+x mkuserimg_mke2fs.py

    # Remove Android build system and metadata
    rm -f Android.bp MODULE_LICENSE_* NOTICE OWNERS

    # Remove unused blk_alloc_to_base_fs and "wipe" APIs
    rm -f blk_alloc_to_base_fs.cpp include/ext4_utils/wipe.h wipe.cpp

    # Remove all tests, as they are not built and have additional dependencies
    find . -name 'test_*' -delete
  )

  preserve_common "libext4_utils" "${staging}"
}

update_mkbootfs() {
  local staging="$1"

  cp -a "${SOURCE_DIRS[mkbootfs]}/." "${staging}/"

  (
    cd "${staging}"

    # Remove Android build system
    rm -f Android.bp
  )

  preserve_common "mkbootfs" "${staging}"
}

# Stage, prune, replace, and patch the upstream sources
for dir in "${DEST_DIRS[@]}"; do
  staging="${work_dir}/${dir}"
  mkdir -p "${staging}"
  "update_${dir}" "${staging}"
  rm -rf "${dir}"
  mv "${staging}" "${dir}"

  if [[ -d "${dir}/patches" ]]; then
    (
      cd "${dir}"
      shopt -s nullglob
      slash_count="${dir//[^\/]/}"
      level=$((4 + ${#slash_count}))
      for patch_file in patches/*.patch; do
        echo "Applying patch ${dir}/${patch_file}..."
        if ! patch --no-backup-if-mismatch -Np"${level}" -i "${patch_file}"; then
          echo "WARNING: Patch ${dir}/${patch_file} did not apply cleanly." >&2
        fi
        echo
      done
    )
  fi
done

if [[ ${#preserved_files[@]} -gt 0 ]]; then
  echo "WARNING: If necessary, please update:"
  echo
  printf '%s\n' "${preserved_files[@]}"
  echo
fi

echo "Done!"
