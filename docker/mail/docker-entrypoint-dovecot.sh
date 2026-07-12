#!/bin/bash
set -e

# Ensure the mail directory is writable by Dovecot
# Dovecot runs as uid 1000 (dovecot user), gid 1000 (dovecot group)
chown 1000:1000 /var/mail 2>/dev/null || true

exec "$@"
