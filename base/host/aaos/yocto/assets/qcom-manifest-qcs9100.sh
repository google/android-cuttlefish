#!/bin/sh

# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT
set -e

# https://github.com/qualcomm-linux/qcom-manifest?tab=readme-ov-file#qcom-repo-manifest-readme
mkdir qcs9100-build
cd qcs9100-build
repo init -u https://github.com/quic-yocto/qcom-manifest -b qcom-linux-kirkstone -m qcom-6.6.52-QLI.1.3-Ver.1.1.xml
repo sync -j8
MACHINE=qcs9100-ride-sx DISTRO=qcom-wayland QCOM_SELECTED_BSP=custom source setup-environment
