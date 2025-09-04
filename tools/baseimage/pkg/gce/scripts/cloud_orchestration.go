// Copyright (C) 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package scripts

const InstallCODockerContainer = `#!/usr/bin/env bash
set -o errexit -o nounset -o pipefail

ARCH=$(uname -m)
if [ "$ARCH" == "x86_64" ]; then
    ARCH="amd64"
elif [ "$ARCH" == "aarch64" ]; then
    ARCH="arm64"
fi

# TODO(b/442924024): Apply chroot
sudo install -m 0755 -d /etc/apt/keyrings
sudo curl -fsSL https://download.docker.com/linux/debian/gpg -o /etc/apt/keyrings/docker.asc
sudo chmod a+r /etc/apt/keyrings/docker.asc
echo "deb [arch=$ARCH signed-by=/etc/apt/keyrings/docker.asc] https://download.docker.com/linux/debian bookworm stable" |\
    sudo tee /etc/apt/sources.list.d/docker.list
sudo apt update
sudo apt install -y containerd.io docker-buildx-plugin docker-ce docker-ce-cli docker-compose-plugin

sudo docker pull us-docker.pkg.dev/android-cuttlefish-artifacts/cuttlefish-orchestration/cuttlefish-cloud-orchestrator
sudo docker pull us-docker.pkg.dev/android-cuttlefish-artifacts/cuttlefish-orchestration/cuttlefish-orchestration
sudo mkdir -p /etc/cloud_orchestrator
sudo wget -O /etc/cloud_orchestrator/conf.toml \
    https://artifactregistry.googleapis.com/download/v1/projects/android-cuttlefish-artifacts/locations/us/repositories/cloud-orchestrator-config/files/on-premise-single-server:main:conf.toml:download?alt=media

sudo docker run --restart unless-stopped -d -p 8080:8080 -e CONFIG_FILE="/conf.toml" \
    -v /etc/cloud_orchestrator/conf.toml:/conf.toml \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -t us-docker.pkg.dev/android-cuttlefish-artifacts/cuttlefish-orchestration/cuttlefish-cloud-orchestrator:latest
`
