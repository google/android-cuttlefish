#!/bin/bash

set -e -x

REPO_DIR="$(realpath "$(dirname "$0")/../..")"

function install_debuild_dependencies() {
  echo "Installing debuild dependencies"
  sudo apt-get update
  sudo DEBIAN_FRONTEND=noninteractive apt-get -y \
    -o Dpkg::Options::="--force-confold" \
    -o Dpkg::Options::="--force-confdef" \
    upgrade
  sudo apt-get install -y devscripts config-package-dev debhelper-compat equivs
}

function build_package() {
  local pkgdir="$1"
  pushd "${pkgdir}"
  echo "Installing package dependencies"
  sudo mk-build-deps -i -t 'apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends -y'
  echo "Building packages"
  debuild --prepend-path /usr/local/bin -i -uc -us -b
  popd
}

function build_spec() {
  local specfile="${REPO_DIR}/tools/rpmbuild/SPECS/$1"
  echo "Installing package dependencies"
  sudo dnf builddep --skip-unavailable $specfile
  echo "Building packages"
  rpmbuild --define "_topdir `pwd`/tools/rpmbuild" -v -ba $specfile
}

if [[ -f /bin/dnf ]]; then
  build_spec cuttlefish_base.spec
  build_spec cuttlefish_user.spec
  build_spec cuttlefish_integration.spec
  build_spec cuttlefish_orchestration.spec
  exit 0
else
  INSTALL_BAZEL="$(dirname $0)/installbazel.sh"
  if ! { command -v bazel || command -v bazelisk; } >/dev/null 2>&1; then sudo "${INSTALL_BAZEL}"; fi
  install_debuild_dependencies
  build_package "${REPO_DIR}/base"
  build_package "${REPO_DIR}/frontend"
  exit 0
fi