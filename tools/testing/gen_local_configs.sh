
# Run this script to generate.json files in
# /tmp that match current AOSP build environment for testing.

# Usage : run this script within the same shell /
# environment as a successful AOSP build.(or init
# the environment via `lunch`, then execute this script)

if [[ -z "${ANDROID_PRODUCT_OUT}" ]] || [[ -z "${ANDROID_HOST_OUT}" ]]; then
  echo "ANDROID_PRODUCT_OUT and ANDROID_HOST_OUT must be defined for this script to work. Did you run lunch?"
  exit 1
fi

for fname in local_*.json
do
   sed -e "s=/tmp/vsoc_x86_64=$ANDROID_PRODUCT_OUT=;" -e "s=/tmp/linux-x86=$ANDROID_HOST_OUT=;" $fname > /tmp/$fname
done
