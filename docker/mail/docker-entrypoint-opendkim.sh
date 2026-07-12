#!/bin/bash
set -e

# Fix DKIM private key permissions — keys are mounted from host (root:root 600)
# OpenDKIM runs as opendkim user and needs read access
chmod 644 /etc/opendkim/keys/*/*.private 2>/dev/null || true

# Create PID directory
mkdir -p /var/run/opendkim
chown opendkim:opendkim /var/run/opendkim

exec "$@"
