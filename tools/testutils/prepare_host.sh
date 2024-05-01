#!/bin/bash

set -e -x

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

install_pkgs "${PKG_DIR}" cuttlefish-base cuttlefish-user

check_service_started cuttlefish-host-resources
load_kernel_modules kvm vhost-vsock vhost-net bridge
grant_device_access vhost-vsock vhost-net kvm
check_service_started cuttlefish-operator

if [[ "${TEST_USER}" != "" ]]; then
  create_test_user "${TEST_USER}" "${EXTRA_GROUPS}"
fi
