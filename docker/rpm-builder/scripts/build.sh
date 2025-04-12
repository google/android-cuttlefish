#!/usr/bin/env bash

# It clones the repository, builds the packages and archives them.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit 1
[ $# -lt 3 ] && echo "REPO_USER, REPO_NAME or REPO_BRANCH missing, using defaults." && REPO_USER=syslogic && REPO_NAME=android-cuttlefish && REPO_BRANCH=rhel
[ $# -eq 2 ] && REPO_USER=$1 && REPO_NAME=$2 && REPO_BRANCH=rhel
[ $# -eq 3 ] && REPO_USER=$1 && REPO_NAME=$2 && REPO_BRANCH=$3

RPMS="${HOME}/.rpms"
REPO_DIR="${HOME}/${REPO_NAME}"
SRC_DIR="${REPO_DIR}/tools/rpmbuild"

# Clone the repository.
[ ! -f "${REPO_DIR}" ] && "${HOME}/clone.sh" "$REPO_USER" "$REPO_NAME" "$REPO_BRANCH"
cd "${REPO_DIR}" || exit

# Build and moves the RPM packages to `--volume` bind-mount.
# Problem: The `gh` client does not support artifact upload.
/bin/bash -c ./docker/rpm-builder/build_rpm_spec.sh

if [ -d "${SRC_DIR}/RPMS/x86_64" ]; then
  for f in "${SRC_DIR}"/RPMS/x86_64/*.rpm; do
    cp -v "$f" "${RPMS}/"
  done
  # The `tar` command stores the absolute path, but it archives.
  # tar -czf "${HOME}/.rpms/${REPO_NAME}-rpm.x86_64.tar.gz" "${SRC_DIR}/RPMS/x86_64"
fi

if [ -d "${SRC_DIR}/RPMS/aarch64" ]; then
  for f in "${SRC_DIR}"/RPMS/aarch64/*.rpm; do
    cp -v "$f" "${RPMS}/"
  done
  # The `tar` command stores the absolute path, but it archives.
  # tar -czf "${HOME}/.rpms/${REPO_NAME}-rpm.aarch64.tar.gz" "${SRC_DIR}/RPMS/aarch64"
fi

echo "Rocky mount-point: ${RPMS}"
ls -la "${RPMS}"
ls -lan "${RPMS}"

# shellcheck disable=SC2012
# [ "$(ls -1 "${RPMS}" 2>/dev/null | wc -l)" -eq 4 ] || exit 1
[ "$(ls -1 "${RPMS}" 2>/dev/null | wc -l)" -gt 0 ] || exit 1
