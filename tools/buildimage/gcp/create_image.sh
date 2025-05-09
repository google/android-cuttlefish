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

# Creates a gcp image given a raw image

set -o errexit -o nounset -o pipefail

function print_usage() {
  >&2 echo "usage: $0 -i /path/to/image -p project-id -n name -f family"
}

IMAGE_SRC=
PROJECT_ID=
IMAGE_NAME=
IMAGE_FAMILY=

while getopts "hi:p:n:f:" opt; do
  case "${opt}" in
    h)
      usage
      exit
      ;;
    i)
      IMAGE_SRC="${OPTARG}"
      ;;
    p)
      PROJECT_ID="${OPTARG}"
      ;;
    n)
      IMAGE_NAME="${OPTARG}"
      ;;
    f)
      IMAGE_FAMILY="${OPTARG}"
      ;;
    *)
      echo "Invalid option: -${OPTARG}"
      print_usage
      exit 1
      ;;
  esac
done

readonly IMAGE_SRC
readonly PROJECT_ID
readonly IMAGE_NAME
readonly IMAGE_FAMILY

if [[ ${IMAGE_SRC} == "" ]] then
  >&2 echo "image source is required"
  print_usage
  exit 1
fi

if [[ ${PROJECT_ID} == "" ]] then
  >&2 echo "project id is required"
  print_usage
  exit 1
fi

if [[ ${IMAGE_NAME} == "" ]] then
  >&2 echo "image name is required"
  print_usage
  exit 1
fi

if [[ ${IMAGE_FAMILY} == "" ]] then
  >&2 echo "image family is required"
  print_usage
  exit 1
fi

readonly BUCKET_NAME="${PROJECT_ID}-cuttlefish-image-upload"

function cleanup {
  gcloud storage rm --recursive gs://${BUCKET_NAME}
}

trap cleanup EXIT

gcloud services enable compute.googleapis.com --project="${PROJECT_ID}"

gcloud storage buckets create gs://${BUCKET_NAME} --location="us-east1" --project="${PROJECT_ID}"
gcloud storage cp ${IMAGE_SRC}  gs://${BUCKET_NAME}/${IMAGE_NAME}.tar.gz

echo "Creating image ..."
gcloud compute images create ${IMAGE_NAME} \
  --source-uri gs://${BUCKET_NAME}/${IMAGE_NAME}.tar.gz \
  --project ${PROJECT_ID} \
  --family ${IMAGE_FAMILY}
