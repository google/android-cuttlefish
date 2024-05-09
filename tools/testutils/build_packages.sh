#!/bin/bash

set -e -x

function install_bazel() {
  # From https://bazel.build/install/ubuntu
  echo "Installing bazel"
  sudo apt install apt-transport-https curl gnupg -y
  curl -fsSL https://bazel.build/bazel-release.pub.gpg | gpg --dearmor >bazel-archive-keyring.gpg
  sudo mv bazel-archive-keyring.gpg /usr/share/keyrings
  echo "deb [arch=amd64 signed-by=/usr/share/keyrings/bazel-archive-keyring.gpg] https://storage.googleapis.com/bazel-apt stable jdk1.8" | sudo tee /etc/apt/sources.list.d/bazel.list
  # bazel needs the zip command to gather test outputs but doesn't depend on it
  sudo apt-get update && sudo apt-get install -y bazel zip unzip
}

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

install_bazel
install_debuild_dependencies

build_package "${REPO_DIR}/base"
build_package "${REPO_DIR}/frontend"
