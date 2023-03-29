#!/bin/bash
# Copyright 2023 Google Inc. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# validate number of arguments to equal 2
if [ "$#" -ne 2 ]; then
  echo "This script requires 2 parameters, server address and assigned base instance number"
  exit 1
fi

if [ -z ${ANDROID_PRODUCT_OUT+x} ]; then echo "ANDROID_PRODUCT_OUT is not defined"; exit 1; fi
if [ -z ${ANDROID_HOST_OUT+x} ]; then echo "ANDROID_HOST_OUT is not defined"; exit 1; fi

set -ex

# map arguments to variables
server=$1
base_instance_num=$2

# create a temp directory to store the artifacts
temp_dir=/tmp/cvd_dist
rm -rf $temp_dir
mkdir -p $temp_dir

# copy and compress the artifacts to the temp directory
ssh $server -t "mkdir -p ~/.cvd_artifact; mkdir -p ~/cvd_home"
cd $ANDROID_PRODUCT_OUT; tar -cvS $(cat required_images) | pigz -R > $temp_dir/img.tar.gz

# re-compress with rsyncable option
# TODO(b/275312073): remove this if toxbox supports rsyncable
# or b/273862156 for not building tar.gz file
cd $ANDROID_HOST_OUT/../linux_bionic-arm64; pigz -d -c cvd-host_package.tar.gz | pigz -R > $temp_dir/cvd-host_package.tar.gz
rsync -avh $temp_dir/* $server:.cvd_artifact --info=progress2

# extract the artifacts to the cvd home directory
ssh $server -t "cd .cvd_artifact; tar -zxvf img.tar.gz -C ~/cvd_home/; tar -zxvf cvd-host_package.tar.gz -C ~/cvd_home/"

web_ui_port=$((8443+$base_instance_num-1))
adb_port=$((6520+$base_instance_num-1))
fastboot_port=$((7520+$base_instance_num-1))
# sets up SSH port forwarding to the remote server for various ports and launch cvd instance
# port forward rule as base_instance_num=1 in local
ssh $server -L 8443:127.0.0.1:$web_ui_port \
  -L 15550:127.0.0.1:15550 -L 15551:127.0.0.1:15551 -L 15552:127.0.0.1:15552 \
  -L 15553:127.0.0.1:15553 -L 15554:127.0.0.1:15554 -L 15555:127.0.0.1:15555 \
  -L 15556:127.0.0.1:15556 -L 15557:127.0.0.1:15557 -L 15558:127.0.0.1:15558 \
  -L 6520:127.0.0.1:$adb_port -L 7520:127.0.0.1:$fastboot_port \
  -t "cd cvd_home && HOME=~/cvd_home bin/launch_cvd --base_instance_num=$base_instance_num"
