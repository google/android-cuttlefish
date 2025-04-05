#!/usr/bin/env bash

# It builds RPM packages locally.
REPO_DIR="$(realpath "$(dirname "$0")/../..")"
X86_64="${REPO_DIR}/tools/rpmbuild/RPMS/x86_64"
ARM_64="${REPO_DIR}/tools/rpmbuild/RPMS/arm64"

# Checking for prerequisites and installing them.
[ ! -f ~/usr/bin/go ] && sudo yum -y install go
[ ! -f ~/go/bin/bazelisk ] && go install github.com/bazelbuild/bazelisk@latest

function build_spec() {
  local specfile="${REPO_DIR}/tools/rpmbuild/SPECS/cuttlefish_$1.spec"
  sudo dnf builddep --skip-unavailable "$specfile"
  rpmbuild --define "_topdir $(pwd)/tools/rpmbuild" -v -ba "$specfile"
}

if [[ -f /bin/dnf ]]; then
  cd "${REPO_DIR}" || exit
  build_spec base
  build_spec user
  build_spec integration
  build_spec orchestration
  [ -d "${X86_64}" ] && ls "${X86_64}"
  [ -d "${ARM_64}" ] && ls "${ARM_64}"
  exit 0
else
  exit 1
fi
