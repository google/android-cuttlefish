#!/bin/bash

# Step 1. Build Cuttlefish docker image if there is no such one
cf_image=$(docker image list | grep "cuttlefish-orchestration")
if [ "$cf_image" == "" ]; then
  # Build CF docker image
  cd /home/vsoc-01
  if [ ! -d "/home/vsoc-01/android-cuttlefish" ]; then
    echo "git  clone https://github.com/google/android-cuttlefish.git" > android-cuttlefish.log
    wget https://github.com/google/android-cuttlefish/archive/refs/tags/stable.zip
    unzip stable.zip
  fi
  cd android-cuttlefish-stable/docker/orchestration
  /bin/bash build.sh &> build.log
fi

# Step 2. Run CO server
cd /home/vsoc-01
if [ ! -d "/home/vsoc-01/cloud-android-orchestration" ]; then
  echo "git clone https://github.com/google/cloud-android-orchestration.git" > cloud-android-orchestration.log
  wget https://github.com/google/cloud-android-orchestration/archive/refs/heads/main.zip
  unzip main.zip
fi
cd cloud-android-orchestration-main # Root directory of this repository
/bin/bash scripts/docker/run.sh &> run.log
