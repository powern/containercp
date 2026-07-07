# Architecture

## Core idea

ContainerCP is designed as a control plane for container-oriented hosting.

The first MVP runs on a single server.

However, the architecture must be ready for future multi-server support.

## Main concepts

### Control Plane

The Control Plane is responsible for:

- CLI
- API
- database
- configuration
- orchestration logic
- task planning
- audit logging

In the MVP, the Control Plane runs on the same server as the first hosting node.

### Node

A Node is a server that runs workloads.

A Node may provide:

- container runtime
- reverse proxy
- volumes
- site files
- databases
- logs
- backups

In the MVP, there is only one node:

- local

## Important rule

Business logic must not assume that there is only one server.

Even if the first implementation uses only the local server, entities must be designed with future Node support.

Examples:

- a Site belongs to a Node
- a Container belongs to a Node
- a Volume belongs to a Node
- a Backup belongs to a Node

## MVP topology

Control Plane and Node run on the same Debian server.

```text
+-----------------------------+
| Debian server               |
|                             |
|  +-----------------------+  |
|  | ContainerCP CLI/Core  |  |
|  +-----------------------+  |
|             |               |
|             v               |
|  +-----------------------+  |
|  | Local Node            |  |
|  | Docker / Proxy / Data |  |
|  +-----------------------+  |
|                             |
+-----------------------------+
Future topology

In the future, the Control Plane may manage multiple remote Nodes.

+-----------------------------+
| Control Plane               |
| CLI / API / DB / UI         |
+-----------------------------+
        |        |        |
        v        v        v
   Node 1   Node 2   Node 3
First implementation decision

The MVP uses a LocalNode provider.

Remote nodes are not implemented yet.

But the architecture must not block adding them later.
