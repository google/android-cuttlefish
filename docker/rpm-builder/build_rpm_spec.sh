#!/usr/bin/env bash

# It builds RPM packages locally.
REPO_DIR="$(realpath "$(dirname "$0")/../..")"
X86_64="${REPO_DIR}/tools/rpmbuild/RPMS/x86_64"
ARM_64="${REPO_DIR}/tools/rpmbuild/RPMS/arm64"

function install_bazel() {
  URL=https://github.com/bazelbuild/bazelisk/releases/download/v1.25.0/bazelisk-linux
  [ "$(uname -m)" == "x86_64" ] && URL="$URL-amd64"
  [ "$(uname -m)" == "aarch64" ] && URL="$URL-arm64"
  sudo wget --no-verbose --output-document /usr/local/bin/bazel $URL
  sudo chmod +x /usr/local/bin/bazel
}

# http://ftp.rpm.org/max-rpm/ch-rpm-b-command.html#S2-RPM-B-COMMAND-SIGN-OPTION
function build_spec() {
  local specfile="${REPO_DIR}/tools/rpmbuild/SPECS/cuttlefish_$1.spec"
  [ ! "$(whoami)" == "github" ] && sudo dnf builddep --skip-unavailable "$specfile"
  rpmbuild --define "_topdir $(pwd)/tools/rpmbuild" -ba "$specfile"
}

# Checking for prerequisites and installing them.
[ ! -f /usr/local/bin/bazel ] && install_bazel

# Build RPM specs.
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
