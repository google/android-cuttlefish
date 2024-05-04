#!/bin/bash

set -o errexit

function print_help {
	echo "   $0 <options>"
	echo "     Options:"
	echo "       -a | --arch                      : architecture to download for: x86, x86_64, arm64"
	echo "       -A | --android                   : download the Android OS images (by default disabled)"
	echo "       -D | --download (default off)    : download images rather than streaming/uncompressing on the fly"
	echo "       -K | --keep (default off)        : keep archives after extracting them (requires -D)"
	echo "       -X | --no-extract (default off)   : do not extract extract downloaded archives (requires -D -K)"
	echo "       -U | --no-userdata (default off) : skip downloading userdata.img (requires -A and not -D)"
	echo "       -C | --cuttlefish                : download the Cuttlefish host images (by default disabled)"
	echo "       -h | --help                      : print this help message"
}

function parse_opts() {
	local help=0
	local params

	if params=$(getopt -o 'a:ADKXUCh' -l 'arch:,android,download,keep,no-userdata,cuttlefish,help' --name "$0" -- "$@"); then
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
							echo "Unknown architecture ${ARCH}" 1>&2
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
				-D|--download)
					DOWNLOAD=1
					shift
					;;
				-K|--keep)
					KEEP=1
					shift
					;;
				-X|--no-extract)
					EXTRACT=0
					shift
					;;
				-U|--no-userdata)
					SKIP_USERDATA='--exclude userdata.img'
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

	if [[ -n "${SKIP_USERDATA}" && "${DOWNLOAD_ANDROID}" -eq 0 ]]; then
		echo "Option -U/--no-userdata requires -A/--android" 1>&2
		help=1
	fi

	if [[ -n "${SKIP_USERDATA}" && "${DOWNLOAD}" -eq 1 ]]; then
		echo "Option -U/--no-userdata does not work with -D/--download" 1>&2
		help=1
	fi

	if [[ "${KEEP}" -eq 1 && "${DOWNLOAD}" -eq 0 ]]; then
		echo "Option -K/--keep requires -D/--download" 1>&2
		help=1
	fi

	if [[ "${EXTRACT}" -eq 0 && "${KEEP}" -eq 0 ]]; then
		echo "Option -X/--no-extract requires -K/--keep" 1>&2
		help=1
	fi

	if [ "${help}" -eq 1 ]; then
		print_help
		exit 1
	fi

	if [[ -n "${SKIP_USERDATA}" ]]; then
cat<<EOF

    NOTE: A blank userdata.img will not be downloaded.  Make sure to start
          Cuttlefish with the options --data_policy create_if_missing
          --blank_data_image_mb=<size in MB>.

EOF
	fi
}

DOWNLOAD_ANDROID=0
DOWNLOAD_CF=0
DOWNLOAD=0
KEEP=0
EXTRACT=1
SKIP_USERDATA=""

parse_opts $*

URL=https://ci.android.com/builds/latest/branches/aosp-main${THROTTLED}/targets/aosp_cf_${ARCH}_phone-userdebug/view/BUILD_INFO
RURL=$(curl -Ls -o /dev/null -w %{url_effective} ${URL})

if [ "${DOWNLOAD_ANDROID}" -eq 1 ]; then
	echo "#"
	echo "# Downloading the latest Android images from AOSP"
	echo "#"
	set -x
	IMG=aosp_cf_${ARCH}_phone-img-$(echo $RURL | awk -F\/ '{print $6}').zip
	if [[ "${DOWNLOAD}" -eq 1 ]]; then
		wget -nv ${RURL%/view/BUILD_INFO}/raw/${IMG}
		if [[ "${EXTRACT}" -eq 1 ]]; then
			unzip "${IMG}"
		fi
		if [[ "${KEEP}" -eq 0 ]]; then
			rm -v "${IMG}"
		fi
	else
		wget -O- -nv --show-progress --progress=bar:giga --prefer-family=IPv4 -c ${RURL%/view/BUILD_INFO}/raw/${IMG} | bsdtar -x -v -f- ${SKIP_USERDATA}
	fi
	set +x
fi

if [ "${DOWNLOAD_CF}" -eq 1 ]; then
	echo "#"
	echo "# Downloading the latest Cuttlefish images from AOSP"
	echo "#"
	set -x
	if [[ "${DOWNLOAD}" -eq 1 ]]; then
		wget -nv ${RURL%/view/BUILD_INFO}/raw/cvd-host_package.tar.gz
		if [[ "${EXTRACT}" -eq 1 ]]; then
			tar xzvf cvd-host_package.tar.gz
		fi
		if [[ "${KEEP}" -eq 0 ]]; then
			rm -v cvd-host_package.tar.gz
		fi
	else
		wget -O- -nv --show-progress --progress=bar:giga --prefer-family=IPv4 -c ${RURL%/view/BUILD_INFO}/raw/cvd-host_package.tar.gz | tar -xzvf-
	fi
	set +x
fi
