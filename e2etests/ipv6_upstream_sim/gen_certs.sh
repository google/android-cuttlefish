#!/bin/sh
#
# Copyright (C) 2026 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Generates a lab CA and a server certificate for the echo server.
#
# Usage: gen_certs.sh OUT_DIR "name1 name2 ..." "ip1 ip2 ..."
#
# Output (OUT_DIR):
#   ca.key ca.crt            lab CA (keep ca.key private; never reuse outside the lab)
#   server.key server.crt    echo server key and certificate (SAN = names + IPs)
#   server_chain.crt         server.crt + ca.crt
#   <hash>.0                 ca.crt named for Android's system trust store
#                            (/system/etc/security/cacerts/<hash>.0), where
#                            <hash> = `openssl x509 -subject_hash_old`.
# Existing files are kept, so re-running is idempotent.

set -eu

OUT=${1:?usage: gen_certs.sh OUT_DIR NAMES [IPS]}
NAMES=${2:-google-ipv6test.appspot.com}
IPS=${3:-}
DAYS=${SIM_CERT_DAYS:-397}

mkdir -p "$OUT"
cd "$OUT"

if [ ! -s ca.crt ] || [ ! -s ca.key ]; then
  openssl req -x509 -newkey rsa:2048 -nodes -sha256 -days 3650 \
    -keyout ca.key -out ca.crt \
    -subj "/O=Cuttlefish IPv6 upstream simulator (LAB ONLY)/CN=cf-ipv6-sim lab CA" \
    -addext "basicConstraints=critical,CA:TRUE" \
    -addext "keyUsage=critical,keyCertSign,cRLSign" 2>/dev/null
  chmod 600 ca.key
fi

if [ ! -s server.crt ] || [ ! -s server.key ]; then
  san=""
  for n in $NAMES; do san="${san:+$san,}DNS:$n"; done
  for i in $IPS; do san="${san:+$san,}IP:$i"; done
  first=$(echo "$NAMES" | awk '{print $1}')
  cat > server.ext <<EOF
basicConstraints=critical,CA:FALSE
keyUsage=critical,digitalSignature,keyEncipherment
extendedKeyUsage=serverAuth
subjectAltName=$san
EOF
  openssl req -newkey rsa:2048 -nodes -sha256 -keyout server.key -out server.csr \
    -subj "/O=Cuttlefish IPv6 upstream simulator (LAB ONLY)/CN=$first" 2>/dev/null
  openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key -CAcreateserial \
    -days "$DAYS" -sha256 -extfile server.ext -out server.crt 2>/dev/null
  rm -f server.csr server.ext
  chmod 600 server.key
fi

cat server.crt ca.crt > server_chain.crt
h=$(openssl x509 -in ca.crt -noout -subject_hash_old)
# Android trust-store format: PEM followed by the text dump.
{ cat ca.crt; openssl x509 -in ca.crt -noout -text -fingerprint; } > "$h.0"
echo "gen_certs: CA=$OUT/ca.crt android=$OUT/$h.0 SAN=$(openssl x509 -in server.crt -noout -ext subjectAltName | tail -1 | sed 's/^ *//')"
