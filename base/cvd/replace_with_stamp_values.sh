#!/usr/bin/env bash
#
# updates sentinel tags in file with values from bazel stable stamp file
#
# $1 stamp filepath - where the key+value pairs are located
# $2 in filepath - template to update
# $3 out filepath - to write to

readonly COMMIT_TAG="@VCS_TAG@"
readonly COMMIT_KEY=STABLE_HEAD_COMMIT
readonly VERSION_TAG="@CF_VER_TAG@"
readonly VERSION_KEY=STABLE_CF_COMMON_VERSION

sed \
  -e "s|$COMMIT_TAG|$(grep --max-count=1 $COMMIT_KEY $1 | cut --fields=2 --delimiter=' ')|" \
  -e "s|$VERSION_TAG|$(grep --max-count=1 $VERSION_KEY $1 | cut --fields=2 --delimiter=' ')|" \
  $2 > $3
