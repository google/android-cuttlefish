#!/bin/bash

# Step 1. Build Cuttlefish docker image if there is no such one
cf_image=$(docker image list | grep "cuttlefish-orchestration")
if [ "$cf_image" == "" ]; then
  # Build CF docker image
  cd /home/vsoc-01
  if [ ! -d "/home/vsoc-01/android-cuttlefish" ]; then
    echo "wget https://github.com/google/android-cuttlefish/archive/refs/tags/stable.zip" > android-cuttlefish.log
    wget https://github.com/google/android-cuttlefish/archive/refs/tags/stable.zip
    unzip stable.zip
  fi
  cd android-cuttlefish-stable/docker/orchestration
  /bin/bash build.sh &> build.log
fi

# Step 2. Run CO server
# use the fixed version instead of HEAD, makes it easier to triage problems when they arise
CO_VERSION="0.1.0-alpha"
cd /home/vsoc-01
if [ ! -d "/home/vsoc-01/cloud-android-orchestration" ]; then
  echo "wget https://github.com/google/cloud-android-orchestration/archive/refs/tags/v$CO_VERSION.zip" > cloud-android-orchestration.log
  wget https://github.com/google/cloud-android-orchestration/archive/refs/tags/v$CO_VERSION.zip
  unzip v$CO_VERSION.zip
fi
cd cloud-android-orchestration-$CO_VERSION # Root directory of this repository
/bin/bash scripts/docker/run.sh &> run.log
