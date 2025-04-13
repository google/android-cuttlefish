#!/usr/bin/env bash

# It will change the version of of `zlib` from `1.3.1.bcr.3` to `1.3.1.bcr.4`.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit 1
REPO_DIR="${HOME}/${REPO_NAME}"
CVD_DIR="${REPO_DIR}/base/cvd"

SEARCH='bazel_dep(name = "zlib", version = "1.3.1.bcr.3")'
REPLACE='bazel_dep(name = "zlib", version = "1.3.1.bcr.4")'
cat "${CVD_DIR}/MODULE.bazel" | sed -e "s/${SEARCH}/${REPLACE}/" > "${CVD_DIR}/MODULE.patched"
mv "${CVD_DIR}/MODULE.patched" "${CVD_DIR}/MODULE.bazel"
echo "RHEL patch: base/cvd/MODULE.bazel changed from zlib@1.3.1.bcr.3 to zlib@1.3.1.bcr.4"
rm "${CVD_DIR}/MODULE.bazel.lock"
echo "RHEL patch: base/cvd/MODULE.bazel.lock also removed"
