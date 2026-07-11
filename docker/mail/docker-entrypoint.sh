#!/bin/bash

# Fix DNS resolution — Docker's embedded DNS (127.0.0.11) doesn't
# properly forward MX queries.  Override with Google DNS directly.
# Docker Engine won't overwrite this after container has started.
echo "nameserver 8.8.8.8
nameserver 8.8.4.4
options ndots:0" > /etc/resolv.conf

# Create Postfix log directory
mkdir -p /var/log/postfix

# Copy resolv.conf into Postfix chroot jail (master.cf has chroot=y)
mkdir -p /var/spool/postfix/etc
cp /etc/resolv.conf /var/spool/postfix/etc/resolv.conf

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

# Keep container alive
exec tail -q -f /var/log/postfix/maillog 2>/dev/null
