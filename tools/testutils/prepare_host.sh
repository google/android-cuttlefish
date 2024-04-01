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

function install_pkgs() {
  local pkgdir="$1"
  shift
  for pkg in "$@"; do
        echo "Installing package: ${pkg}"
        sudo apt-get install -y "${pkgdir}/${pkg}"_*_*64.deb
  done
}

function check_service_started() {
  local service="$1"
  echo "Checking service ${service} status"
  systemctl is-active "${service}"
}

function load_kernel_modules() {
        echo "Loading kernel modules"
        sudo modprobe "$@"
}

function grant_device_access() {
  for d in "$@"; do
    ls -l /dev/"${d}"
    sudo chmod a+rw /dev/"${d}"
  done
}

function create_test_user() {
  local username=$1
  local groups="kvm,cvdnetwork"
  if [[ "$2" != "" ]]; then
    groups="${groups},$2"
  fi
  echo "Creating user: ${username}"
  sudo useradd -G "${groups}" -m "${username}"
  sudo chmod a+rx "/home/${username}"
}


PKG_DIR=""
TEST_USER=""
EXTRA_GROUPS=""
while getopts "d:u:g:" opt; do
  case "${opt}" in
    u)
      TEST_USER="${OPTARG}"
      ;;
    g)
      EXTRA_GROUPS="${OPTARG}"
      ;;
    d)
      PKG_DIR="${OPTARG}"
      ;;
    *)
    echo "Invalid option: -${opt}"
    echo "Usage: $0 -d PACKAGE_DIR [-u TEST_USER [-g EXTRA_GROUPS]]"
    exit 1
    ;;
  esac
done

if [[ "${PKG_DIR}" == "" ]] || ! [[ -d "${PKG_DIR}" ]]; then
  echo "Invalid package directory: ${PKG_DIR}"
  exit 1
fi

install_bazel

install_pkgs "${PKG_DIR}" cuttlefish-base cuttlefish-user

check_service_started cuttlefish-host-resources
load_kernel_modules kvm vhost-vsock vhost-net bridge
grant_device_access vhost-vsock vhost-net kvm
check_service_started cuttlefish-operator

if [[ "${TEST_USER}" != "" ]]; then
  create_test_user "${TEST_USER}" "${EXTRA_GROUPS}"
fi
