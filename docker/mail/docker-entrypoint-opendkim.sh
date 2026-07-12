#!/bin/bash
set -e

# Fix DKIM private key permissions
chmod 644 /etc/opendkim/keys/*/*.private 2>/dev/null || true

# Create PID directory
mkdir -p /var/run/opendkim
chown opendkim:opendkim /var/run/opendkim

# Run OpenDKIM — no syslog dependency (logs to stderr)
exec /usr/sbin/opendkim -f -x /etc/opendkim/opendkim.conf
