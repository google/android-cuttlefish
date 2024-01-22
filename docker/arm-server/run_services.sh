#!/bin/bash

service cuttlefish-host-resources start
service cuttlefish-operator start
#service cuttlefish-host_orchestrator start
/root/cvd_home/bin/cvd start --vhost-user-vsock=true --report_anonymous_usage_stats=y $@
