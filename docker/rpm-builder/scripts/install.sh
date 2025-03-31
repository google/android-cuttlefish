#!/usr/bin/env bash

# It will run build.sh or DNF install/remove.
[ ! -f "${HOME}/.dockerfile" ] && echo ".dockerfile not present, exiting now." && exit
[ $# -eq 2 ] && REPO_USER=$1 && REPO_NAME=$2
RPMS="${HOME}/.rpms"

# Build the repository, when the directory is not present.
cd "$HOME" || exit
[ ! -f "$HOME/${REPO_NAME}" ] && ./build.sh "$REPO_USER" "$REPO_NAME"
[ ! -d "${RPMS}" ] && mkdir -p "${RPMS}"

cd "${RPMS}" || exit
[ "$(ls -1 *.rpm 2>/dev/null | wc -l)" -gt 0 ] && ./build.sh

sudo dnf -y install "${RPMS}/cuttlefish-*.rpm"
