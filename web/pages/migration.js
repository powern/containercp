import {
  api, apiPost, card, esc
} from '../core/context.js';


/* ===== MIGRATION (myVestaCP Import) ===== */
async function loadMigration(p) {
  try {
    let html = `<div class="page-header"><h1>Migration</h1><div class="page-actions"></div></div>`;
    html += `<div class="card"><div class="card-header"><h3>Import from myVestaCP</h3></div>
      <div style="padding:16px;">
      <p style="margin-bottom:16px;color:var(--text2);">Analyze a myVestaCP backup archive before importing into ContainerCP.</p>`;

    // Load available backups
    html += `<div class="form-group"><label>Backup file</label>
      <select id="migrate-backup" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text);">
      <option value="">Loading backups...</option></select></div>`;

    html += `<div class="form-group"><label>Domain</label>
      <input type="text" id="migrate-domain" placeholder="example.com" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text);"></div>`;

    html += `<div class="form-group"><label>Owner</label>
      <input type="text" id="migrate-owner" value="admin" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text);"></div>`;

    html += `<details style="margin-bottom:12px;"><summary style="cursor:pointer;font-size:13px;color:var(--text2);">Advanced options</summary>
      <div class="form-group" style="margin-top:8px;"><label>Database (override)</label>
        <input type="text" id="migrate-database" placeholder="Auto-detect" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text);"></div>
      <div style="display:flex;gap:16px;margin-top:8px;">
        <label><input type="checkbox" id="migrate-skip-db"> Skip database import</label>
        <label><input type="checkbox" id="migrate-keep-staging"> Keep staging files</label>
      </div>
    </details>`;

    html += `<button class="btn btn-primary" id="migrate-analyze-btn" onclick="analyzeBackup()">Analyze backup</button>
      <div id="migrate-result" style="margin-top:16px;"></div>
      </div></div>`;

    p.innerHTML = html;

    // Load backup list
    try {
      const backups = await api('/api/migration/vesta/backups');
      const sel = document.getElementById('migrate-backup');
      if (backups.data && backups.data.length > 0) {
        sel.innerHTML = '<option value="">Select a backup...</option>';
        backups.data.forEach(b => {
          sel.innerHTML += `<option value="${esc(b.name)}">${esc(b.name)} (${(b.size/1024/1024).toFixed(1)} MB)</option>`;
        });
      } else {
        sel.innerHTML = '<option value="">No backups found</option>';
      }
    } catch(e) {
      document.getElementById('migrate-backup').innerHTML = '<option value="">Failed to load backups</option>';
    }
  } catch(e) {
    p.innerHTML = '<div class="empty-state">Failed to load migration page</div>';
  }
}

