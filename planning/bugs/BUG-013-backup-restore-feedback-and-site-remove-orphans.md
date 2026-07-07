# BUG-013: Backup restore feedback missing, site removal deletes backups

## Severity
Medium

## Description

Two issues discovered during Web UI validation:

1. **Backup restore button does nothing.** The restore icon in the
   backups table had no `onclick` handler. Clicking it was a no-op
   with zero feedback.

2. **Site removal deletes backup archive files.** The
   `SiteRemoveOperation::execute()` method explicitly called
   `std::system("rm -f " + file_path)` to delete every backup file
   belonging to the removed site, and also removed all backup records.
   This is incorrect: backups should survive site removal so operators
   can inspect or restore them later.

## Fix

### Backup restore feedback
- Added `restoreBackup(id, filename)` function with confirmation dialog
- Success toast: "Backup restored successfully" + refresh list
- Error toast: back-end error message
- Loading guard to prevent double-submission

### Backup removal feedback  
- Added success/error toast and list refresh to `removeBackup()`

### Site removal preserves backups
- Removed the backup deletion loop from `SiteRemoveOperation::execute()`
- Backup archive files in `/srv/containercp/backups/` now survive
- Backup records in the database are preserved (orphaned)
- CLI and REST API both go through `SiteRemoveOperation`, so both paths
  are fixed

## Files changed
- `web/app.js` — restore button onClick, restoreBackup function,
  removeBackup feedback
- `libs/operations/SiteRemoveOperation.cpp` — stop deleting backups
