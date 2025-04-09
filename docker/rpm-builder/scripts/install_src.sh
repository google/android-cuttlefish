#!/usr/bin/env bash

# It will run build.sh or DNF install/remove.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit 1
[ $# -lt 3 ] && echo "REPO_USER, REPO_NAME or REPO_BRANCH missing, using defaults." && REPO_USER=syslogic && REPO_NAME=android-cuttlefish && REPO_BRANCH=redhat-workflow
[ $# -eq 3 ] && REPO_USER=$1 && REPO_NAME=$2 && REPO_BRANCH=$3

cd "$HOME" || exit
RPMS="${HOME}/.rpms"

# Build the repository, when the directory is not present.
[ ! -f "$HOME/${REPO_NAME}" ] && ./build.sh "$REPO_USER" "$REPO_NAME" "$REPO_BRANCH"
[ ! -d "${RPMS}" ] && mkdir -p "${RPMS}"
cd "${RPMS}" || exit

[ "$(ls -1 *.rpm 2>/dev/null | wc -l)" -gt 0 ] && ./build.sh

# Install RPM packages.
./rpm_instal.sh