async function analyzeBackup() {
  const btn = document.getElementById('migrate-analyze-btn');
  const resultDiv = document.getElementById('migrate-result');
  btn.disabled = true;
  btn.textContent = 'Analyzing...';
  resultDiv.innerHTML = '';

  try {
    const backup = document.getElementById('migrate-backup').value;
    const domain = document.getElementById('migrate-domain').value.trim();
    const owner = document.getElementById('migrate-owner').value.trim();
    const database = document.getElementById('migrate-database').value.trim();
    const skipDb = document.getElementById('migrate-skip-db').checked;
    const keepStaging = document.getElementById('migrate-keep-staging').checked;

    if (!backup || !domain || !owner) {
      resultDiv.innerHTML = '<div class="alert alert-error">Backup, domain and owner are required.</div>';
      btn.disabled = false; btn.textContent = 'Analyze backup';
      return;
    }

    const body = { backup, domain, owner };
    if (database) body.database = database;
    if (skipDb) body.skip_db = true;
    if (keepStaging) body.keep_staging = true;

    const res = await apiPost('/api/migration/vesta/inspect', body);
    const d = res.data;

    let html = '<div class="card" style="margin-top:12px;"><div class="card-header"><h3>Analysis Result</h3></div><div style="padding:12px;font-size:13px;">';

    if (d.errors && d.errors.length > 0) {
      html += '<div style="color:var(--red);margin-bottom:8px;"><strong>Errors:</strong><ul>';
      d.errors.forEach(e => { html += '<li>' + esc(e) + '</li>'; });
      html += '</ul></div>';
    }

    html += '<table style="width:100%;border-collapse:collapse;">';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Domain found</td><td>' + (d.domain_found ? '<span class="badge badge-ok">Yes</span>' : '<span class="badge badge-info">No</span>') + '</td></tr>';

    if (d.domain_found) {
      html += '<tr><td style="padding:4px 8px;color:var(--text2);">Web archive</td><td>' + esc(d.web_archive_path) + '</td></tr>';
      html += '<tr><td style="padding:4px 8px;color:var(--text2);">Web root</td><td>' + esc(d.web_root_type) + '</td></tr>';
      html += '<tr><td style="padding:4px 8px;color:var(--text2);">WordPress config</td><td>' + (d.wp_config_found ? '<span class="badge badge-ok">Found</span>' : '<span class="badge badge-info">Not found</span>') + '</td></tr>';

      if (d.wp_config_found) {
        if (d.wp_db_ambiguous) {
          html += '<tr><td style="padding:4px 8px;color:var(--text2);">DB_NAME</td><td><span class="badge badge-warning">Ambiguous</span> Use database override</td></tr>';
        } else if (d.wp_config_parsed) {
          html += '<tr><td style="padding:4px 8px;color:var(--text2);">DB_NAME</td><td>' + esc(d.wp_db_name) + '</td></tr>';
          html += '<tr><td style="padding:4px 8px;color:var(--text2);">DB_USER</td><td>' + esc(d.wp_db_user) + '</td></tr>';
        }
      }

      html += '<tr><td style="padding:4px 8px;color:var(--text2);">SQL dump</td><td>' + (d.db_dump_found ? '<span class="badge badge-ok">Found (' + esc(d.db_type) + ')</span>' : '<span class="badge badge-info">Not found</span>') + '</td></tr>';

      if (d.all_databases && d.all_databases.length > 0) {
        html += '<tr><td style="padding:4px 8px;color:var(--text2);">Available DBs</td><td>' + d.all_databases.map(esc).join(', ') + '</td></tr>';
      }
    }

    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Site exists</td><td>' + (d.site_exists ? '<span class="badge badge-error">Yes</span>' : '<span class="badge badge-ok">No</span>') + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Disk available</td><td>' + (d.available_disk_mb > 0 ? d.available_disk_mb + ' MB' : 'Unknown') + '</td></tr>';
    html += '</table>';

    if (d.warnings && d.warnings.length > 0) {
      html += '<div style="margin-top:8px;"><strong>Warnings:</strong><ul>';
      d.warnings.forEach(w => { html += '<li style="color:var(--orange);">' + esc(w) + '</li>'; });
      html += '</ul></div>';
    }

    html += '</div></div>';

    // Decide which buttons to show based on state machine
    const hasErrors = d.errors && d.errors.length > 0;

    if (!hasErrors) {
      if (!d.site_exists) {
        // Stage 0: Create site
        html += `<div class="card" style="margin-top:12px;border-color:var(--blue);">
          <div class="card-header"><h3>Migration Stage 0 — Backup Analyzed</h3></div>
          <div style="padding:12px;font-size:13px;">
            <p style="margin-bottom:12px;">Backup analyzed. Site not yet created.</p>
            <button class="btn btn-primary" onclick="importMigrationSite()" id="migrate-import-btn">Create site</button>
            <div id="migrate-import-result" style="margin-top:12px;"></div>
          </div></div>`;

      } else if (d.migration_completed) {
        // Stage 3: Completed
        html += `<div class="card" style="margin-top:12px;border-color:var(--green);">
          <div class="card-header"><h3 style="color:var(--green);">Migration Completed</h3></div>
          <div style="padding:12px;font-size:13px;">
            <p>All migration stages completed.</p>
          </div></div>`;

      } else if (d.site_exists && d.migration_marker_found) {
        // Migration in progress — show current stage
        const stageLabels = {1: 'Site created', 2: 'Files imported'};
        const stageDesc = stageLabels[d.migration_stage] || ('Stage ' + d.migration_stage);
        html += `<div class="card" style="margin-top:12px;border-color:var(--blue);">
          <div class="card-header"><h3 style="color:var(--blue);">Migration: ${esc(stageDesc)}</h3></div>
          <div style="padding:12px;font-size:13px;">
            <table style="width:100%;border-collapse:collapse;">
              <tr><td style="padding:4px 8px;color:var(--text2);">Site ID</td><td>${d.migration_site_id || '?'}</td></tr>
              <tr><td style="padding:4px 8px;color:var(--text2);">Files</td><td>${d.files_status === 'imported' ? '<span class="badge badge-ok">Imported</span>' : '<span class="badge badge-info">' + esc(d.files_status || 'Pending') + '</span>'}</td></tr>
              <tr><td style="padding:4px 8px;color:var(--text2);">SQL</td><td>${d.sql_status === 'imported' ? '<span class="badge badge-ok">Imported</span>' : '<span class="badge badge-info">' + esc(d.sql_status || 'Pending') + '</span>'}</td></tr>
            </table>`;

        // Import files button (stage 1 → 2)
        if (d.can_import_files) {
          html += `<button class="btn btn-primary" style="margin-top:12px;" onclick="importMigrationFiles()">Import files</button>
            <div id="migrate-files-result" style="margin-top:12px;"></div>`;
        }

        // Import SQL button (stage 2 → 3)
        if (d.can_import_sql) {
          html += `<button class="btn btn-primary" style="margin-top:12px;" onclick="importMigrationSql()">Import SQL</button>
            <div id="migrate-sql-result" style="margin-top:12px;"></div>`;
        }

        html += `</div></div>`;

      } else if (d.site_exists && d.migration_marker_found && d.marker_error) {
        html += `<div class="alert alert-warning" style="margin-top:12px;">Marker error: ${esc(d.marker_error)}</div>`;
      } else if (d.site_exists) {
        html += `<div class="alert alert-info" style="margin-top:12px;">Existing site is not an active myVesta migration.</div>`;
      }
    }

    resultDiv.innerHTML = html;
  } catch(e) {
    resultDiv.innerHTML = '<div class="alert alert-error">Analysis failed: ' + esc(e.message || 'Unknown error') + '</div>';
  }

  btn.disabled = false;
  btn.textContent = 'Analyze backup';
}

