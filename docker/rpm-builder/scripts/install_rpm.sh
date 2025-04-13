#!/usr/bin/env bash

# It will run DNF install.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit 1
cd "${HOME}/.rpms" || echo "${HOME}/.rpms not found, exiting now." && exit 1

PACKAGES="nano"
for FILE in ${HOME}/.rpms; do
    PACKAGES="${PACKAGES} $FILE"
done
echo "Packages to install: ${PACKAGES}"
dnf -y install "${PACKAGES}"
