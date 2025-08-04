#!/usr/bin/env bash

set -e -x

function install_debuild_dependencies() {
  echo "Installing debuild dependencies"
  sudo apt-get update
  sudo DEBIAN_FRONTEND=noninteractive apt-get -y --allow-downgrades \
    -o Dpkg::Options::="--force-confold" \
    -o Dpkg::Options::="--force-confdef" \
    upgrade
  sudo apt-get install -y devscripts config-package-dev debhelper-compat equivs
}

REPO_DIR="$(realpath "$(dirname "$0")/../..")"
INSTALL_BAZEL="$(dirname $0)/installbazel.sh"
BUILD_PACKAGE="$(dirname $0)/build_package.sh"

command -v bazel &> /dev/null || sudo "${INSTALL_BAZEL}"
install_debuild_dependencies

"${BUILD_PACKAGE}" "${REPO_DIR}/base" $@
"${BUILD_PACKAGE}" "${REPO_DIR}/frontend" $@
