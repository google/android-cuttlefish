#!/usr/bin/env bash

# It clones the repository, builds the packages and archives them.
[ ! -f "${HOME}/.dockerfile" ] && echo ".dockerfile not present, exiting now." && exit
[ $# -eq 2 ] && REPO_USER=$1 && REPO_NAME=$2

REPO_DIR="${HOME}/${REPO_NAME}"
SRC_DIR="${REPO_DIR}/tools/rpmbuild"

# Clone the repository.
[ ! -f "${REPO_DIR}" ] && "${HOME}/clone.sh" "$REPO_USER" "$REPO_NAME"
cd "${REPO_DIR}" || exit

# Build and moves the RPM packages to `--volume` bind-mount.
# Problem: The `gh` client does not support artifact upload.
/bin/bash -c ./docker/rpm-builder/build_rpm_spec.sh

for f in "${SRC_DIR}"/RPMS/x86_64/*.rpm; do
  cp -v "$f" "${HOME}/.rpms/"
done
ls -la "${HOME}/.rpms"

#Note: This still stores the absolute path, but it archives.
tar -czf "${HOME}/.rpms/${REPO_NAME}-rpm.x86_64.tar.gz" "${SRC_DIR}/RPMS/x86_64"