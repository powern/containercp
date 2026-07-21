#!/usr/bin/env node

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const backups = fs.readFileSync(path.join(root, 'web/pages/backups.js'), 'utf8');
const databases = fs.readFileSync(path.join(root, 'web/pages/databases.js'), 'utf8');

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

assert(backups.includes("api('/api/backups')"), 'Backups page must list backups through the Backups API');
assert(backups.includes("apiPost('/api/backups'"), 'Backups page must create backups through POST /api/backups');
assert(backups.includes("'/api/backups/' + Number(id) + '/restore'"), 'Backups page must restore through backup-id restore route');
assert(backups.includes("'/ui-api/api/backups/' + Number(id) + '/download'"), 'Backups page must download through backup-id download route');
assert(backups.includes('full') && backups.includes('files_only') && backups.includes('database_only'), 'Backups page must expose DB-5 restore modes');
assert(backups.includes('contains_database') && backups.includes('database_status'), 'Backups page must display DB-aware metadata');
assert(!backups.includes('file_path'), 'Backups page must not render internal backup file paths');
assert(!backups.includes('DB_PASSWORD') && !backups.includes('MYSQL_ROOT_PASSWORD'), 'Backups page must not render database secret tokens');

assert(!databases.includes('Backup Database'), 'Databases page must not add a Backup Database action');
assert(!databases.includes('/api/backups'), 'Databases page must not call Backups API for DB-5 controls');

console.log('backup action regression tests passed');
