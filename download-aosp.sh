#!/bin/bash

set -o errexit

function print_help {
	echo "   $0 <options>"
	echo "     Options:"
	echo "       -a | --arch                      : architecture to download for: x86, x86_64, arm64"
	echo "       -A | --android                   : download the Android OS images (by default disabled)"
	echo "       -C | --cuttlefish                : download the Cuttlefish host images (by default disabled)"
	echo "       -h | --help                      : print this help message"
}

function parse_opts() {
	local help=0
	local params

	if params=$(getopt -o 'a:ACh' -l 'arch:,android,cuttlefish,help' --name "$0" -- "$@"); then
		eval set -- "${params}"
		while true; do
			case "$1" in
				-a|--arch)
					ARCH=$2
					case "${ARCH}" in
						x86)
							THROTTLED=
							;;
						x86_64)
							THROTTLED='-throttled'
							;;
						arm64|aarch64)
							ARCH='arm64'
							THROTTLED='-throttled'
							;;
						*)
							echo "Uknown architecture ${ARCH}" 1>&2
							help=1
							;;
					esac
					shift 2
					;;
				-A|--android)
					DOWNLOAD_ANDROID=1
					shift
					;;
				-C|--cuttlefish)
					DOWNLOAD_CF=1
					shift
					;;
				-h|--help)
					help=1
					shift
					;;
				--)
					shift
					break
					;;
			esac
		done
	fi

	if [ -z "${ARCH}" ]; then
		echo "You must specify an architecture." 1>&2
		help=1
	fi

	if [[ "${DOWNLOAD_ANDROID}" -eq 0 && "${DOWNLOAD_CF}" -eq 0 ]]; then
		echo "You are not downloading anything.  Select -A and/or -C" 1>&2
		help=1
	fi

	if [ "${help}" -eq 1 ]; then
		print_help
		exit 1
	fi
}

DOWNLOAD_ANDROID=0
DOWNLOAD_CF=0

parse_opts $*

URL=https://ci.android.com/builds/latest/branches/aosp-master${THROTTLED}/targets/aosp_cf_${ARCH}_phone-userdebug/view/BUILD_INFO
RURL=$(curl -Ls -o /dev/null -w %{url_effective} ${URL})

if [ "${DOWNLOAD_ANDROID}" -eq 1 ]; then
	echo "#"
	echo "# Downloading the latest Android images from AOSP"
	echo "#"
	set -x
	IMG=aosp_cf_${ARCH}_phone-img-$(echo $RURL | awk -F\/ '{print $6}').zip
	wget -nv ${RURL%/view/BUILD_INFO}/raw/${IMG}
	unzip "${IMG}"
	rm -v "${IMG}"
	set +x
fi

if [ "${DOWNLOAD_CF}" -eq 1 ]; then
	echo "#"
	echo "# Downloading the latest Cuttlefish images from AOSP"
	echo "#"
	set -x
	wget -nv ${RURL%/view/BUILD_INFO}/raw/cvd-host_package.tar.gz
	tar xzvf cvd-host_package.tar.gz
	rm -v cvd-host_package.tar.gz
	set +x
fi
