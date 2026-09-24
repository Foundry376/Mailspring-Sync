#!/bin/sh
# Creates the SASL users from $CYRUS_USERS ("user:password user:password"),
# applies $CYRUS_EXTRA_CONF ("option: value" lines) to imapd.conf, then runs the Cyrus
# master in the foreground.
set -e
for pair in ${CYRUS_USERS:-cyrus:admin test:pass}; do
  printf '%s' "${pair#*:}" | saslpasswd2 -p -c -u "" "${pair%%:*}" 2>/dev/null || \
    printf '%s' "${pair#*:}" | saslpasswd2 -p -c "${pair%%:*}"
done
chown cyrus:mail /etc/sasldb2
# Cyrus refuses to start when an option appears twice, so an override replaces the line.
if [ -n "$CYRUS_EXTRA_CONF" ]; then
  printf '%s\n' "$CYRUS_EXTRA_CONF" | while IFS= read -r line; do
    key="${line%%:*}"
    sed -i "/^$key:/d" /etc/imapd.conf
    printf '%s\n' "$line" >> /etc/imapd.conf
  done
fi
mkdir -p /run/cyrus/proc /run/cyrus/lock /run/cyrus/socket /var/lib/cyrus/log
chown -R cyrus:mail /run/cyrus /var/lib/cyrus /var/spool/cyrus
exec /usr/lib/cyrus/bin/master -M /etc/cyrus.conf -C /etc/imapd.conf
