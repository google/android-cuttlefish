#!/bin/bash
#
# Copyright (C) 2017 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0(the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

# Installs an Android zip file to a directory

set -e

usage() {
  echo cat build.zip \| $0 "\${dir} [ -- file_to_extract [another_file]... ]"
  echo or
  echo $0 build-zip "\${dir} [ -- file_to_extract [another_file]... ]"
}

# sanitize input to treat everything after '--' as files to be extracted
idx=0
for arg in $@; do
    if [[ "$arg" == "--" ]]; then break; fi
    idx=$((idx+1))
done
files_to_extract=${@:(($idx+2))}
set -- ${@:1:$idx}

case $# in
  1)
    source=-
    destdir="$1"
    ;;
  2)
    source="$1"
    destdir="$2"
    sparse=-S
    ;;
  *)
    usage
    exit 2
    ;;
esac

mkdir -p "${destdir}"
bsdtar -x -C "${destdir}" -f "${source}" ${sparse} ${files_to_extract}

if [[ " ${files_to_extract[*]} " == *" boot.img "* ]]; then
    /usr/lib/cuttlefish-common/bin/unpack_boot_image.py -boot_img "${destdir}/boot.img" -dest "${destdir}"
fi

find "${destdir}" -name "*.img" -exec sh -c '
  img="$0"
  file "$img" | grep "Android sparse image," -q  \
    && simg2img "$img" "$img.inflated"           \
    && mv "$img.inflated" "$img"
' {} ';'

exit 0
