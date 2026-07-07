# BACKUP-006: REST API endpoint

## Description

Add GET /api/backups endpoint.

## Requirements

Return list of all backups as JSON.

Format:
```json
{
    "success": true,
    "data": [
        {
            "id": 1,
            "site_id": 1,
            "filename": "example.com-20240101.tar.gz",
            "type": "manual",
            "size": 1048576,
            "created_at": "2024-01-01T00:00:00Z",
            "status": "completed",
            "file_path": "/srv/containercp/backups/example.com-20240101.tar.gz",
            "compression": "gzip"
        }
    ]
}
```
