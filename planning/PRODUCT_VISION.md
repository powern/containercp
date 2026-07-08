# ContainerCP Product Vision

## What is ContainerCP?

ContainerCP is a modern, open-source hosting control panel built
from the ground up around containers.

Unlike traditional control panels that manage system packages and
processes, ContainerCP treats every website as an isolated container
stack. This makes it inherently more secure, portable, and predictable
than panel software that configures Apache, PHP, and MySQL directly
on the host operating system.

## Target audience

ContainerCP is designed for:

- **System administrators** who manage multiple websites and want
  a modern, clean interface

- **Developers** who need fast, reproducible hosting environments
  for testing and staging

- **Hosting providers** who want to offer container-based hosting
  without managing Kubernetes

- **Small businesses** that need a reliable, low-maintenance hosting
  platform

- **VPS owners** who want a self-hosted alternative to SaaS control
  panels

## Problems ContainerCP solves

| Problem | Solution |
|---------|----------|
| Traditional panels are tied to system packages | ContainerCP isolates every site in its own Docker Compose stack |
| Upgrading PHP breaks existing sites | Each site selects its own PHP version independently |
| SSL certificates require manual renewal | Automatic Let's Encrypt provisioning (future) |
| Backups are complex shell scripts | Built-in backup/restore with tar+gzip |
| Web servers are hardcoded | Template profiles support nginx, Apache, and custom configs |
| Panel software is monolithic | Modular resource-based architecture with clean API |
| Configuration is fragile | Disk-based templates editable without recompilation |

## Why ContainerCP instead of alternatives?

| Feature | ContainerCP | VestaCP | HestiaCP | Plesk | CloudPanel |
|---------|------------|---------|----------|-------|------------|
| Container-native | Yes | No | No | Partial | No |
| API-first | Yes | No | No | Yes | Partial |
| Modern Web UI | Yes | No | No | Yes | Yes |
| Open source | Yes | Yes | Yes | No | Yes |
| Per-site PHP | Yes | No | No | Yes | No |
| Template profiles | Yes | No | No | Yes | Fixed |
| REST API built-in | Yes | No | No | Yes | No |
| Daemon architecture | Yes | No | No | No | No |

## Architecture Proposal requirement

Before any new subsystem is implemented, an Architecture Proposal
must be written and approved. This ensures every feature aligns with
the Product Vision before code is written.

Proposals are stored in `planning/proposals/` and follow the template
in `planning/proposals/README.md`.

## Core principles (immutable)

These principles must never be violated:

### 1. API first

Every operation must be available through the REST API before any
other interface. CLI and Web UI are only clients of the API.

### 2. Daemon owns business logic

The daemon process owns all business logic, managers, storage, and
providers. The CLI is a thin client that communicates via UNIX socket.
The Web UI communicates via HTTP REST API.

### 3. Everything is a Resource

Every managed entity inherits from `core::Resource`. Resources have
`id` and `name`, are managed by a Manager, and are persisted via
Storage. This guarantees uniform behaviour across all subsystems.

### 4. Providers isolate external systems

Every external system (Docker, reverse proxy, SSL, backup, SFTP, DNS,
mail) is accessed through a Provider interface. The core never depends
directly on external tools.

### 5. Configuration is editable without recompilation

Templates, profiles, and configuration files live on disk.
Administrators can edit them with any text editor. The executable
only renders templates, never hardcodes them.

### 6. Production before optimization

Simple working code is preferred over complex optimised code.
Optimise only when production usage proves a bottleneck exists.

### 7. Backward compatibility

Existing CLI commands, API endpoints, and configuration files must
continue working across minor versions. Breaking changes require a
major version bump with documented migration.

### 8. Predictable over clever

Code should be easy to read and reason about. Follow established
patterns even if a more elegant solution exists. Consistency across
the codebase is more valuable than cleverness in one module.

### 9. One responsibility per subsystem

Every module has exactly one responsibility.
- `libs/backup/` — backup and restore
- `libs/proxy/` — reverse proxy
- `libs/ssl/` — certificate management
- `libs/access/` — access users and permissions
- `libs/dns/` — DNS management (future)

No module should grow to do two things.

### 10. Resources are always linked

Every resource that belongs to a Site (domain, database, backup, SSL,
proxy, access user) has a `site_id` foreign key. This enables the UI
to navigate from any resource to its related resources and back.

## What is explicitly out of scope

- **Kubernetes orchestration** — ContainerCP manages Docker Compose
  stacks, not Kubernetes clusters

- **Managed Kubernetes** — no plan to compete with GKE, EKS, AKS

- **Public cloud hosting** — ContainerCP runs on your own server, not
  as a SaaS platform

- **CMS management** — no WordPress, Joomla, or Drupal updater built in

- **E-commerce features** — no shopping cart, payment processing, or
  store management

- **Email marketing** — no newsletter or campaign tools

- **CI/CD pipeline** — no built-in build server or deployment pipeline

- **Monitoring infrastructure** — no Prometheus/Grafana replacement

- **Log aggregation platform** — no ELK/Loki replacement

- **Managed database as a service** — no separate database hosting

## What a successful Version 1.0 looks like

A successful v1.0 release means:

1. A system administrator can install ContainerCP on a clean Debian 13
   (Trixie) server and have a working hosting platform within 30 minutes

2. The administrator can create a website, point a domain, provision
   SSL, create an SFTP user, and upload files — all from the Web UI

3. The platform reliably creates, starts, stops, and removes container
   stacks without leaving orphan resources

4. Backups can be created and restored through the CLI and Web UI

5. The daemon runs continuously without crashes or memory leaks

6. All 137 validation checklist items pass

7. A first-time user can deploy a PHP website without reading
   documentation

## What Version 2.0 could become

Version 2.0 is the long-term vision, not a commitment:

- **Multi-server cluster** — one control plane manages multiple nodes
- **DNS management** — built-in DNS server or API integration
- **Mail server** — integrated Postfix/Dovecot stack
- **Monitoring dashboard** — real-time resource usage, alerts
- **Team collaboration** — multi-admin with roles and permissions
- **API tokens** — authenticated API access for automation
- **Webhooks** — event-driven integration with external systems
- **Plugin system** — third-party extensions
- **One-click installers** — WordPress, Laravel, Joomla templates
- **Import tool** — migrate from VestaCP, HestiaCP, cPanel
- **Commercial edition** — enterprise features for hosting providers
