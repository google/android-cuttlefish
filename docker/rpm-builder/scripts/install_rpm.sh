#!/usr/bin/env bash

# It will run DNF install.
[ ! -f "${HOME}/.dockerfile" ] && echo ".dockerfile not present, exiting now." && exit
cd /root/.rpms || echo "/root/.rpms not found, exiting now." && exit

PACKAGES=""
for file in ./*.rpm; do
    PACKAGES="${PACKAGES} $file"
done
dnf -y install "${PACKAGES}"
