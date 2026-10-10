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
# Starts the IPv6 upstream simulator container and wires it to the network
# namespace of a Cuttlefish host with a veth pair ("transit link").
#
# Usage: sim_up.sh [options] TARGET
#   TARGET   PID of any process in the Cuttlefish host network namespace,
#            a netns file (e.g. /run/netns/cf or /proc/PID/ns/net), or
#            "host" (the initial namespace; requires --allow-host because it
#            changes the real host's IPv6 routes).
# Options:
#   --prefix P::/48       routed prefix of the Cuttlefish host
#                         (default 2001:db8:cf00::/48)
#   --transit T::/64      transit link (default 2001:db8:ffff::/64;
#                         sim = T::1, Cuttlefish host = T::2)
#   --route default|inet  route installed in TARGET: "default" = ::/0 via T::1
#                         (default), "inet" = only the sim service prefix
#   --names "N1 N2 ..."   DNS names answered with the echo address
#   --host-if NAME        TARGET-side veth name (default cf-upstream0)
#   --name NAME           container name (default cf-ipv6-upstream-sim)
#   --image NAME          image tag (default cf-ipv6-upstream-sim)
#   --state DIR           certificate dir (default ~/.cache/cf-ipv6-upstream-sim)
#   --no-build            do not (re)build the image
#   --allow-host          permit TARGET=host
#
# Privileges: needs access to the Docker daemon. The veth pair spans two
# network namespaces, so it is created by a short-lived privileged helper
# container (--privileged --pid=host --network=none). The helper only touches
# the simulator namespace and TARGET; nothing else on the host is changed
# unless TARGET=host.

set -euo pipefail

DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PREFIX=2001:db8:cf00::/48
TRANSIT=2001:db8:ffff::/64
ROUTE=default
NAMES="google-ipv6test.appspot.com ipv6test.googleapis-cn.com echo.sim.test"
HOST_IF=cf-upstream0
NAME=cf-ipv6-upstream-sim
IMAGE=cf-ipv6-upstream-sim
STATE=${XDG_CACHE_HOME:-$HOME/.cache}/cf-ipv6-upstream-sim
BUILD=1
ALLOW_HOST=0
ECHO_ADDR=2001:db8:eeee::80
DNS_ADDR=2001:db8:eeee::53
INET_PREFIX=2001:db8:eeee::/64

die() { echo "sim_up: $*" >&2; exit 1; }

while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) PREFIX=$2; shift 2 ;;
    --transit) TRANSIT=$2; shift 2 ;;
    --route) ROUTE=$2; shift 2 ;;
    --names) NAMES=$2; shift 2 ;;
    --host-if) HOST_IF=$2; shift 2 ;;
    --name) NAME=$2; shift 2 ;;
    --image) IMAGE=$2; shift 2 ;;
    --state) STATE=$2; shift 2 ;;
    --no-build) BUILD=0; shift ;;
    --allow-host) ALLOW_HOST=1; shift ;;
    -h|--help) sed -n '17,45p' "$0"; exit 0 ;;
    -*) die "unknown option $1" ;;
    *) break ;;
  esac
done
[ $# -eq 1 ] || die "usage: sim_up.sh [options] TARGET (see --help)"
TARGET=$1

case "$PREFIX" in */48) ;; *) die "--prefix must be a /48 (got $PREFIX)" ;; esac
case "$TRANSIT" in *::/64) ;; *) die "--transit must be written as X:Y:Z::/64" ;; esac
case "$ROUTE" in default) TGT_ROUTE=default ;; inet) TGT_ROUTE=$INET_PREFIX ;;
  *) die "--route must be default or inet" ;; esac
T=${TRANSIT%::/64}
SIM_T="$T::1"; HOST_T="$T::2"

# Resolve TARGET into a helper mount argument and an in-helper netns file.
HELPER_MOUNT=()
case "$TARGET" in
  host)
    [ "$ALLOW_HOST" = 1 ] || die "TARGET=host changes host IPv6 routes; add --allow-host"
    TGT_NS=/proc/1/ns/net ;;
  ''|*[!0-9]*)
    [ -e "$TARGET" ] || die "netns file $TARGET not found"
    HELPER_MOUNT=(-v "$TARGET:/target_netns:ro")
    TGT_NS=/target_netns ;;
  *)
    [ -d "/proc/$TARGET" ] || die "no process $TARGET"
    TGT_NS=/proc/$TARGET/ns/net ;;
esac