async function importMigrationSite() {
  const btn = document.getElementById('migrate-import-btn');
  const resultDiv = document.getElementById('migrate-import-result');
  if (!btn || !resultDiv) return;

  btn.disabled = true;
  btn.textContent = 'Importing...';
  resultDiv.innerHTML = '';

  try {
    const backup = document.getElementById('migrate-backup').value;
    const domain = document.getElementById('migrate-domain').value.trim();
    const owner = document.getElementById('migrate-owner').value.trim();
    const database = document.getElementById('migrate-database').value.trim();
    const skipDb = document.getElementById('migrate-skip-db').checked;
    const keepStaging = document.getElementById('migrate-keep-staging').checked;

    const body = { backup, domain, owner };
    if (database) body.database = database;
    if (skipDb) body.skip_db = true;
    if (keepStaging) body.keep_staging = true;

    const res = await apiPost('/api/migration/vesta/create-site', body);
    const d = res.data;

    let html = '<div class="card" style="margin-top:12px;border-color:var(--green);"><div class="card-header"><h3 style="color:var(--green);">Stage 1 Completed</h3></div><div style="padding:12px;font-size:13px;">';
    html += '<p style="margin-bottom:12px;">' + esc(d.message) + '</p>';
    html += '<table style="width:100%;border-collapse:collapse;">';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Site ID</td><td><strong>' + d.site_id + '</strong></td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Domain</td><td>' + esc(d.domain) + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Database</td><td>' + esc(d.database_name) + ' / ' + esc(d.database_user) + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Document root</td><td>' + esc(d.document_root) + '</td></tr>';
    html += '</table>';

    html += '<div style="margin-top:12px;"><strong>Status:</strong><ul style="margin-top:4px;">';
    if (d.status) {
      for (const [key, val] of Object.entries(d.status)) {
        const icon = val === 'created' ? '✅' : '⏳';
        html += '<li>' + icon + ' ' + esc(key) + ': ' + esc(val) + '</li>';
      }
    }
    html += '</ul></div>';

    html += '<p style="margin-top:12px;font-size:12px;color:var(--text2);">Run Analyze backup again to continue with Stage 2.</p>';
    html += '</div></div>';
    resultDiv.innerHTML = html;
  } catch(e) {
    resultDiv.innerHTML = '<div class="alert alert-error">Import failed: ' + esc(e.message || 'Unknown error') + '</div>';
  }

  btn.disabled = false;
  btn.textContent = 'Import site';
}

