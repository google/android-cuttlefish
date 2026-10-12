#!/usr/bin/env bash
#
# Copyright (C) 2026 The Android Open Source Project
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
#
# Stops the IPv6 upstream simulator. Removing the container destroys its
# network namespace, which deletes both ends of the veth pair and the route
# via the transit link in the target namespace.
#
# Usage: sim_down.sh [--name NAME] [--purge-certs [--state DIR]]

set -euo pipefail

NAME=cf-ipv6-upstream-sim
STATE=${XDG_CACHE_HOME:-$HOME/.cache}/cf-ipv6-upstream-sim
PURGE=0
while [ $# -gt 0 ]; do
  case "$1" in
    --name) NAME=$2; shift 2 ;;
    --state) STATE=$2; shift 2 ;;
    --purge-certs) PURGE=1; shift ;;
    *) echo "usage: sim_down.sh [--name NAME] [--purge-certs [--state DIR]]" >&2; exit 1 ;;
  esac
done

if docker inspect "$NAME" >/dev/null 2>&1; then
  docker rm -f "$NAME" >/dev/null
  echo "sim_down: removed container $NAME"
else
  echo "sim_down: container $NAME not running"
fi
if [ "$PURGE" = 1 ] && [ -d "$STATE" ]; then
  rm -rf "$STATE"
  echo "sim_down: removed $STATE"
fi
