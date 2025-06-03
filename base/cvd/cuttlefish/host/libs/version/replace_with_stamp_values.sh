#!/usr/bin/env bash
#
# updates sentinel tags in file with values from bazel stable stamp file
#

readonly stamp_filepath=$1   # where the key+value pairs are located
readonly in_filepath=$2      # template to update
readonly out_filepath=$3     # to write to

readonly COMMIT_TAG="@VCS_TAG@"
readonly COMMIT_KEY=STABLE_HEAD_COMMIT
readonly VERSION_TAG="@CF_VER_TAG@"
readonly VERSION_KEY=STABLE_CF_COMMON_VERSION

sed \
  -e "s|$COMMIT_TAG|$(grep --max-count=1 $COMMIT_KEY ${stamp_filepath} | cut --fields=2 --delimiter=' ')|" \
  -e "s|$VERSION_TAG|$(grep --max-count=1 $VERSION_KEY ${stamp_filepath} | cut --fields=2 --delimiter=' ')|" \
  ${in_filepath} > ${out_filepath}
