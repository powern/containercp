#!/bin/sh
set -e

WEBROOT=/var/www/snappymail
DATA_DIR=${WEBROOT}/data
CONFIG_DIR=${DATA_DIR}/_data_/_default_/configs

# Ensure data directory exists and is writable
mkdir -p "${CONFIG_DIR}"
chown -R nobody:nobody "${DATA_DIR}"

# Generate SnappyMail domain config (IMAP/SMTP settings) if not present
if [ ! -f "${DATA_DIR}/_data_/_default_/domains/default.json" ]; then
    mkdir -p "${DATA_DIR}/_data_/_default_/domains"
    cat > "${DATA_DIR}/_data_/_default_/domains/default.json" << EOF
{
    "IMAP": {
        "host": "containercp-mail-dovecot",
        "port": 143,
        "type": 0,
        "timeout": 300,
        "shortLogin": false,
        "lowerLogin": true,
        "sasl": ["SCRAM-SHA3-512", "SCRAM-SHA-512", "SCRAM-SHA-256", "SCRAM-SHA-1", "PLAIN", "LOGIN"],
        "ssl": {
            "verify_peer": false,
            "verify_peer_name": false,
            "allow_self_signed": false,
            "SNI_enabled": true,
            "disable_compression": true,
            "security_level": 1
        },
        "disabled_capabilities": ["METADATA", "OBJECTID", "PREVIEW", "STATUS=SIZE"],
        "use_expunge_all_on_delete": false,
        "fast_simple_search": true,
        "force_select": false,
        "message_all_headers": false,
        "message_list_limit": 10000,
        "search_filter": ""
    },
    "SMTP": {
        "host": "containercp-mail-postfix",
        "port": 587,
        "type": 0,
        "timeout": 60,
        "shortLogin": false,
        "lowerLogin": true,
        "sasl": ["SCRAM-SHA3-512", "SCRAM-SHA-512", "SCRAM-SHA-256", "SCRAM-SHA-1", "PLAIN", "LOGIN"],
        "ssl": {
            "verify_peer": false,
            "verify_peer_name": false,
            "allow_self_signed": false,
            "SNI_enabled": true,
            "disable_compression": true,
            "security_level": 2
        },
        "useAuth": true,
        "setSender": false,
        "usePhpMail": false
    },
    "Sieve": {
        "host": "localhost",
        "port": 4190,
        "type": 0,
        "timeout": 10,
        "shortLogin": false,
        "lowerLogin": true,
        "sasl": ["SCRAM-SHA3-512", "SCRAM-SHA-512", "SCRAM-SHA-256", "SCRAM-SHA-1", "PLAIN", "LOGIN"],
        "ssl": {
            "verify_peer": false,
            "verify_peer_name": false,
            "allow_self_signed": false,
            "SNI_enabled": true,
            "disable_compression": true,
            "security_level": 1
        },
        "enabled": false
    },
    "whiteList": ""
}
EOF
fi

# Generate SnappyMail config if not present
if [ ! -f "${CONFIG_DIR}/application.ini" ]; then
    cat > "${CONFIG_DIR}/application.ini" << EOF
[webmail]
title = ContainerCP Webmail
language = en
theme = Default
allow_custom_login = Off
login_default_domain = auto

[plugins]
antispam_banner_config = Off
debug_logging = Off

[login]
default_account = Off
suggest_account = Off
sign_me_auto = Off

[db]
type = sqlite
pdo_dsn = sqlite:${DATA_DIR}/_data_/_default_/snappymail.db

[security]
use_encryption = On
admin_login = admin
admin_password = \$2y\$10\$Lrf5U7SJcH8Q5F3F3F3F3F3F3F3F3F3F3F3F3F3F3F3F3F3F3F3F
csrf_protection = On
allow_admin_panel = Off
custom_logo = Off

[logs]
type = file
path = /var/log/nginx/snappymail.log
EOF
fi

# Generate nginx config
cat > /etc/nginx/http.d/default.conf << EOF
server {
    listen 80;
    root ${WEBROOT};
    index index.php index.html;

    location / {
        try_files \$uri \$uri/ /index.php?\$query_string;
    }

    location ~ \.php\$ {
        fastcgi_pass 127.0.0.1:9000;
        fastcgi_index index.php;
        fastcgi_param SCRIPT_FILENAME ${WEBROOT}\$fastcgi_script_name;
        fastcgi_param PATH_INFO \$fastcgi_path_info;
        include fastcgi_params;
    }

    location ~ /\.ht {
        deny all;
    }
}
EOF

# Start PHP-FPM
php-fpm82 -R --nodaemonize --fpm-config /etc/php82/php-fpm.conf &
sleep 1

# Start nginx
nginx -g "daemon off;"
