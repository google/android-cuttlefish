#!/usr/bin/env bash

# It clones the repository, builds the packages and archives them.
[ ! -f "${HOME}/.dockerfile" ] && echo ".dockerfile not present, exiting now." && exit
[ $# -eq 2 ] && REPO_USER=$1 && REPO_NAME=$2

REPO_DIR="${HOME}/${REPO_NAME}"
SRC_DIR="${REPO_DIR}/tools/rpmbuild"

# Clone the repository.
[ ! -f "${REPO_DIR}" ] && "${HOME}/clone.sh" "$REPO_USER" "$REPO_NAME"
cd "${REPO_DIR}" || exit

# Build and moves the RPM packages to bind-mount.
# Note: One could as well publish them.
/bin/bash -c ./docker/rpm-builder/build_rpm_spec.sh

mv "${SRC_DIR}/RPMS/x86_64/*.rpm" "${HOME}/.rpms"
mv "${SRC_DIR}/SPMS/x86_64/*.rpm" "${HOME}/.rpms"
tar czf "${HOME}/.rpms/${REPO_NAME}-rpm.tar.gz" "${HOME}/.rpms"
ls -la "${HOME}/.rpms"
