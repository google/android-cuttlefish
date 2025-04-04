#!/usr/bin/env bash

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

if [[ -f /bin/apt ]]; then
  if ! { command -v bazel || command -v bazelisk; } &> /dev/null; then sudo "$(dirname $0)/installbazel.sh"; fi
  install_debuild_dependencies
  build_package "${REPO_DIR}/base"
  build_package "${REPO_DIR}/frontend"
else
  "${REPO_DIR}/docker/rpm-builder/build_rpm_spec.sh"
fi