if [ "$BUILD" = 1 ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  echo "sim_up: building image $IMAGE"
  docker build -q -t "$IMAGE" "$DIR" >/dev/null
fi

# Certificates are generated as the calling user so they stay readable.
mkdir -p "$STATE"
docker run --rm --network none --user "$(id -u):$(id -g)" -v "$STATE:/certs" \
  --entrypoint /sim/gen_certs.sh "$IMAGE" /certs "$NAMES" "$ECHO_ADDR"

if docker inspect "$NAME" >/dev/null 2>&1; then
  docker rm -f "$NAME" >/dev/null
fi
docker run -d --name "$NAME" --network none --cap-add NET_ADMIN \
  --sysctl net.ipv6.conf.all.disable_ipv6=0 \
  --sysctl net.ipv6.conf.default.disable_ipv6=0 \
  --sysctl net.ipv6.conf.all.forwarding=1 \
  -v "$STATE:/certs:ro" \
  -e SIM_NAMES="$NAMES" -e SIM_ECHO_ADDR="$ECHO_ADDR" -e SIM_DNS_ADDR="$DNS_ADDR" \
  -e SIM_INET_PREFIX="$INET_PREFIX" \
  "$IMAGE" >/dev/null

for _ in $(seq 50); do
  docker logs "$NAME" 2>&1 | grep -q "entrypoint: ready" && break
  [ "$(docker inspect -f '{{.State.Running}}' "$NAME")" = true ] || break
  sleep 0.2
done
docker logs "$NAME" 2>&1 | grep -q "entrypoint: ready" || {
  docker logs "$NAME" >&2; die "container did not become ready"; }
CPID=$(docker inspect -f '{{.State.Pid}}' "$NAME")

docker run --rm --privileged --pid=host --network none "${HELPER_MOUNT[@]}" \
  -e CPID="$CPID" -e TGT_NS="$TGT_NS" -e HOST_IF="$HOST_IF" \
  -e SIM_T="$SIM_T" -e HOST_T="$HOST_T" -e PREFIX="$PREFIX" -e TGT_ROUTE="$TGT_ROUTE" \
  -e ECHO_ADDR="$ECHO_ADDR" \
  --entrypoint /bin/sh "$IMAGE" -euc '
    mkdir -p /run/netns
    touch /run/netns/sim /run/netns/tgt
    mount --bind /proc/$CPID/ns/net /run/netns/sim
    mount --bind "$TGT_NS" /run/netns/tgt
    if ip -n sim link show transit0 >/dev/null 2>&1; then
      ip -n sim link del transit0
    fi
    if ip -n tgt link show "$HOST_IF" >/dev/null 2>&1; then
      ip -n tgt link del "$HOST_IF"
    fi
    ip -n sim link add transit0 type veth peer name "$HOST_IF" netns tgt
    ip -n sim -6 addr add "$SIM_T/64" dev transit0 nodad
    ip -n tgt -6 addr add "$HOST_T/64" dev "$HOST_IF" nodad
    ip -n sim link set transit0 up
    ip -n tgt link set "$HOST_IF" up
    ip -n sim -6 route replace "$PREFIX" via "$HOST_T" dev transit0
    ip -n tgt -6 route replace $TGT_ROUTE via "$SIM_T" dev "$HOST_IF"
    echo "--- simulator netns"
    ip -n sim -br -6 addr
    ip -n sim -6 route
    echo "--- target netns ($TGT_NS)"
    ip -n tgt -br -6 addr show dev "$HOST_IF"
    ip -n tgt -6 route show dev "$HOST_IF"
    echo "target forwarding=$(ip netns exec tgt cat /proc/sys/net/ipv6/conf/all/forwarding)"
    ip netns exec tgt ping -6 -c1 -W2 "$SIM_T" >/dev/null && echo "ping $SIM_T from target: ok" \
      || echo "ping $SIM_T from target: FAILED"
  '

cat <<EOF
sim_up: ready
  container      $NAME (pid $CPID)
  routed prefix  $PREFIX via $HOST_T (dev transit0 in the simulator)
  target route   $TGT_ROUTE via $SIM_T dev $HOST_IF
  echo server    http://[$ECHO_ADDR]/ip.js?fmt=text  https://<name>/ip.js?fmt=text
  DNS server     $DNS_ADDR  names: $NAMES
  lab CA         $STATE/ca.crt  (Android: $(ls "$STATE"/*.0 2>/dev/null | head -1))
EOF
