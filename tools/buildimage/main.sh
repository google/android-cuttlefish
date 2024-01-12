#!/bin/bash

# Copyright (C) 2024 The Android Open Source Project
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

# Build custom image to be used in GCE VMs.

set -e

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)

pushd /tmp
wget https://cloud.debian.org/images/cloud/bullseye/20240104-1616/debian-11-genericcloud-amd64-20240104-1616.raw
popd

out_dir=$(pwd)
cp /tmp/debian-11-genericcloud-amd64-20240104-1616.raw ${out_dir}/disk.raw

bash ${script_dir}/install_gce_guest_environment.sh -r ${out_dir}/disk.raw
bash ${script_dir}/install_cf_packages.sh -r ${out_dir}/disk.raw -p $(pwd)

tar -czvf image.tar.gz -C ${out_dir} disk.raw
