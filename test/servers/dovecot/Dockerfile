# Dovecot 2.3.21 for the mailsync integration harness. Built from Alpine's package because
# the official dovecot/dovecot image is amd64-only and its services die under emulation
# on Apple Silicon. Configuration is copied in at run time by harness/servers/dovecot.py.
FROM alpine:3.19
RUN apk add --no-cache dovecot openssl && adduser -D -u 1000 vmail && mkdir -p /srv/mail /tmp/dovecot
CMD ["dovecot", "-c", "/tmp/harness.conf", "-F"]
