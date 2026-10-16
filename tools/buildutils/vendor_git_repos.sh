#!/usr/bin/env bash

# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Pre-fetches every `git_repository` dependency of base/cvd into the Bazel
# vendor directory (`base/cvd/vendor_src`, see `--vendor_dir` in .bazelrc).
#
# `git_repository` has no retry logic and, when fetching a single commit fails
# for any reason, falls back to fetching *every* branch and tag of the remote.
# Transient 5xx responses from the git hosts therefore regularly break CI
# (b/574473449). Vendoring moves that fetch into one retried step whose output
# CI caches, so subsequent builds never contact the git hosts.
#
# Usage: tools/buildutils/vendor_git_repos.sh [extra bazel vendor flags...]
#   VENDOR_ATTEMPTS         number of attempts (default 3)
#   OVERRIDE_BAZEL_VERSION  honoured via base/cvd/tools/bazel as usual

set -o errexit -o nounset -o pipefail

readonly SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
readonly CVD_DIR="$(cd "${SCRIPT_DIR}/../../base/cvd" && pwd)"
readonly ATTEMPTS="${VENDOR_ATTEMPTS:-3}"

# Populate local git mirrors for crosvm and its submodules and configure git
# url.<mirror>.insteadOf so crate_universe's ~136 non-vendored crosvm crate
# repositories fetch from the cached local mirror (b/574521063).
"${SCRIPT_DIR}/setup_git_mirrors.sh"

cd "${CVD_DIR}"

# Every `name = "..."` that appears inside a `git_repository(...)` block.
shopt -s globstar nullglob
mapfile -t repos < <(
  sed -n '/^git_repository(/,/^)/{s/^ *name = "\([^"]*\)".*/\1/p}' \
    MODULE.bazel build_external/**/*.MODULE.bazel | sort -u
)
if [[ ${#repos[@]} -eq 0 ]]; then
  >&2 echo "No git_repository rules found under ${CVD_DIR}"
  exit 1
fi

repo_flags=()
for repo in "${repos[@]}"; do
  repo_flags+=("--repo=@${repo}")
done

echo "Vendoring ${#repos[@]} git repositories: ${repos[*]}"
# Each attempt passes the full list. Repos fetched by an earlier attempt already
# have a matching marker file in vendor_src, so Bazel skips them and only the
# failed ones hit the network again.
for ((attempt = 1; attempt <= ATTEMPTS; attempt++)); do
  if bazel vendor "${repo_flags[@]}" "$@"; then
    exit 0
  fi
  >&2 echo "bazel vendor failed (attempt ${attempt}/${ATTEMPTS})"
  sleep $((15 * attempt))
done
exit 1
