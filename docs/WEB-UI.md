# Web UI Preview

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

```
http://127.0.0.1:8080/
```

## Available API endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | /api/version | Application version |
| GET | /api/health | Health check |
| GET | /api/sites | List all sites |
| GET | /api/users | List all users |
| GET | /api/domains | List all domains |
| GET | /api/databases | List all databases |
| GET | /api/ssl | List SSL certificates |
| GET | /api/proxy | List proxy configs |
| GET | /api/access-users | List access users |

## Dashboard pages

- **Dashboard** — overview cards with counts for all resource types
- **Sites** — table of all sites
- **Users** — table of all users
- **Domains** — table of all domains
- **Databases** — table of all databases
- **SSL** — table of SSL certificates
- **Proxy** — table of proxy configs
- **Access** — table of access users

## Current limitations

- Read-only: the UI only displays data, no create/edit/delete
- No authentication: the API is open on localhost
- No pagination: all data is loaded at once
- No real-time updates: manual refresh required
- Static files are served from `/opt/containercp/web/`
- The daemon must be running for the UI to work
