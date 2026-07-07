# ContainerCP - Agent Rules

## Project Goal

ContainerCP is a container-oriented hosting control panel.

The first MVP must be small and safe:
- CLI first
- no web UI yet
- no mail server yet
- no DNS server yet
- no Kubernetes yet
- no multi-server support yet

## Architecture Rule

Do not create one huge application.

Use small modules:
- core
- config
- database
- users
- sites
- docker
- proxy
- logging

## Safety Rules

Never run destructive commands without explicit confirmation.

Forbidden without confirmation:
- rm -rf
- docker system prune
- docker volume rm
- deleting /srv/containercp
- deleting /etc/containercp
- changing firewall rules
- changing SSH config
- changing network config

## Development Rules

Every change must be committed to Git.

Use small iterations.

One task = one logical change.

Prefer simple working code over complex architecture.

## MVP v0.1

The first working version must support:

1. Create local ContainerCP user.
2. Create site record.
3. Generate docker-compose.yml for a PHP site.
4. Start site stack.
5. Stop site stack.
6. Show site status.
