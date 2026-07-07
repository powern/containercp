# ContainerCP

ContainerCP is an experimental container-oriented hosting control panel.

The project starts with a CLI-only MVP.

## Current status

Early development.

## MVP v0.1

The first version will manage simple PHP sites as isolated Docker Compose stacks.

Planned CLI commands:

- containercp user create <username>
- containercp site create <username> <domain>
- containercp site start <domain>
- containercp site stop <domain>
- containercp site status <domain>

## Paths

- /opt/containercp - source code
- /etc/containercp - configuration
- /srv/containercp - site data and generated stacks
- /var/log/containercp - logs

## Development rule

Small iterations only.

Every logical change must be committed to Git.
