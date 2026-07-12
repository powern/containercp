#!/bin/bash
set -e

# Create required directories
mkdir -p /run/rspamd /var/lib/rspamd

# Ensure DKIM keys are readable
chmod 644 /etc/rspamd/keys/*/*.private 2>/dev/null || true
chown -R _rspamd:_rspamd /etc/rspamd/keys 2>/dev/null || true

exec /usr/bin/rspamd -f -u _rspamd -g _rspamd
