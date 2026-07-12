#!/bin/sh
set -e

WEBROOT=/var/www/snappymail
DATA_DIR=${WEBROOT}/data
CONFIG_DIR=${DATA_DIR}/_data_/_default_/configs

# Ensure data directory exists and is writable
mkdir -p "${CONFIG_DIR}"
chown -R nobody:nobody "${DATA_DIR}"

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
