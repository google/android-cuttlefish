#!/usr/bin/env bash

AUTO_UPDATE_CF_DEBIAN_PACKAGES=${AUTO_UPDATE_CF_DEBIAN_PACKAGES:-false}

if [ "$AUTO_UPDATE_CF_DEBIAN_PACKAGES" = "true" ]; then
  apt update
  apt --only-upgrade -y --no-install-recommends install \
    cuttlefish-base \
    cuttlefish-user \
    cuttlefish-orchestration
fi

service nginx start
service cuttlefish-host-resources start
service cuttlefish-operator start
service cuttlefish-host_orchestrator start

if [ -f /root/cvd_home/bin/cvd ]; then
  /root/cvd_home/bin/cvd start \
    --vhost-user-vsock=true \
    --report_anonymous_usage_stats=y \
    $@
fi
# To keep it running
tail -f /dev/null
