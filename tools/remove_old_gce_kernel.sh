#!/bin/bash

set -x
set -o errexit

sudo apt --purge -y remove linux-image-5.10.0-10-cloud-amd64
sudo update-grub2
