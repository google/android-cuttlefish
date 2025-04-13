#!/usr/bin/env bash

# It clones the repository.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit 1
[ $# -lt 3 ] && echo "REPO_USER, REPO_NAME or REPO_BRANCH missing on docker run, using defaults." && REPO_USER=syslogic && REPO_NAME=android-cuttlefish && REPO_BRANCH=rhel
[ $# -eq 3 ] && REPO_USER=$1 && REPO_NAME=$2 && REPO_BRANCH=$3

cd "${HOME}" && git clone "https://github.com/${REPO_USER}/${REPO_NAME}.git"
cd "${REPO_NAME}" || exit 1

# TODO: Line to be removed upon merging to `main` branch; the PR is `rhel`.
[ "${REPO_USER}" == "syslogic" ] && git switch "${REPO_BRANCH}"
