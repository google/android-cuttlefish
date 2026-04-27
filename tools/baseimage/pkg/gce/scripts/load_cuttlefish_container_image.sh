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

if [[ $# -lt 2 ]] ; then
  echo "usage: $0 [file|pull] reference"
  exit 1
fi
mode=$1
reference=$2

# Load docker image. Internally, it modifies data-root configuration of
# /etc/docker/daemon.json to load image under default data-root location of
# mounted attached disk. With this approach, docker installed on chroot
# environment can recognize and use docker image when dockerd starts to run
# on running GCE instance with GCE image created via this script.
sudo apt install -y docker.io jq
sudo systemctl stop docker.socket docker.service
if [ -f /etc/docker/daemon.json ]; then
  sudo jq '.["data-root"] = "/mnt/image/var/lib/docker"' /etc/docker/daemon.json \
    | sudo tee /etc/docker/daemon.json > /dev/null
else
  sudo jq -n '.["data-root"] = "/mnt/image/var/lib/docker"' \
    | sudo tee /etc/docker/daemon.json > /dev/null
fi
sudo systemctl start docker.socket docker.service
if [ "$mode" == "file" ]; then
  sudo docker load --input "$reference"
elif [ "$mode" == "pull" ]; then
  sudo docker pull "$reference"
else
  echo "Unknown mode: $mode"
  exit 1
fi
sudo systemctl stop docker.socket docker.service

sudo chroot /mnt/image /usr/bin/apt install -y docker.io
