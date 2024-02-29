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

function build_and_install_pkgs() {
  local pkgdir="$1"
  shift
  pushd "${pkgdir}"
  echo "Installing dependencies for: $@"
  sudo mk-build-deps -i -t 'apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends -y'
  echo "Building packages: $@"
  debuild -i -uc -us -b
  for pkg in "$@"; do
        echo "Installing package: ${pkg}"
        sudo apt-get install -y ../"${pkg}"_*_*64.deb
  done
  popd
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


REPO_DIR="$(realpath "$(dirname "$0")/..")"
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
    *)
    echo "Invalid option: -${opt}"
    echo "Usage: $0 [-u TEST_USER [-g EXTRA_GROUPS]]"
    exit 1
    ;;
  esac
done

if [[ "${REPO_DIR}" == "" ]] || ! [[ -d "${REPO_DIR}" ]]; then
  echo "Invalid repo directory: ${REPO_DIR}"
  exit 1
fi

install_debuild_dependencies

install_bazel

build_and_install_pkgs "${REPO_DIR}/base" cuttlefish-base
check_service_started cuttlefish-host-resources
load_kernel_modules kvm vhost-vsock vhost-net bridge
grant_device_access vhost-vsock vhost-net kvm

build_and_install_pkgs "${REPO_DIR}/frontend" cuttlefish-user
check_service_started cuttlefish-operator

if [[ "${TEST_USER}" != "" ]]; then
  create_test_user "${TEST_USER}" "${EXTRA_GROUPS}"
fi
