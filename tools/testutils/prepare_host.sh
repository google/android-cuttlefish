#!/usr/bin/env bash

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
  local groups=$2
  echo "Creating user: ${username}"
  if [[ "${groups}" != "" ]]; then
    sudo useradd -G "${groups}" -m "${username}"
  else
    sudo useradd -m "${username}"
  fi
  sudo chmod a+rx "/home/${username}"
}

function setup_user_runtime_dir() {
  local username=$1
  echo "Setting up XDG_RUNTIME_DIR for user: ${username}"
  local uid=$(id -u "${username}")
  sudo mkdir -p -m 0700 "/run/user/${uid}"
  sudo chown "${username}:${username}" "/run/user/${uid}"
  sudo loginctl enable-linger "${username}" 2>/dev/null || true
}

function setup_podman_storage_conf() {
  local username=$1
  echo "Setting up Podman storage config for user: ${username}"
  local uid=$(id -u "${username}")
  local config_dir="/tmp/podcvd-podman-config"
  sudo mkdir -p "${config_dir}"
  local driver=$(sudo -u "${username}" XDG_RUNTIME_DIR="/run/user/${uid}" podman info --format '{{ .Store.GraphDriverName }}')
  local graphroot=$(sudo -u "${username}" XDG_RUNTIME_DIR="/run/user/${uid}" podman info --format '{{ .Store.GraphRoot }}')
  sudo tee "${config_dir}/storage.conf" >/dev/null <<EOF
[storage]
driver = "${driver}"
graphroot = "${graphroot}"
rootless_storage_path = "${graphroot}"
EOF
  sudo chown -R "${username}:${username}" "${config_dir}"
}


PKG_DIR=""
TEST_USER=""
EXTRA_GROUPS=""
PODCVD_MODE=false
while getopts "d:u:g:p" opt; do
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
    p)
      PODCVD_MODE=true
      ;;
    *)
    echo "Invalid option: -${opt}"
    echo "Usage: $0 -d PACKAGE_DIR [-u TEST_USER [-g EXTRA_GROUPS]] [-p]"
    exit 1
    ;;
  esac
done

if [[ "${PKG_DIR}" == "" ]] || ! [[ -d "${PKG_DIR}" ]]; then
  echo "Invalid package directory: ${PKG_DIR}"
  exit 1
fi

sudo apt-get update

if [[ "${PODCVD_MODE}" == true ]]; then
  install_pkgs "${PKG_DIR}" cuttlefish-podcvd
  load_kernel_modules kvm vhost-vsock vhost-net bridge
  grant_device_access vhost-vsock vhost-net kvm
  if [[ "${TEST_USER}" != "" ]]; then
    create_test_user "${TEST_USER}" "${EXTRA_GROUPS}"
    setup_user_runtime_dir "${TEST_USER}"
    setup_podman_storage_conf "${TEST_USER}"
    sudo /usr/bin/podcvd-setup "${TEST_USER}"
  fi
else
  install_pkgs "${PKG_DIR}" cuttlefish-base cuttlefish-metrics cuttlefish-user

  check_service_started cuttlefish-host-resources
  load_kernel_modules kvm vhost-vsock vhost-net bridge
  grant_device_access vhost-vsock vhost-net kvm
  check_service_started cuttlefish-operator

  if [[ "${TEST_USER}" != "" ]]; then
    create_test_user "${TEST_USER}" "kvm,cvdnetwork${EXTRA_GROUPS:+,${EXTRA_GROUPS}}"
  fi
fi
