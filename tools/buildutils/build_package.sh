#!/usr/bin/env bash

# Copyright (C) 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
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

set -o errexit -o nounset -o pipefail

function print_usage() {
  >&2 echo "usage: $0 /path/to/pkgdir [debuild options]"
}

if [[ $# -eq 0 ]]; then
  >&2 echo "missing path to package directory"
  print_usage
  exit 1
fi

readonly PKGDIR="$1"
shift

pushd "${PKGDIR}"
echo "Installing package dependencies"
sudo mk-build-deps -i -t 'apt-get -o Debug::pkgProblemResolver=yes --no-install-recommends -y'
echo "Building packages"
debuild $@ --prepend-path /usr/local/bin -i -uc -us -b
popd
