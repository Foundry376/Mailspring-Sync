#!/bin/bash
# Generates a throwaway root -> intermediate -> leaf (SAN: DNS mailspring-test.invalid, IP 127.0.0.1)
# for the Disallowed-store test. No CRL/OCSP URLs, like many private CAs and AV proxies.
set -e; out="$1"; mkdir -p "$out"; cd "$out"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout root.key -out root.crt -days 2 \
  -subj "/CN=Mailspring Test Root" -addext "basicConstraints=critical,CA:TRUE" -addext "keyUsage=critical,keyCertSign,cRLSign"
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout int.key -out int.csr -subj "/CN=Mailspring Test Intermediate"
printf "basicConstraints=critical,CA:TRUE,pathlen:0\nkeyUsage=critical,keyCertSign,cRLSign\nextendedKeyUsage=serverAuth\n" > int.ext
openssl x509 -req -in int.csr -CA root.crt -CAkey root.key -CAcreateserial -out int.crt -days 2 -extfile int.ext
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout leaf.key -out leaf.csr -subj "/CN=mailspring-test.invalid"
printf "basicConstraints=CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:mailspring-test.invalid,IP:127.0.0.1\n" > leaf.ext
openssl x509 -req -in leaf.csr -CA int.crt -CAkey int.key -CAcreateserial -out leaf.crt -days 2 -extfile leaf.ext
cat leaf.crt int.crt > chain.pem
