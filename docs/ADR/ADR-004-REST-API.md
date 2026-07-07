# ADR-004: REST API Foundation

## Status

Accepted

## Context

ContainerCP needs a REST API for:
- future Web UI
- automation and scripting
- Terraform/Ansible integration
- SDK and external integrations

## Decision

### Architecture

The API is a built-in HTTP server within the `containercp` executable.
No external frameworks or libraries are used.

### Components

- **ApiServer** — TCP server, accepts connections, dispatches to Router
- **Router** — maps URL patterns to handler functions
- **Request** — parses HTTP/1.1 requests
- **Response** — builds HTTP/1.1 responses
- **JsonFormatter** — serializes resources to JSON
- **AuthMiddleware** — authentication interface (placeholder, always allows)

### Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | /api/version | Application version |
| GET | /api/health | Health check |
| GET | /api/sites | List all sites |
| GET | /api/users | List all users |
| GET | /api/domains | List all domains |
| GET | /api/proxy | List all proxy configs |
| GET | /api/ssl | List all SSL certificates |

### JSON format

All responses follow a consistent JSON envelope:

```json
{
    "success": true,
    "data": { ... }
}
```

Errors:

```json
{
    "success": false,
    "error": "message"
}
```

### Authentication

An `AuthMiddleware` interface allows plugging in authentication later.
Currently, all requests are allowed.

### Server configuration

- Port: configured via `--api-port` CLI flag or `CONTAINERCP_API_PORT` env
- Default: 8080
- Binds to 127.0.0.1 only for security

## Consequences

- API is always available when the process runs
- No external dependencies
- Easy to extend with new endpoints
- Authentication can be added without changing controllers
- JSON format is consistent across all endpoints
