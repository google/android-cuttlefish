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

# Export the host image to GCP.
#
# Image Name Format: cf-debian11-amd64-YYYYMMDD-commit
#
# IMPORTANT!!! Artifact download URL only work for registered users (404 for guests)
# https://github.com/actions/upload-artifact/issues/51

set -e

usage() {
  echo "usage: $0 -s head-commit-sha -t /path/to/github_auth_token.txt -b bucket-name -p project-name -f image-family-name"
}

commit_sha=
github_auth_token_filename=
gce_bucket=
image_dest_project=
image_family_name=

while getopts ":hs:t:p:b:f:" opt; do
  case "${opt}" in
    h)
      usage
      exit 0
      ;;
    s)
      commit_sha="${OPTARG}"
      ;;
    t)
      github_auth_token_filename="${OPTARG}"
      ;;
    b)
      gce_bucket="${OPTARG}"
      ;;
    p)
      image_dest_project="${OPTARG}"
      ;;
    f)
      image_family_name="${OPTARG}"
      ;;
    \?)
      echo "Invalid option: ${OPTARG}" >&2
      usage
      exit 1
      ;;
    :)
      echo "Invalid option: ${OPTARG} requires an argument" >&2
      usage
      exit 1
      ;;
  esac
done

jq_select=$(echo ".workflow_run.head_sha == \"${commit_sha}\" and .name == \"image_gce_debian11_amd64\"")

artifact=$(curl -L \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer $(cat ${github_auth_token_filename})" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  https://api.github.com/repos/google/android-cuttlefish/actions/artifacts 2> /dev/null \
  | jq ".artifacts[] | select(${jq_select})")

updated_at=$(echo $artifact | jq -r ".updated_at")
date_suffix=$(date -u -d ${updated_at} +"%Y%m%d")
name=cf-debian11-amd64-${date_suffix}-${commit_sha:0:7}

function cleanup {
  rm "image.zip" 2> /dev/null
  rm "image.tar.gz" 2> /dev/null
  gcloud storage rm gs://${gce_bucket}/${name}.tar.gz 2> /dev/null
}

trap cleanup EXIT

download_url=$(echo $artifact | jq -r ".archive_download_url")
curl -L \
  -H "Accept: application/vnd.github+json" \
  -H "Authorization: Bearer $(cat ${github_auth_token_filename})" \
  -H "X-GitHub-Api-Version: 2022-11-28" \
  --output image.zip \
  ${download_url}

unzip image.zip

gcloud storage cp image.tar.gz  gs://${gce_bucket}/${name}.tar.gz

echo "Creating image ..."
gcloud compute images create ${name} \
  --source-uri gs://${gce_bucket}/${name}.tar.gz \
  --project ${image_dest_project} \
  --family ${image_family_name}
