#!/usr/bin/env bash

# It clones the repository.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit
[ $# -eq 2 ] && REPO_USER=$1 && REPO_NAME=$2

cd "${HOME}" && git clone "https://github.com/${REPO_USER}/${REPO_NAME}.git"
cd "${REPO_NAME}" || exit

# TODO: Line to be removed upon merging to `main` branch.
[ "${REPO_USER}" == "syslogic" ] && git switch rpm-build
