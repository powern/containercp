# BACKUP-001: BackupProvider interface

## Description

Create an abstract BackupProvider interface following the existing
provider pattern (HostingProvider, CertificateProvider, AccessProvider).

## Requirements

### Interface

```
class BackupProvider {
    virtual ~BackupProvider() = default;
    virtual OperationResult create_backup(const std::string& site_dir, const std::string& output_path);
    virtual OperationResult restore_backup(const std::string& backup_path, const std::string& site_dir);
    virtual OperationResult remove_backup(const std::string& backup_path);
    virtual OperationResult list_backups(const std::string& site_dir);
};
```

### Location

libs/backup/BackupProvider.h

### Namespace

containercp::backup
