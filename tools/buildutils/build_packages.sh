#!/usr/bin/env bash

set -e -x

function disable_apt_daily() {
  echo 'DPkg::Lock::Timeout "120";' | sudo tee /etc/apt/apt.conf.d/99lock-timeout >/dev/null
  sudo systemctl stop apt-daily.timer apt-daily-upgrade.timer \
    apt-daily.service apt-daily-upgrade.service 2>/dev/null || true
  sudo systemctl mask apt-daily.timer apt-daily-upgrade.timer \
    apt-daily.service apt-daily-upgrade.service 2>/dev/null || true
}

function install_debuild_dependencies() {
  echo "Installing debuild dependencies"
  sudo apt-get install -y devscripts config-package-dev debhelper-compat equivs
}

REPO_DIR="$(realpath "$(dirname "$0")/../..")"
INSTALL_BAZEL="$(dirname $0)/installbazel.sh"
BUILD_PACKAGE="$(dirname $0)/build_package.sh"

disable_apt_daily
command -v bazel &> /dev/null || sudo "${INSTALL_BAZEL}"
install_debuild_dependencies

# "${BUILD_PACKAGE}" "${REPO_DIR}/container" $@
"${BUILD_PACKAGE}" "${REPO_DIR}/base" $@
"${BUILD_PACKAGE}" "${REPO_DIR}/frontend" $@
