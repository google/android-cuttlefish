#!/bin/sh

# Copyright 2019 Google Inc. All rights reserved.

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# As explained in
#  https://gist.github.com/darrenjs/4645f115d10aa4b5cebf57483ec82eca

openssl genrsa -des3 -passout pass:xxxx -out server.pass.key 2048
openssl rsa -passin pass:xxxx -in server.pass.key -out server.key
rm -f server.pass.key

openssl req \
    -subj "/C=US/ST=California/L=Santa Clara/O=Beyond Aggravated/CN=localhost" \
    -new -key server.key -out server.csr

openssl x509 -req -sha256 -days 99999 -in server.csr -signkey server.key -out server.crt
rm -f server.csr

# Now create the list of certificates we trust as a client.

rm trusted.pem

# For now we just trust our own server.
openssl x509 -in server.crt -text >> trusted.pem

# Also add the system standard CA cert chain.
# cat /opt/local/etc/openssl/cert.pem >> trusted.pem

# Convert .pem to .der
# openssl x509 -outform der -in trusted.pem -out trusted.der

# Convert .crt and .key to .p12 for use by Security.framework
# Enter password "foo"!
openssl pkcs12 -export -inkey server.key -in server.crt -name localhost -out server.p12
