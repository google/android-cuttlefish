#!/bin/bash

uapi_headers=("videodev2.h" "v4l2-controls.h" "v4l2-common.h")

for uapi_header in "${uapi_headers[@]}"; do
  wget -O "include/linux/$uapi_headers" "https://raw.githubusercontent.com/torvalds/linux/refs/heads/master/include/uapi/linux/$uapi_headers"
done

wget -O "include/sys/time.h" "https://raw.githubusercontent.com/torvalds/linux/refs/heads/master/usr/dummy-include/sys/time.h"

latest_kernel_version=$(curl -s https://api.github.com/repos/torvalds/linux/tags | jq -r '.[0].name')
echo "$latest_kernel_version" >kernel_headers_version.txt
