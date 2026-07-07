# Sprint 3 — Infrastructure Compatibility

**Goal:** Make ContainerCP resource model compatible with classic hosting
panels like myVestaCP / VestaCP.

Classic hosting panels model infrastructure as a set of standalone
resources: Users, Domains, Databases, Backups, SSL certificates, and
Mail accounts. ContainerCP's current resource model (Node, Site) must
be extended to support this view while keeping its container-native
architecture.

## Scope

- User resource with hosting package support
- Domain resource (separate from Site record)
- PHP version abstraction (per-site PHP selection)
- Database resource (standalone, not just embedded in compose)
- Backup resource (scheduled backup metadata)
- SSL resource (certificate tracking and Let's Encrypt placeholders)
- Mail placeholder resource (future mail stack)

## Out of scope

- Actual Let's Encrypt integration
- Actual mail delivery
- DNS server management
- Reverse proxy configuration
- Web UI
- Multi-node support

## Definition of done

1. Every new resource inherits from `core::Resource`
2. Every new resource has a `node_id` field
3. Every new resource is persisted via `Storage` (pipe-delimited text)
4. Every new resource has at minimum `create`, `list`, `find` management
5. All existing Sprint 2 commands continue working

## Resource relationship

```
User
 └─ Site (owner = User)
     ├─ Domain
     ├─ PHP Version
     ├─ Database
     ├─ Backup
     ├─ SSL
     └─ Mail (placeholder)
```
