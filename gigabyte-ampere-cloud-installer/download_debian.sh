#!/bin/sh

DEBIAN_DISTRIBUTION="$(lsb_release -c -s)"
DEBIAN_DOWNLOAD_URL_DEFAULT="https://cloud.debian.org/images/cloud/${DEBIAN_DISTRIBUTION}/latest"
if [ x"${DEBIAN_DOWNLOAD_URL}" = x"" ]; then
    DEBIAN_DOWNLOAD_URL="${DEBIAN_DOWNLOAD_URL_DEFAULT}"
fi

DEBIAN_DOWNLOAD_FILE_DEFAULT="debian-13-generic-arm64.raw"
if [ x"${DEBIAN_DOWNLOAD_FILE}" = x"" ]; then
    DEBIAN_DOWNLOAD_FILE="${DEBIAN_DOWNLOAD_FILE_DEFAULT}"
fi

wget -nv -c "${DEBIAN_DOWNLOAD_URL}/${DEBIAN_DOWNLOAD_FILE}"
