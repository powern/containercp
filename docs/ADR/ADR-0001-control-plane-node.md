# ADR-0001: Control Plane and Node separation

## Status

Accepted

## Context

ContainerCP must manage hosting resources on servers. The architecture
must support a future where one control plane manages multiple servers.

Early commits mixed CLI, business logic, and data storage in a single
executable. This is acceptable for the MVP but the design must not
block future separation.

## Decision

The codebase distinguishes two conceptual roles:

- **Control Plane** — CLI, configuration, orchestration, storage
- **Node** — server that runs workloads (Docker, proxy, data)

In the MVP both run on the same machine and in the same process.
However, every entity (Site, Node, etc.) is designed with a `node_id`
field so it can be assigned to a remote Node later.

The `Resource` base class and `ResourceManager` provide a uniform
way to manage entities regardless of where they run.

## Consequences

- Adding remote Nodes later will not require schema changes
- Business logic never assumes a single server
- The trade-off is extra indirection in the MVP (node_id on every entity)
