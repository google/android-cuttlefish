#!/bin/bash

cd "$(dirname "$0")"
rm -f /tmp/ivshmem_socket /tmp/ivshmem_socket_client
if [ ! -d .env ]; then
  virtualenv -p python3 .env
fi
source .env/bin/activate
python src/ivserver.py "$@"
