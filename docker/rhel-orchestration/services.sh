#!/usr/bin/env bash

[ ! -f "${HOME}/.dockerfile" ] && echo ".dockerfile not present, exiting now." && exit

dnf upgrade && dnf -y install cuttlefish-*

if [ -f /root/cvd_home/bin/cvd ]; then
  /root/cvd_home/bin/cvd start \
    --vhost-user-vsock=true \
    --report_anonymous_usage_stats=y \
    "$@"
fi

# To keep it running
tail -f /dev/null
