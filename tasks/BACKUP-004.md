# BACKUP-004: Update Backup resource and manager

## Description

Extend Backup struct with file_path and compression fields.
Update BackupManager and Storage format.

## Requirements

### struct changes

```
struct Backup : core::Resource {
    uint64_t site_id = 0;
    uint64_t owner_id = 0;
    std::string filename;
    std::string type = "manual";
    uint64_t size = 0;
    std::string created_at;
    std::string status = "completed";
    std::string file_path;  // NEW
    std::string compression = "gzip";  // NEW
};
```

### Manager update

- create() signature unchanged (file_path set separately)
- find_by_site(site_id) method added
