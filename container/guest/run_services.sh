#!/usr/bin/env bash

service nginx start
service cuttlefish-host-resources start
service cuttlefish-operator start
service cuttlefish-host_orchestrator start

# To keep it running
tail -f /dev/null
