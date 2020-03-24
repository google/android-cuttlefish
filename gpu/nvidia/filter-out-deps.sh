#!/bin/bash

set -o errexit
# set -x

# $1 = parent
# $2 = parent version
# $3 = name
# $4 = version
# $5 = op
# $6 = filter
# $7 = process

parent="$1"
parent_version="$2"
name="$3"
version="$4"
op="$5"
filter="$6"
process="$7"

# grep -qw '^.*nvidia.*kernel.*\|^xserver-xorg-video-nvidia$\|^nvidia-settings$\|^libx11.*$\|^nvidia-updater$' <<< ${name}
# grep -q '.*nvidia.*kernel.*' <<< ${name}
grep -q '^.*nvidia.*kernel.*$\|^xserver-xorg-video-nvidia$' <<< ${name}
