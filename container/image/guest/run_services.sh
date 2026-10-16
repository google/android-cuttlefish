#!/usr/bin/env bash

if [[ "${USE_GCE_METADATA}" == "true" ]]; then
  echo "build_api_credentials_use_gce_metadata=true" \
    >> /etc/default/cuttlefish-host_orchestrator
fi

service nginx start
service cuttlefish-host-resources start
service cuttlefish-operator start
service cuttlefish-host_orchestrator start

# To keep it running
tail -f /dev/null