async function importMigrationFiles() {
  const resultDiv = document.getElementById('migrate-files-result');
  if (!resultDiv) return;
  resultDiv.innerHTML = 'Importing files...';

  try {
    const backup = document.getElementById('migrate-backup').value;
    const domain = document.getElementById('migrate-domain').value.trim();
    const owner = document.getElementById('migrate-owner').value.trim();
    const keepStaging = document.getElementById('migrate-keep-staging').checked;

    const body = { backup, domain, owner };
    if (keepStaging) body.keep_staging = true;

    const res = await apiPost('/api/migration/vesta/import-files', body);
    const d = res.data;

    let html = '<div class="card" style="margin-top:12px;border-color:var(--green);"><div class="card-header"><h3 style="color:var(--green);">Stage 2 Completed</h3></div><div style="padding:12px;font-size:13px;">';
    html += '<p style="margin-bottom:12px;">' + esc(d.message) + '</p>';
    html += '<table style="width:100%;border-collapse:collapse;">';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Web root</td><td>' + esc(d.web_root) + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Destination</td><td>' + esc(d.destination) + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Files</td><td>' + (d.files_count || 'unknown') + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Size</td><td>' + (d.bytes_copied ? Math.round(d.bytes_copied/1024) + ' KB' : 'unknown') + '</td></tr>';
    html += '</table>';

    html += '<div style="margin-top:12px;"><strong>Status:</strong><ul style="margin-top:4px;">';
    if (d.status) {
      for (const [key, val] of Object.entries(d.status)) {
        const icon = val === 'imported' ? '✅' : '⏳';
        html += '<li>' + icon + ' ' + esc(key) + ': ' + esc(val) + '</li>';
      }
    }
    html += '</ul></div>';

    if (d.warnings && d.warnings.length > 0) {
      html += '<div style="margin-top:8px;"><strong>Warnings:</strong><ul>';
      d.warnings.forEach(w => { html += '<li style="color:var(--orange);">' + esc(w) + '</li>'; });
      html += '</ul></div>';
    }

    html += '</div></div>';
    resultDiv.innerHTML = html;
  } catch(e) {
    resultDiv.innerHTML = '<div class="alert alert-error">File import failed: ' + esc(e.message || 'Unknown error') + '</div>';
  }
}

async function importMigrationSql() {
  const resultDiv = document.getElementById('migrate-sql-result');
  if (!resultDiv) return;
  resultDiv.innerHTML = 'Starting SQL import...';

  try {
    const backup = document.getElementById('migrate-backup').value;
    const domain = document.getElementById('migrate-domain').value.trim();
    const owner = document.getElementById('migrate-owner').value.trim();
    const keepStaging = document.getElementById('migrate-keep-staging').checked;

    const body = { backup, domain, owner };
    if (keepStaging) body.keep_staging = true;

    const res = await apiPost('/api/migration/vesta/import-sql', body);
    const d = res.data;
    const jobId = d.job_id;

    // Poll job status
    resultDiv.innerHTML = '<div class="card" style="margin-top:12px;"><div class="card-header"><h3>SQL Import Running</h3></div><div style="padding:12px;font-size:13px;"><p>Job #' + jobId + ' started. Waiting for completion...</p><div id="migrate-sql-progress"></div></div></div>';

    const poll = setInterval(async () => {
      try {
        const statusRes = await api('/api/jobs?id=' + jobId);
        // API returns { success, data: { id, type, status, progress, message } }
        // or { success, data: [...] } for list view
        const jobData = statusRes && statusRes.data;
        if (!jobData || !jobData.status) {
          // Don't clear polling yet — maybe job is still being created
          return;
        }
        const progressDiv = document.getElementById('migrate-sql-progress');
        if (progressDiv) {
          const statusDisplay = esc(jobData.status || 'unknown');
          const progressDisplay = typeof jobData.progress === 'number' ? jobData.progress : 0;
          const msgDisplay = esc(jobData.message || '');
          progressDiv.innerHTML = '<div style="margin-bottom:8px;"><strong>Status:</strong> ' + statusDisplay + '</div>'
            + '<div style="margin-bottom:8px;"><strong>Progress:</strong> ' + progressDisplay + '%</div>'
            + (msgDisplay ? '<div><strong>Message:</strong> ' + msgDisplay + '</div>' : '');
        }

        if (jobData.status === 'completed') {
          clearInterval(poll);
          resultDiv.innerHTML = '<div class="card" style="margin-top:12px;border-color:var(--green);"><div class="card-header"><h3 style="color:var(--green);">Stage 3 Completed</h3></div><div style="padding:12px;font-size:13px;"><p>SQL import completed successfully.</p></div></div>';
        } else if (jobData.status === 'failed') {
          clearInterval(poll);
          resultDiv.innerHTML = '<div class="alert alert-error">SQL import failed: ' + esc(jobData.message || 'Unknown error') + '</div>';
        }
      } catch(e) {
        clearInterval(poll);
        resultDiv.innerHTML = '<div class="alert alert-error">Failed to poll job status: ' + esc(e.message || 'Unknown') + '</div>';
      }
    }, 2000);
  } catch(e) {
    resultDiv.innerHTML = '<div class="alert alert-error">SQL import failed: ' + esc(e.message || 'Unknown error') + '</div>';
  }
}

export { loadMigration };
Object.assign(window, { loadMigration, analyzeBackup, importMigrationSite, importMigrationFiles, importMigrationSql });
