#!/bin/bash

set -e -x

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
  debuild -i -uc -us -b
  popd
}

REPO_DIR="$(realpath "$(dirname "$0")/../..")"

install_debuild_dependencies

build_package "${REPO_DIR}/base"
build_package "${REPO_DIR}/frontend"
