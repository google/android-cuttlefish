#!/usr/bin/env bash
#
# https://bazel.build/docs/user-manual#workspace-status
#
# append `--workspace_status_command=path/to/stamp_helper.sh` to build command
# to "stamp" this output to `bazel-out/stable_status.txt``
# 
# NOTES: 
# - output must be a "key value" format on a single line
# - output key must begin with "STABLE_" or the key+value pair end up in
#   `volatile_status.txt`
# - changes to `stable_status.txt` values triggers a re-run of the affected
#   actions

echo "STABLE_HEAD_COMMIT $(git rev-parse HEAD)"
echo "STABLE_CF_COMMON_VERSION $(dpkg-parsechangelog --file ../debian/changelog --show-field Version)"
