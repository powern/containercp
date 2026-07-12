#!/bin/bash
set -e

# OpenDKIM needs /var/run/opendkim to exist
mkdir -p /var/run/opendkim
chown opendkim:opendkim /var/run/opendkim

exec "$@"
