
# This utility generates .json files in
# /tmp that match current AOSP build environment for testing.
# These configs are used for cvd multi-vd testing.

# Usage:
# - First ensure you are within the same shell /
# environment as a successful AOSP build.(or init
# the environment via `lunch` in new shell)
# - Then navigate to the folder:
# cd /device/google/cuttlefish/host/cvd_test_configs/local_tests
# - Run this script
# ../../../tools/testing/gen_local_configs.sh

if [[ -z "${ANDROID_PRODUCT_OUT}" ]] || [[ -z "${ANDROID_HOST_OUT}" ]]; then
  echo "ANDROID_PRODUCT_OUT and ANDROID_HOST_OUT must be defined for this script to work. Did you run lunch?"
  exit 1
fi

for fname in local_*.json
do
   sed -e "s=/tmp/vsoc_x86_64=$ANDROID_PRODUCT_OUT=;" -e "s=/tmp/linux-x86=$ANDROID_HOST_OUT=;" $fname > /tmp/$fname
done
