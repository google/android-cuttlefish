#!/usr/bin/env bash

# It will patch the version of of `zlib` from `1.3.1.bcr.3` to `1.3.1.bcr.4`.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit 1
REPO_DIR="$(realpath "$(dirname "$0")/../../..")"
CVD_DIR="${REPO_DIR}/base/cvd"

SEARCH='bazel_dep(name = "zlib", version = "1.3.1.bcr.3")'
REPLACE='bazel_dep(name = "zlib", version = "1.3.1.bcr.4")'
cat "${CVD_DIR}/MODULE.bazel" | sed -e "s/${SEARCH}/${REPLACE}/" > "${CVD_DIR}/MODULE.patched"
mv "${CVD_DIR}/MODULE.patched" "${CVD_DIR}/MODULE.bazel"
