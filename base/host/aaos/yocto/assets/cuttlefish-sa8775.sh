#!/bin/sh

# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT

# https://github.com/qualcomm-linux/qcom-manifest?tab=readme-ov-file#qcom-repo-manifest-readme
mkdir qcs9100-build
cd qcs9100-build
repo init -u https://github.com/qualcomm-linux/qcom-manifest -b qcom-linux-scarthgap -m qcom-6.6.58-QLI.1.3.1-Ver.1.0.xml
cp ~/assets/cuttlefish-manifest.xml .repo/manifests 
repo sync -j8 -m cuttlefish-manifest.xml
cp -r ~/assets/meta-cuttlefish layers/meta-cuttlefish
RUST_VERSION=1.81 EXTRALAYERS="meta-rust meta-cuttlefish" MACHINE=qcs9100-ride-sx DISTRO=qcom-wayland QCOM_SELECTED_BSP=base source setup-environment
cp ~/assets/cuttlefish-sa8775.conf conf/local.conf
# Temporarily blocked here
bitbake -C compile crosvm
#bitbake cuttlefish-qcom-image
