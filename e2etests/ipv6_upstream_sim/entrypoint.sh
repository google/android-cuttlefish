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
# Container entrypoint for the IPv6 upstream simulator.
#
# Brings up the "internet side" (dummy interface inet0 holding the echo and
# DNS service addresses), generates certificates if missing, writes the
# dnsmasq configuration and runs dnsmasq plus the echo server. The transit
# link (transit0) and the route to the routed /48 are added from outside by
# sim_up.sh, because the peer end lives in another network namespace.

set -eu

: "${SIM_ECHO_ADDR:=2001:db8:eeee::80}"
: "${SIM_DNS_ADDR:=2001:db8:eeee::53}"
: "${SIM_INET_PREFIX:=2001:db8:eeee::/64}"
: "${SIM_NAMES:=google-ipv6test.appspot.com ipv6test.googleapis-cn.com echo.sim.test}"
# Extra records, space separated NAME=IPV6 pairs.
: "${SIM_DNS_RECORDS:=}"
: "${SIM_CERT_DIR:=/certs}"

log() { echo "entrypoint: $*" >&2; }

if [ "$(cat /proc/sys/net/ipv6/conf/all/forwarding)" != 1 ]; then
  log "WARNING: net.ipv6.conf.all.forwarding=0; start with --sysctl net.ipv6.conf.all.forwarding=1"
fi

ip link set lo up
if ! ip link show inet0 >/dev/null 2>&1; then
  ip link add inet0 type dummy
fi
ip link set inet0 up
ip -6 addr replace "$SIM_ECHO_ADDR/128" dev inet0 nodad
ip -6 addr replace "$SIM_DNS_ADDR/128" dev inet0 nodad
ip -6 route replace "$SIM_INET_PREFIX" dev inet0

if [ ! -s "$SIM_CERT_DIR/server.crt" ]; then
  /sim/gen_certs.sh "$SIM_CERT_DIR" "$SIM_NAMES" "$SIM_ECHO_ADDR"
fi

conf=/run/dnsmasq-sim.conf
{
  echo "no-resolv"
  echo "no-hosts"
  echo "bind-dynamic"
  echo "listen-address=$SIM_DNS_ADDR"
  echo "log-queries"
  echo "log-facility=-"
  # local=/NAME/ makes dnsmasq authoritative for NAME, so other record types
  # (A) get an empty NOERROR answer instead of REFUSED.
  for n in $SIM_NAMES; do
    echo "local=/$n/"
    echo "address=/$n/$SIM_ECHO_ADDR"
  done
  for r in $SIM_DNS_RECORDS; do
    echo "local=/${r%%=*}/"
    echo "address=/${r%%=*}/${r#*=}"
  done
} > "$conf"
log "dnsmasq config:"; sed 's/^/  /' "$conf" >&2

dnsmasq --keep-in-foreground --conf-file="$conf" &
dns_pid=$!
python3 /sim/echo_server.py --cert "$SIM_CERT_DIR/server_chain.crt" \
  --key "$SIM_CERT_DIR/server.key" &
echo_pid=$!

trap 'kill $dns_pid $echo_pid 2>/dev/null; exit 0' TERM INT
for _ in $(seq 50); do
  l=$(netstat -ltn 2>/dev/null)
  echo "$l" | grep -q ':53 ' && echo "$l" | grep -q ':80 ' && echo "$l" | grep -q ':443 ' && break
  sleep 0.2
done
log "ready echo=$SIM_ECHO_ADDR dns=$SIM_DNS_ADDR names=[$SIM_NAMES]"
# Exit (and let Docker report it) if either service dies.
while kill -0 $dns_pid 2>/dev/null && kill -0 $echo_pid 2>/dev/null; do
  sleep 2
done
log "a service exited; stopping"
for p in $dns_pid $echo_pid; do
  if kill -0 "$p" 2>/dev/null; then
    kill "$p" 2>/dev/null
  fi
done
exit 1
