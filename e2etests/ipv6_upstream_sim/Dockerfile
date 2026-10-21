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
# IPv6 upstream simulator for Cuttlefish routed-mode testing.
# See README.md. Build: docker build -t cf-ipv6-upstream-sim .

FROM alpine:3.23

RUN apk add --no-cache dnsmasq python3 iproute2 openssl util-linux-misc \
      curl bind-tools

COPY entrypoint.sh gen_certs.sh echo_server.py /sim/
RUN chmod 755 /sim/entrypoint.sh /sim/gen_certs.sh /sim/echo_server.py

VOLUME /certs
EXPOSE 53/udp 53/tcp 80/tcp 443/tcp
ENTRYPOINT ["/sim/entrypoint.sh"]
