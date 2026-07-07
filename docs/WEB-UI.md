# ContainerCP Admin Panel

## How to start

1. Build the daemon:

```
cmake --build build-release
```

2. Start the daemon:

```
./build/containercpd
```

3. Open in browser:

Local access (full functionality, no auth):
```
http://127.0.0.1:8080/
```

External access (with basic auth):
```
http://<server-ip>:8081/
```

Default credentials:
- **Username:** `admin`
- **Password:** Generated on first daemon start. Printed to daemon log.
  Stored at `/etc/containercp/ui-password`. Set a custom password by
  writing it to this file before starting the daemon.

For SSH tunneling (bypasses basic auth):
```
ssh -L 8080:127.0.0.1:8080 user@<server>
```

## Dashboard

The dashboard shows:

- Resource cards with counts (Sites, Domains, Databases, SSL, Proxy, Access, Users, Nodes)
- System health panel (Daemon, REST API, Storage, Runtime, Proxy)
- Recent activity feed

## Pages

| Page | Description |
|------|-------------|
| Dashboard | Overview with cards, health, activity |
| Sites | Table with create, filter, actions |
| Domains | Table with SSL status badges |
| Databases | Table with engine info |
| SSL | Table with certificate status badges |
| Proxy | Table with proxy configs |
| Access | Table with access users |
| Profiles | Configuration profiles (tabs for Web/PHP/Docker/SSL) |
| Templates | Web server template list |
| Nodes | Node details |
| Logs | System log viewer |
| Settings | Application settings |

## Features

- **Dark theme by default** with light/dark toggle
- **Responsive layout** with collapsible sidebar on mobile
- **Search** across resource tables
- **Status indicator** in top bar (green/red dot)
- **Version badge** in top bar
- **Inline SVG icons** throughout the UI
- **Professional typography** and consistent spacing

## REST API endpoints used

| Endpoint | Page |
|----------|------|
| GET /api/health | Dashboard, status |
| GET /api/version | Top bar |
| GET /api/sites | Dashboard, Sites |
| GET /api/domains | Dashboard, Domains |
| GET /api/databases | Dashboard, Databases |
| GET /api/ssl | Dashboard, SSL |
| GET /api/proxy | Dashboard, Proxy |
| GET /api/access-users | Dashboard, Access |
| GET /api/users | Dashboard |
| GET /api/nodes | Dashboard |

## Technical details

- Pure HTML5, CSS3, Vanilla JavaScript
- No frameworks, no build tools, no npm
- Served by containercpd's built-in HTTP server
- Files located in `/opt/containercp/web/`
- Zero external dependencies
