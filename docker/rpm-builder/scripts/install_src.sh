#!/usr/bin/env bash

# It will run build.sh or DNF install/remove.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit
[ $# -lt 2 ] && echo "REPO_USER and/or REPO_NAME missing, exiting now." && exit 1
[ $# -eq 2 ] && REPO_USER=$1 && REPO_NAME=$2

cd "$HOME" || exit
RPMS="${HOME}/.rpms"

# Build the repository, when the directory is not present.
[ ! -f "$HOME/${REPO_NAME}" ] && ./build.sh "$REPO_USER" "$REPO_NAME"
[ ! -d "${RPMS}" ] && mkdir -p "${RPMS}"
cd "${RPMS}" || exit

[ "$(ls -1 *.rpm 2>/dev/null | wc -l)" -gt 0 ] && ./build.sh

# Install RPM packages.
./rpm_instal.sh
