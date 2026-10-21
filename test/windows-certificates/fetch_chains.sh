#!/bin/bash
# Usage: fetch_chains.sh <outdir>  — saves the chain each server sends, as PEM.
out="$1"; mkdir -p "$out"
fetch() { # host port [starttls-proto]
  local extra=""; [ -n "$3" ] && extra="-starttls $3"
  echo | openssl s_client -connect "$1:$2" -servername "$1" -showcerts $extra 2>/dev/null \
    | awk '/BEGIN CERT/{p=1} p{print} /END CERT/{p=0}' > "$out/$1_$2.pem"
  echo "$1:$2 $(grep -c BEGIN "$out/$1_$2.pem") certs"
}
fetch imap.gmail.com 993; fetch smtp.gmail.com 465; fetch outlook.office365.com 993
fetch imap.mail.me.com 993; fetch imap.fastmail.com 993; fetch smtp.office365.com 587 smtp
fetch imap.mail.yahoo.com 993; fetch id.getmailspring.com 443
for h in expired wrong.host self-signed untrusted-root revoked sha1-intermediate; do fetch $h.badssl.com 443; done
for h in dv-g3b ov-g3b ev-g3b dv-g3c ov-g3c ev-g3c; do fetch $h-emudhra.we-have-a-problem.com 443; fetch $h-aaa-emudhra.we-have-a-problem.com 443; done
