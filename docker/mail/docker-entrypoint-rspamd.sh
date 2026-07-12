#!/bin/bash
set -e

# Create required directories and fix ownership
mkdir -p /run/rspamd /var/lib/rspamd
chown _rspamd:_rspamd /run/rspamd /var/lib/rspamd

# Copy default config if not present (first run)
if [ ! -f /etc/rspamd/rspamd.conf ]; then
    cp -a /usr/share/rspamd/* /etc/rspamd/ 2>/dev/null || true
fi

exec /usr/bin/rspamd -f -u _rspamd -g _rspamd
