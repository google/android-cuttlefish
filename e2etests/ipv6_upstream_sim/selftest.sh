#!/usr/bin/env bash
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
# Self-test without Cuttlefish. A throwaway rootless network namespace plays
# the Cuttlefish host (IPv6 forwarding on, P:22::2/64 on a dummy interface).
# A second namespace plays a guest behind veth cvd-mtap-01
# (host P:2101::1/64, guest P:2101::2/64, default route via the host), so its
# traffic is forwarded through the fake host. The simulator is attached with
# sim_up.sh, then the test checks that
#   - DNS answers AAAA for the configured names (and an empty A answer),
#   - HTTP and HTTPS (with the lab CA) echo the exact source address (no NAT),
#     for the forwarded guest and for a host-local address,
#   - HTTPS without the lab CA is rejected.
# Requires: docker access, unshare/nsenter (util-linux), curl, dig.

set -uo pipefail

DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
P=2001:db8:cf00
ECHO=2001:db8:eeee::80
DNS=2001:db8:eeee::53
NAME=cf-ipv6-upstream-sim-selftest
STATE=$(mktemp -d)
FAIL=0

pass() { echo "PASS: $*"; }
fail() { echo "FAIL: $*"; FAIL=1; }

unshare --user --map-root-user --net sleep infinity &
H=$!
sleep 0.5
NS=(nsenter -t "$H" -U -n --preserve-credentials)
# Guest netns, owned by the same user namespace, behind the fake host.
nsenter -t "$H" -U --preserve-credentials unshare --net sleep infinity &
G=$!
sleep 0.5
GNS=(nsenter -t "$G" -U -n --preserve-credentials)
cleanup() {
  "$DIR/sim_down.sh" --name "$NAME" >/dev/null 2>&1
  kill "$G" "$H" 2>/dev/null
  rm -rf "$STATE"
}
trap cleanup EXIT

echo "== fake Cuttlefish host netns: holder pid $H ($(readlink /proc/$H/ns/net))"
echo "== fake guest netns:           holder pid $G ($(readlink /proc/$G/ns/net))"
"${NS[@]}" sh -euc "
  ip link set lo up
  echo 1 > /proc/sys/net/ipv6/conf/all/forwarding
  ip link add cvd-fake0 type dummy
  ip link set cvd-fake0 up
  ip -6 addr add $P:22::2/64 dev cvd-fake0 nodad
  ip link add cvd-mtap-01 type veth peer name eth0 netns $G
  ip -6 addr add $P:2101::1/64 dev cvd-mtap-01 nodad
  ip link set cvd-mtap-01 up
  ip -br -6 addr show dev cvd-fake0
  ip -br -6 addr show dev cvd-mtap-01
" || { echo "cannot configure rootless host netns"; exit 1; }
"${GNS[@]}" sh -euc "
  ip link set lo up
  ip -6 addr add $P:2101::2/64 dev eth0 nodad
  ip link set eth0 up
  ip -6 route add default via $P:2101::1 dev eth0
  ip -br -6 addr show dev eth0
" || { echo "cannot configure rootless guest netns"; exit 1; }

echo "== sim_up"
"$DIR/sim_up.sh" --name "$NAME" --state "$STATE" --prefix "$P::/48" "$H" \
  || { echo "sim_up failed"; exit 1; }

echo "== DNS (queried from the forwarded guest netns)"
for n in google-ipv6test.appspot.com echo.sim.test; do
  a=$("${GNS[@]}" dig +short +time=2 +tries=1 "@$DNS" AAAA "$n")
  [ "$a" = "$ECHO" ] && pass "AAAA $n = $a" || fail "AAAA $n = '$a' (want $ECHO)"
done
a4=$("${GNS[@]}" dig +time=2 +tries=1 "@$DNS" A google-ipv6test.appspot.com)
a4s=$(echo "$a4" | sed -n 's/.*status: \([A-Z]*\).*/\1/p')
a4n=$(echo "$a4" | sed -n 's/.*ANSWER: \([0-9]*\).*/\1/p')
[ "$a4s" = NOERROR ] && [ "$a4n" = 0 ] \
  && pass "A google-ipv6test.appspot.com: NOERROR, 0 answers (IPv6-only)" \
  || fail "A google-ipv6test.appspot.com: status=$a4s answers=$a4n"
st=$("${NS[@]}" dig +time=2 +tries=1 "@$DNS" AAAA www.example.com | sed -n 's/.*status: \([A-Z]*\).*/\1/p')
echo "INFO: unconfigured name www.example.com -> status $st"

echo "== HTTP/HTTPS echo"
URL_PATH='/ip.js?fmt=text'
# Case 1: forwarded guest (P:2101::2 behind cvd-mtap-01), default source
# address selection, name resolved through the simulator DNS.
src=$P:2101::2
got=$("${GNS[@]}" curl -gs --max-time 5 "http://[$ECHO]$URL_PATH")
[ "$got" = "$src" ] && pass "HTTP  guest(forwarded) echoed $got" || fail "HTTP  guest echoed '$got' (want $src)"
got=$("${GNS[@]}" curl -gs --max-time 5 --cacert "$STATE/ca.crt" \
  --resolve "google-ipv6test.appspot.com:443:[$("${GNS[@]}" dig +short "@$DNS" AAAA google-ipv6test.appspot.com)]" \
  "https://google-ipv6test.appspot.com$URL_PATH")
[ "$got" = "$src" ] && pass "HTTPS guest(forwarded) echoed $got (lab CA verified)" \
  || fail "HTTPS guest echoed '$got' (want $src)"
# Case 2: address local to the fake host (P:22::2), explicit source.
src=$P:22::2
got=$("${NS[@]}" curl -gs --max-time 5 --interface "$src" "http://[$ECHO]$URL_PATH")
[ "$got" = "$src" ] && pass "HTTP  host-local src $src echoed $got" || fail "HTTP  src $src echoed '$got'"
got=$("${NS[@]}" curl -gs --max-time 5 --interface "$src" --cacert "$STATE/ca.crt" \
  --resolve "google-ipv6test.appspot.com:443:[$ECHO]" "https://google-ipv6test.appspot.com$URL_PATH")
[ "$got" = "$src" ] && pass "HTTPS host-local src $src echoed $got (lab CA verified)" \
  || fail "HTTPS src $src echoed '$got'"
body=$("${GNS[@]}" curl -gs --max-time 5 "http://[$ECHO]$URL_PATH" | od -c | head -3)
echo "INFO: raw body bytes: $(echo "$body" | tr -s ' ' | tr '\n' ' ')"
if "${GNS[@]}" curl -gs --max-time 5 --resolve "google-ipv6test.appspot.com:443:[$ECHO]" \
    "https://google-ipv6test.appspot.com$URL_PATH" >/dev/null 2>&1; then
  fail "HTTPS without lab CA succeeded"
else
  pass "HTTPS without lab CA rejected"
fi

echo "== simulator log (tail)"
docker logs "$NAME" 2>&1 | tail -12

[ "$FAIL" = 0 ] && echo "SELFTEST: ALL PASS" || echo "SELFTEST: FAILURES"
exit "$FAIL"
