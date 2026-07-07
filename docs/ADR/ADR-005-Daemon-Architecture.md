# ADR-005: Daemon Architecture

## Status

Accepted

## Context

ContainerCP started as a CLI-only application. As the system grew
(managers, storage, runtime, reverse proxy, SSL, REST API), it
became clear that a client/server split is necessary.

A daemon process owns all persistent state, resources, and long-running
services. The CLI becomes a thin client that communicates with the
daemon over a UNIX socket.

## Decision

### Binaries

- **containercpd** — daemon process. Owns Application, ServiceRegistry,
  all managers, Storage, Runtime, Reverse Proxy, SSL, Access, and the
  REST API server. Listens on a UNIX socket for CLI commands.

- **containercp** — thin CLI client. Parses arguments, connects to the
  daemon over UNIX socket, sends the command, receives the response,
  and prints it to stdout.

### Communication

- UNIX socket at `/srv/containercp/containercpd.sock`
- Text protocol: `COMMAND|arg1|arg2|...`
- Response: `SUCCESS|message` or `ERROR|message`
- Simple, debuggable, no serialization dependencies

### Daemon lifecycle

1. Creates Application and ServiceRegistry
2. Loads all persisted state from Storage
3. Starts UNIX socket server on background thread
4. Starts REST API server on background thread
5. Main thread waits for shutdown signal

### CLI lifecycle

1. Parses arguments
2. Connects to UNIX socket
3. Sends command string
4. Receives response
5. Prints to stdout
6. Exits with appropriate code

### State management

The daemon calls `save()` after every mutating command.
The CLI never holds state.

## Consequences

- Containercpd must be running before CLI commands work
- CLI becomes trivially stateless
- Adding new commands only requires updating the daemon
- REST API benefits from same ServiceRegistry
- Future: systemd service file for daemon
