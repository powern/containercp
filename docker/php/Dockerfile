ARG PHP_VERSION=8.4
FROM php:${PHP_VERSION}-fpm

ARG PHP_VERSION

LABEL org.opencontainers.image.title="ContainerCP PHP ${PHP_VERSION} Runtime"
LABEL org.opencontainers.image.name="ghcr.io/powern/containercp-php"
LABEL org.opencontainers.image.description="PHP-FPM image with MySQL extensions for ContainerCP WordPress/PHP sites"
LABEL org.opencontainers.image.source="https://github.com/powern/containercp"
LABEL org.opencontainers.image.version="${PHP_VERSION}"

# Install msmtp for outbound mail delivery via SMTP
RUN apt-get update && apt-get install -y --no-install-recommends \
    msmtp msmtp-mta ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Install PHP extensions required by WordPress and common CMS
RUN docker-php-ext-install \
    mysqli \
    pdo_mysql

# Copy PHP configuration
COPY php.ini /usr/local/etc/php/conf.d/containercp.ini

# Verify extensions
RUN php -r "exit(extension_loaded('mysqli') ? 0 : 1);" \
    && php -r "exit(extension_loaded('pdo_mysql') ? 0 : 1);" \
    && echo "ContainerCP PHP ${PHP_VERSION}: mysqli+pdo_mysql OK"
