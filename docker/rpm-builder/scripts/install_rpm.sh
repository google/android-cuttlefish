#!/usr/bin/env bash

# It will run DNF install.
[ ! -f "${HOME}/.dockerenv" ] && echo ".dockerenv not present, exiting now." && exit
cd "${HOME}/.rpms" || echo "${HOME}/.rpms not found, exiting now." && exit

PACKAGES=""
for file in ./*.rpm; do
    PACKAGES="${PACKAGES} $file"
done
echo "Packages to install: ${PACKAGES}"
dnf -y install "${PACKAGES}"
