#!/bin/sh

# As explained in
#  https://gist.github.com/darrenjs/4645f115d10aa4b5cebf57483ec82eca

openssl genrsa -des3 -passout pass:x -out server.pass.key 2048
openssl rsa -passin pass:x -in server.pass.key -out server.key
rm -f server.pass.key

openssl req \
    -subj "/C=US/ST=California/L=Santa Clara/O=Beyond Aggravated/CN=localhost" \
    -new -key server.key -out server.csr

openssl x509 -req -sha256 -days 365 -in server.csr -signkey server.key -out server.crt
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
