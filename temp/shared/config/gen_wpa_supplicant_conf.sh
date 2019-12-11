#!/bin/bash
#
# Generates wpa_supplicant.conf file for wifi
# Usage: generate_wpa_supplicant_conf.sh <device name> <model name> <SDK API level>

if [ -n "$3" -a "$3" -lt "21" ]
then
  # before mnc.
  cat <<eof
ctrl_interface=wlan0
eof
fi

cat <<eof
update_config=1
device_name=$1
model_name=$2
serial_number=
device_type=10-0050F204-5
eof
