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

# Populates local shallow git mirrors for crosvm and its git submodules in
# `base/cvd/vendor_src/.git-mirrors` (cached alongside `base/cvd/vendor_src` by
# `.github/actions/mount-bazel-vendor-cache`) and configures
# `url.<mirror>.insteadOf` in global git config.
#
# While top-level `git_repository` dependencies are vendored via
# `vendor_git_repos.sh`, rules_rust's crate_universe dynamically generates ~136
# separate `new_git_repository` rules (each with `init_submodules = True`)
# pointing to crosvm (`@@rules_rust++crate+crosvm_bin__*` and
# `@@rules_rust++crate+netsim_crates__*`). Without a local mirror, a single
# Bazel build issues >500 concurrent git fetches against
# chromium.googlesource.com and triggers HTTP 429 rate limits (b/574521063).

set -o errexit -o nounset -o pipefail

readonly REPO_DIR="$(realpath "$(dirname "$0")/../..")"
readonly MIRROR_DIR_ARG="${1:-${REPO_DIR}/base/cvd/vendor_src/.git-mirrors}"

mkdir -p "${MIRROR_DIR_ARG}"
readonly MIRROR_DIR="$(realpath "${MIRROR_DIR_ARG}")"

# Allowlists a mirror directory in global git config so containerized or
# multi-user builds do not fail with "detected dubious ownership".
function add_safe_directory() {
  local repo_dir="$1"
  local dir
  for dir in "${repo_dir}" "${repo_dir}/.git"; do
    if ! git config --global --get-all safe.directory 2>/dev/null | grep -Fxq "${dir}"; then
      git config --global --add safe.directory "${dir}"
    fi
  done
}

# Initializes a local git repository and enables fetching arbitrary commit SHAs
# from it (required because Bazel fetches pinned commits by raw SHA).
function init_local_mirror_repo() {
  local repo_dir="$1"
  if [ ! -d "${repo_dir}/.git" ]; then
    # Use the traditional "files" ref format so older git clients inside Docker
    # containers can read repositories initialized by newer host git versions.
    git -c init.defaultRefFormat=files init --quiet "${repo_dir}"
  fi
  if [ "$(git -C "${repo_dir}" config --get uploadpack.allowAnySHA1InWant || true)" != "true" ]; then
    git -C "${repo_dir}" config uploadpack.allowReachableSHA1InWant true
    git -C "${repo_dir}" config uploadpack.allowAnySHA1InWant true
  fi
}

# Removes any existing url.<local>.insteadOf mapping for remote_url so that a
# subsequent `git fetch` talks to the upstream server instead of looping back
# to the local mirror.
function clear_url_insteadof() {
  local remote_url="$1"
  local key
  while read -r key; do
    [ -n "${key}" ] && git config --global --unset-all "${key}" || true
  done < <(git config --global --name-only --get-regexp '^url\..*\.insteadOf$' "^${remote_url}$" 2>/dev/null || true)
}

# Shallow-fetches a single commit into repo_dir if it is not already cached,
# retrying with exponential backoff on transient HTTP 429/5xx errors.
function fetch_commit_with_retry() {
  local repo_dir="$1"
  local remote_url="$2"
  local rev="$3"

  if git -C "${repo_dir}" cat-file -e "${rev}^{commit}" 2>/dev/null; then
    echo "Using cached mirror for ${remote_url} @ ${rev} (${repo_dir})"
    return 0
  fi

  echo "Mirroring ${remote_url} @ ${rev} into ${repo_dir} ..."
  local attempt=1
  local max_attempts=5
  local delay=5
  while ! git -C "${repo_dir}" fetch --depth=1 "${remote_url}" "${rev}:refs/tags/pinned-${rev}"; do
    if [ "${attempt}" -ge "${max_attempts}" ]; then
      echo "Failed to fetch ${remote_url} @ ${rev} after ${max_attempts} attempts" >&2
      return 1
    fi
    echo "Retrying fetch of ${remote_url} in ${delay}s (attempt ${attempt}/${max_attempts})..." >&2
    sleep "${delay}"
    attempt=$((attempt + 1))
    delay=$((delay * 2))
  done
}

# Ensures a local mirror contains `rev` from `remote_url` and redirects all
# future git fetches for `remote_url` to the local mirror directory.
function mirror_repo() {
  local name="$1"
  local remote_url="$2"
  local rev="$3"
  local repo_dir="${MIRROR_DIR}/${name}"

  add_safe_directory "${repo_dir}"
  init_local_mirror_repo "${repo_dir}"
  clear_url_insteadof "${remote_url}"
  fetch_commit_with_retry "${repo_dir}" "${remote_url}" "${rev}"
  git config --global --add "url.file://${repo_dir}.insteadOf" "${remote_url}"
}

# Extracts a string assignment of the form `KEY = "VALUE"` from a Bazel file.
function extract_bazel_string_attr() {
  local file="$1"
  local key="$2"
  sed -n "s/^ *${key} *= *\"\\([^\"]*\\)\".*/\\1/p" "${file}"
}

# Mirrors the main crosvm repository and each of its git submodules at the
# exact commits pinned in crosvm.MODULE.bazel.
function mirror_crosvm_and_submodules() {
  local crosvm_module="${REPO_DIR}/base/cvd/build_external/crosvm/crosvm.MODULE.bazel"
  local crosvm_remote
  local crosvm_rev
  crosvm_remote="$(extract_bazel_string_attr "${crosvm_module}" "CROSVM_REMOTE")"
  crosvm_rev="$(extract_bazel_string_attr "${crosvm_module}" "CROSVM_REV")"

  mirror_repo "crosvm" "${crosvm_remote}" "${crosvm_rev}"

  local gitmodules_temp
  gitmodules_temp="$(mktemp)"
  trap 'rm -f "${gitmodules_temp:-}"' RETURN
  git -C "${MIRROR_DIR}/crosvm" show "${crosvm_rev}:.gitmodules" > "${gitmodules_temp}" 2>/dev/null || true

  # In git tree objects, mode 160000 denotes a submodule gitlink entry whose
  # object hash is the pinned submodule commit SHA.
  local _mode _type submod_rev submod_path submod_url submod_name
  while read -r _mode _type submod_rev submod_path; do
    [ -z "${submod_path}" ] && continue
    submod_url=""
    if [ -s "${gitmodules_temp}" ]; then
      submod_url="$(git config --file "${gitmodules_temp}" --get "submodule.${submod_path}.url" || true)"
    fi
    [ -z "${submod_url}" ] && continue
    submod_name="crosvm_$(echo "${submod_path}" | tr '/' '_')"
    mirror_repo "${submod_name}" "${submod_url}" "${submod_rev}"
  done < <(git -C "${MIRROR_DIR}/crosvm" ls-tree -r "${crosvm_rev}" | awk '$1 == "160000" {print $1, $2, $3, $4}')
}

function main() {
  # Allow git submodule update to clone from local file:// mirrors (disabled by
  # default in Git 2.38+).
  git config --global protocol.file.allow always

  mirror_crosvm_and_submodules
}

main "$@"
