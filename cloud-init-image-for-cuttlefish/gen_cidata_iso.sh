#!/bin/sh

ISO_FILE="gigabyte-cidata.iso"

rm -f "${ISO_FILE}"

genisoimage -output "${ISO_FILE}" \
            -volid cidata \
            -joliet \
            -rock \
            user-data meta-data
