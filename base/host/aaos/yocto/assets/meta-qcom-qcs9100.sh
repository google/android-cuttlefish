#!/bin/sh

# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT
set -e

python3 -m venv venv
source venv/bin/activate
pip3 install kas

git clone https://github.com/qualcomm-linux/meta-qcom.git -b master

# https://github.com/YoeDistro/yoe-distro/issues/789
# https://github.com/containers/toolbox/issues/849
cp assets/qcs9100-ride-sx-custom.yml meta-qcom/ci/qcs9100-ride-sx.yml

kas build meta-qcom/ci/qcs9100-ride-sx.yml
