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

color_cyan="\033[0;36m"
color_plain="\033[0m"
color_yellow="\033[0;33m"

# validate number of arguments to equal 3
if [ "$#" -lt 2 ] || [ "$#" -gt 3 ]; then
  echo "This script requires 2 mandatory and 1 optional parameters, server address, base instance number and optionally number of instances to invoke"
  exit 1
fi

# map arguments to variables
server=$1
base_instance_num=$2
if [ "$#" -eq 3 ]; then
 num_instances=$3
else
 num_instances=1
fi

# set img_dir and cvd_host_tool_dir
img_dir=${ANDROID_PRODUCT_OUT:-$PWD}
cvd_host_tool_dir=${ANDROID_HOST_OUT:+"$ANDROID_HOST_OUT/../linux_musl-arm64"}
cvd_host_tool_dir=${cvd_host_tool_dir:-$PWD}

# create a temp directory to store the artifacts
temp_dir=/tmp/cvd_dist
rm -rf $temp_dir
mkdir -p $temp_dir

# copy and compress the artifacts to the temp directory
cvd_home_dir=cvd_home
ssh $server -t "mkdir -p ~/.cvd_artifact; mkdir -p ~/$cvd_home_dir"
if [ -f $img_dir/required_images ]; then
  rsync -aSvch --recursive $img_dir --files-from=$img_dir/required_images $server:~/$cvd_home_dir --info=progress2
else
  rsync -aSvch --recursive $img_dir/bootloader $img_dir/*.img $server:~/$cvd_home_dir --info=progress2
fi

# copy the cvd host package
if [ -d $cvd_host_tool_dir/cvd-host_package ]; then
  echo "Use contents in cvd-host_package dir"
  rsync -avch $cvd_host_tool_dir/cvd-host_package/* $server:~/$cvd_home_dir --info=progress2
elif [ -f $cvd_host_tool_dir/cvd-host_package.tar.gz ]; then
  echo "Use contents in cvd-host_package.tar.gz"
  # re-compress with rsyncable option
  # TODO(b/275312073): remove this if toxbox supports rsyncable
  cd $cvd_host_tool_dir; pigz -d -c cvd-host_package.tar.gz | pigz -R > $temp_dir/cvd-host_package.tar.gz
  rsync -avh $temp_dir/* $server:.cvd_artifact --info=progress2
  ssh $server -t "cd .cvd_artifact; tar -zxvf cvd-host_package.tar.gz -C ~/$cvd_home_dir/"
else
  echo "There is neither cvd-host_package dir nor cvd-host_package.tar.gz"
  exit 1
fi

trap cleanup SIGINT
cleanup() {
  echo -e "${color_yellow}SIGINT: stopping the launch instances${color_plain}"
  ssh $server -t "cd ~/$cvd_home_dir && HOME=~/$cvd_home_dir bin/stop_cvd"
}

# TODO(kwstephenkim): remove the flag at once if cuttlefish removes the flag
daemon_flag="--daemon=true"
instance_ids_flag="--base_instance_num=$base_instance_num \
  --num_instances=$num_instances"
echo -e "${color_cyan}Booting the cuttlefish instances${color_plain}"
ssh $server \
  -t "cd ~/$cvd_home_dir && HOME=~/$cvd_home_dir bin/launch_cvd $instance_ids_flag $daemon_flag"

# Web UI port is 2443 instead 1443 because there could be a running operator in this machine as well.
web_ui_port=2443
echo -e "Web UI port: $web_ui_port. ${color_cyan}Please point your browser to https://localhost:$web_ui_port for the UI${color_plain}"

# sets up SSH port forwarding to the remote server for various ports and launch cvd instance
adb_port_forwarding=""
print_launcher_logs=""
for instance_num in $(seq $base_instance_num $(($base_instance_num+$num_instances-1))); do
  device_name="cvd_$base_instance_num-$instance_num"
  adb_port=$((6520+$instance_num-1))
  echo -e "$device_name is using adb port $adb_port. Try ${color_cyan}adb connect 127.0.0.1:${adb_port}${color_plain} if you want to connect to this device"
  adb_port_forwarding+="-L $adb_port:127.0.0.1:$adb_port "
  print_launcher_logs+="tail -f ~/$cvd_home_dir/cuttlefish/instances/cvd-$instance_num/logs/launcher.log | sed 's/^/[$device_name] /' &"
done

ports_forwarding="-L $web_ui_port:127.0.0.1:1443 \
  -L 15550:127.0.0.1:15550 -L 15551:127.0.0.1:15551 -L 15552:127.0.0.1:15552 \
  -L 15553:127.0.0.1:15553 -L 15554:127.0.0.1:15554 -L 15555:127.0.0.1:15555 \
  -L 15556:127.0.0.1:15556 -L 15557:127.0.0.1:15557 -L 15558:127.0.0.1:15558 \
  $adb_port_forwarding"
echo "Set up ssh ports forwarding: $ports_forwarding"
echo -e "${color_yellow}Please stop the running instances by ctrl+c${color_plain}"
ssh $server $ports_forwarding $print_launcher_logs
