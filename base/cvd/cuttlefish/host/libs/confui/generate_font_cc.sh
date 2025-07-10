#!/bin/bash

set -o pipefail

fontfile="$1"
varname="$2"

# The array variable name is followed by [], while the length variable name ends
# in _len. This would be much easier with the -n option, but it's not present in
# all platforms.
xxd -i "${fontfile}" | \
  sed -e "s|[^ ]*\\[|${varname}\\[|" | \
  sed -e "s|[^ ]*_len |${varname}_length |"
