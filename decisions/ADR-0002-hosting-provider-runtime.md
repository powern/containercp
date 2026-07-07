# ADR-0002: HostingProvider and Runtime separation

## Status

Accepted

## Context

The system needs to run Docker Compose stacks for PHP sites.
Directly coupling site operations to Docker commands would make it
impossible to support other runtimes (Podman, Kubernetes) later.

The `Runtime` interface was introduced first as a thin abstraction
over docker compose commands. However, creating a site involves more
than running a container — it requires filesystem setup, template
generation, and environment configuration.

## Decision

Two abstractions with different responsibilities:

- **Runtime** — low-level container lifecycle
  (`create_site_stack`, `start_site`, `stop_site`, etc.)
  Operates on a domain string and runs docker commands.

- **HostingProvider** — high-level site orchestration
  (`create_site`, `start_site`, etc.)
  Operates on a `Site` object and coordinates filesystem, templates,
  and Runtime calls.

`DockerComposeProvider` implements `HostingProvider` and calls
`DockerRuntime` internally. A future `KubernetesProvider` could
implement the same interface without changing site operations.

## Consequences

- `SiteCreateOperation` only depends on `HostingProvider`
- Adding a new runtime only requires a new Provider implementation
- CLI handlers talk to Provider, not Runtime directly
- Slightly more indirection but clear separation of concerns
