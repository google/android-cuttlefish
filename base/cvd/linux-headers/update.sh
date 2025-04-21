#!/bin/sh

set -e

usage() {
  echo "usage: $0 [linux-headers-common.deb]"
  exit
}

[ -z "$1" ] && usage

tmpdir="$(mktemp -d)"
rmtemp() {
  rm -rf "${tmpdir}"
  trap - EXIT
  exit
}
trap rmtemp EXIT HUP INT TERM

dpkg-deb -x "$1" "${tmpdir}"

script_dir="$(dirname $(readlink -f "$0"))"
include_dir=$(echo "${tmpdir}/usr/src/linux-headers-*/include")

cd ${include_dir}
find -type f -not -name Kbuild | colrm 1 2 | grep $(echo "
^uapi/linux/if_link.h$\|
^uapi/linux/netlink.h$\|
^uapi/linux/nl80211.h$
" | tr -d '\n') | while IFS= read -r line; do
  mkdir -p "${script_dir}/$(dirname ${line})"
  cp "$line" "${script_dir}/${line}"
done
