#!/bin/bash

# Fix DNS resolution — keep Docker DNS (127.0.0.11) for service discovery,
# add Google DNS as fallback for external MX lookups.
# Docker Engine won't overwrite this after container has started.
if grep -q "127.0.0.11" /etc/resolv.conf 2>/dev/null; then
    # Docker DNS is present — add Google DNS after it
    echo "nameserver 8.8.8.8
nameserver 8.8.4.4
options ndots:0" >> /etc/resolv.conf
else
    # No Docker DNS — use Google DNS directly (service discovery won't work)
    echo "nameserver 8.8.8.8
nameserver 8.8.4.4
options ndots:0" > /etc/resolv.conf
fi

# Create Postfix log directory
mkdir -p /var/log/postfix

# Copy resolv.conf into Postfix chroot jail (master.cf has chroot=y)
mkdir -p /var/spool/postfix/etc
cp /etc/resolv.conf /var/spool/postfix/etc/resolv.conf

# DKIM keys mounted to Rspamd container — permissions set by DockerMailProvider

# Clean Postfix state from previous runs
rm -f /var/spool/postfix/pid/master.pid 2>/dev/null || true
rm -rf /var/spool/postfix/public/* 2>/dev/null || true
rm -rf /var/spool/postfix/private/* 2>/dev/null || true

# Ensure /dev/log exists for Postfix syslog
# The full Debian image may have this from systemd-logind or similar
if [ ! -e /dev/log ]; then
    # Create a syslogd to provide /dev/log
    if command -v syslogd &>/dev/null; then
        syslogd
    elif command -v rsyslogd &>/dev/null; then
        rsyslogd
    fi
    sleep 1
fi

# Create Postfix log directory
mkdir -p /var/log/postfix

# Initialize aliases database
newaliases 2>/dev/null || true

# Start Postfix
postfix start 2>&1

# Wait for Postfix to be ready
for i in 1 2 3 4 5; do
    if [ -e /var/spool/postfix/pid/master.pid ]; then
        break
    fi
    sleep 1
done

# Keep container alive — use -F to follow by name (waits for file to appear)
exec tail -q -F /var/log/postfix/maillog
