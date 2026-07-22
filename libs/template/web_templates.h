#ifndef CONTAINERCP_TEMPLATE_WEB_TEMPLATES_H
#define CONTAINERCP_TEMPLATE_WEB_TEMPLATES_H

#include <string>
#include <unordered_map>

namespace containercp::template_engine {

inline std::unordered_map<std::string, std::string> default_web_templates() {
    return {
        {"nginx-php-default", R"~(server {
    listen 80;
    server_name {{DOMAIN}};
    root {{PUBLIC_ROOT}};
    index index.php index.html;

    set_real_ip_from 172.31.0.0/16;
    real_ip_header X-Forwarded-For;

    location / {
        try_files $uri $uri/ /index.php?$query_string;
    }

    location ~ \.php$ {
        fastcgi_pass {{PHP_UPSTREAM}};
        fastcgi_index index.php;
        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
        include fastcgi_params;
    }

    location ~ /\.ht {
        deny all;
    }
})~"},
        {"nginx-wordpress", R"~(server {
    listen 80;
    server_name {{DOMAIN}};
    root {{PUBLIC_ROOT}};
    index index.php index.html;

    set_real_ip_from 172.31.0.0/16;
    real_ip_header X-Forwarded-For;

    location / {
        try_files $uri $uri/ /index.php?$args;
    }

    location ~ \.php$ {
        fastcgi_pass {{PHP_UPSTREAM}};
        fastcgi_index index.php;
        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
        include fastcgi_params;
    }

    location ~ /\.ht {
        deny all;
    }

    location = /favicon.ico {
        log_not_found off;
        access_log off;
    }

    location = /robots.txt {
        allow all;
        log_not_found off;
        access_log off;
    }

    location ~ /\. {
        deny all;
    }
})~"},
        {"nginx-laravel", R"~(server {
    listen 80;
    server_name {{DOMAIN}};
    root {{PUBLIC_ROOT}};
    index index.php index.html;

    set_real_ip_from 172.31.0.0/16;
    real_ip_header X-Forwarded-For;

    location / {
        try_files $uri $uri/ /index.php?$query_string;
    }

    location ~ \.php$ {
        fastcgi_pass {{PHP_UPSTREAM}};
        fastcgi_index index.php;
        fastcgi_param SCRIPT_FILENAME $document_root$fastcgi_script_name;
        include fastcgi_params;
    }

    location ~ /\.ht {
        deny all;
    }

    location ~ /\. {
        deny all;
    }
})~"},
        {"apache-php-default", R"~(<VirtualHost *:80>
    ServerName {{DOMAIN}}
    DocumentRoot {{PUBLIC_ROOT}}
    DirectoryIndex index.php index.html

    RemoteIPHeader X-Forwarded-For
    RemoteIPInternalProxy 172.31.0.0/16

    <Directory {{PUBLIC_ROOT}}>
        Options FollowSymLinks
        AllowOverride All
        Require all granted
    </Directory>

    <FilesMatch \.php$>
        SetHandler "proxy:fcgi://{{PHP_UPSTREAM}}"
    </FilesMatch>

    ErrorLog {{LOG_ROOT}}/error.log
    CustomLog {{LOG_ROOT}}/access.log combined
</VirtualHost>)~"},
        {"apache-wordpress", R"~(<VirtualHost *:80>
    ServerName {{DOMAIN}}
    DocumentRoot {{PUBLIC_ROOT}}
    DirectoryIndex index.php index.html

    RemoteIPHeader X-Forwarded-For
    RemoteIPInternalProxy 172.31.0.0/16

    <Directory {{PUBLIC_ROOT}}>
        Options FollowSymLinks
        AllowOverride All
        Require all granted
    </Directory>

    <FilesMatch \.php$>
        SetHandler "proxy:fcgi://{{PHP_UPSTREAM}}"
    </FilesMatch>

    ErrorLog {{LOG_ROOT}}/error.log
    CustomLog {{LOG_ROOT}}/access.log combined

    <IfModule mod_rewrite.c>
        RewriteEngine On
        RewriteRule .* - [E=HTTP_AUTHORIZATION:%{HTTP:Authorization}]
    </IfModule>
</VirtualHost>)~"},
    };
}

} // namespace containercp::template_engine

#endif // CONTAINERCP_TEMPLATE_WEB_TEMPLATES_H
