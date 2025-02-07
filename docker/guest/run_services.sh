#!/usr/bin/env bash

apt update
apt --only-upgrade -y --no-install-recommends install \
  cuttlefish-base \
  cuttlefish-user \
  cuttlefish-orchestration

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
