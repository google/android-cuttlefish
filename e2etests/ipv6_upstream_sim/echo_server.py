#!/usr/bin/env python3
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
"""IP echo server: replies to any GET with the client source address.

The body is the bare address with no trailing newline, which matches the
`/ip.js?fmt=text` format that CTS `ConnectivityManagerTest#testOpenConnection`
parses with `InetAddresses.parseNumericAddress`. Listens on HTTP and HTTPS
(dual-stack `::`). IPv4-mapped addresses (`::ffff:a.b.c.d`) are reported as
plain IPv4.
"""

import argparse
import http.server
import socket
import socketserver
import ssl
import sys
import threading


class Handler(http.server.BaseHTTPRequestHandler):
  server_version = "ipv6-upstream-sim-echo/1"

  def _client_ip(self):
    ip = self.client_address[0]
    if ip.startswith("::ffff:") and "." in ip:
      ip = ip[len("::ffff:"):]
    return ip.split("%", 1)[0]

  def _reply(self, send_body):
    body = self._client_ip().encode()
    self.send_response(200)
    self.send_header("Content-Type", "text/plain")
    self.send_header("Content-Length", str(len(body)))
    self.send_header("Cache-Control", "no-cache")
    self.send_header("Connection", "close")
    self.end_headers()
    if send_body:
      self.wfile.write(body)

  def do_GET(self):
    self._reply(True)

  def do_HEAD(self):
    self._reply(False)

  def log_message(self, fmt, *args):
    sys.stderr.write("echo %s %s\n" % (self._client_ip(), fmt % args))
    sys.stderr.flush()


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
  address_family = socket.AF_INET6
  daemon_threads = True
  allow_reuse_address = True

  def server_bind(self):
    # Accept IPv4 too (IPv4-mapped), in case the sim is ever dual-stack.
    self.socket.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    # HTTPServer.server_bind() calls socket.getfqdn(), which blocks when no
    # resolver is reachable (the container has --network none). Skip it.
    socketserver.TCPServer.server_bind(self)
    self.server_name = "ipv6-upstream-sim"
    self.server_port = self.server_address[1]


class TlsServer(Server):
  """HTTPS server; the TLS handshake runs in the per-connection thread."""

  ssl_context = None

  def finish_request(self, request, client_address):
    try:
      request.settimeout(10)
      request = self.ssl_context.wrap_socket(request, server_side=True)
    except (ssl.SSLError, OSError) as e:
      sys.stderr.write("echo %s TLS handshake failed: %s\n" %
                       (client_address[0], e))
      return
    super().finish_request(request, client_address)


def main():
  p = argparse.ArgumentParser()
  p.add_argument("--bind", default="::")
  p.add_argument("--http-port", type=int, default=80)
  p.add_argument("--https-port", type=int, default=443)
  p.add_argument("--cert", help="server certificate chain (PEM)")
  p.add_argument("--key", help="server private key (PEM)")
  a = p.parse_args()

  servers = [Server((a.bind, a.http_port), Handler)]
  if a.cert and a.key:
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(a.cert, a.key)
    TlsServer.ssl_context = ctx
    servers.append(TlsServer((a.bind, a.https_port), Handler))

  for s in servers[1:]:
    threading.Thread(target=s.serve_forever, daemon=True).start()
  print("echo: listening http=%d https=%s" %
        (a.http_port, a.https_port if len(servers) > 1 else "off"),
        file=sys.stderr, flush=True)
  servers[0].serve_forever()


if __name__ == "__main__":
  main()
