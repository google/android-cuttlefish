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

if $(dirname $0)/filter-out-deps.sh $*; then
  exit 1
else
  exit 0
fi
