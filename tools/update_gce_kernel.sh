#!/bin/bash

set -x
set -o errexit

sudo apt install -t bullseye-backports -y linux-image-cloud-amd64
sudo reboot
