FROM php:8.4-fpm

# Install PHP extensions required by WordPress and common CMS
RUN docker-php-ext-install \
    mysqli \
    pdo_mysql

# Verify extensions
RUN php -r 'exit(extension_loaded("mysqli") ? 0 : 1);' \
    && php -r 'exit(extension_loaded("pdo_mysql") ? 0 : 1);'
