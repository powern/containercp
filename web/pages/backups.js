import {
  api, apiPost, buildTable, esc, getSessionToken, hideModal, pageHeader, pollRotationJob, renderRotationJobTimeline, showModal, summaryCards, tb, toast
} from '../core/context.js';


/* ===== BACKUPS ===== */
let activeBackupsLifecycle = null;
let backupSubmitting = false;

function backupBadge(value) {
  const v = value || 'unknown';
  const cls = v === 'completed' || v === 'included' || v === 'complete' ? 'badge-ok' : (v === 'failed' ? 'badge-err' : 'badge-info');
  return `<span class="badge ${cls}">${esc(v)}</span>`;
}

function backupErrorMessage(e, fallback) {
  return (e && e.body && e.body.error && e.body.error.message) || e.api_message || e.message || fallback;
}

function pollBackupJob(jobId, label) {
  const msgId = 'backup-job-msg';
  pollRotationJob(jobId, {
    lifecycle: activeBackupsLifecycle,
    messageEl: msgId,
    renderRunning: (id, job) => `<div class="db-job-box"><div><strong>${esc(label)} job #${esc(String(id))}</strong>: ${esc(job.message || job.status || 'pending')}</div>${renderRotationJobTimeline(job)}</div>`,
    renderFailed: (id, job) => `<div class="db-job-box"><div class="badge badge-err">${esc(label)} failed</div>${renderRotationJobTimeline(job)}</div>`,
    renderCompleted: (id, job) => `<div class="db-job-box"><div class="badge badge-ok">${esc(label)} completed</div>${renderRotationJobTimeline(job)}</div>`,
    onCompleted: () => { backupSubmitting = false; loadBackups($('page'), null, activeBackupsLifecycle); },
    onFailed: () => { backupSubmitting = false; loadBackups($('page'), null, activeBackupsLifecycle); },
    maxAttempts: 180,
    intervalMs: 2000
  });
}

async function loadBackups(p, params, lifecycle) {
  activeBackupsLifecycle = lifecycle || activeBackupsLifecycle;
  try {
    const data = await api('/api/backups');
    if (lifecycle && !lifecycle.isActive()) return;
    const rows = data.data || [];
    p.innerHTML = pageHeader('Backups', 'Site backups now include the managed MariaDB database when the Site is DB-5 eligible.', '<button class="btn btn-primary btn-sm" onclick="showBackupModal()">+ Create Backup</button>', 'Recovery')
      + summaryCards([
        {label:'Backups', value:rows.length, tone:'neutral', help:'Known backup records'},
        {label:'Completed', value:rows.filter(r => r.status === 'completed').length, tone:'healthy', help:'Restorable backups'},
        {label:'With DB', value:rows.filter(r => r.contains_database).length, tone:'healthy', help:'Backups with managed SQL payload'},
        {label:'Legacy/Unknown', value:rows.filter(r => !r.contains_database).length, tone:'warning', help:'Database payload not verified'}
      ]);
    p.innerHTML += '<div id="backup-job-msg" style="margin:10px 0;"></div>' + tb('All Backups') + buildTable([
      {label:'Filename',html:r=>esc(r.filename)},
      {label:'Size',html:r=>esc(r.size)+' bytes'},
      {label:'Archive',html:r=>backupBadge(r.status)},
      {label:'Database',html:r=>r.contains_database ? `${backupBadge(r.database_status)}<div style="font-size:11px;color:var(--text3);margin-top:3px;">${esc(r.database_engine || '')} ${esc(r.database_name || '')}</div>` : backupBadge(r.database_status || 'legacy_unknown')},
      {label:'Restore',html:r=>esc(r.restore_capability || 'files_only')},
      {label:'Date',html:r=>esc(r.created_at)},
      {label:'Actions',html:r=>r.status==='completed'?`<button class="btn-icon" title="Download" onclick="downloadBackup(${r.id},'${encodeURIComponent(r.filename || '')}')">&#8681;</button><button class="btn-icon" title="Restore" onclick="showRestoreBackupModal(${r.id})">&#8635;</button><button class="btn-icon" style="color:var(--red)" onclick="removeBackup(${r.id})">&#10005;</button>`:''}
    ], rows);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load backups</div>'; }
}

async function showBackupModal() {
  let sitesHtml = '<option value="" disabled selected>Loading sites...</option>';
  try {
    const sites = await api('/api/sites');
    const list = (sites.data||[]);
    if (list.length === 0) {
      sitesHtml = '<option value="" disabled selected>No sites available</option>';
    } else {
      sitesHtml = list.map(s => '<option value="'+esc(s.id)+'">'+esc(s.domain)+'</option>').join('');
    }
  } catch(e) {
    sitesHtml = '<option value="" disabled selected>Failed to load sites</option>';
  }
  showModal('Create Backup', '<div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Site</label><select id="bk-site-id" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">'+sitesHtml+'</select><div style="margin-top:10px;font-size:12px;color:var(--text2);">Creates a full Site archive plus the managed MariaDB logical dump. Imported or ownership-uncertain databases are rejected.</div><button class="btn btn-primary" onclick="createBackup()" style="margin-top:12px;">Create Backup</button><div id="backup-job-msg" style="margin-top:10px;"></div>', 480);
}

async function createBackup() {
  if (backupSubmitting) return;
  backupSubmitting = true;
  const sel = $('bk-site-id');
  const siteId = sel && sel.value ? Number(sel.value) : 0;
  if (!siteId) {
    toast('Select a site', 'error');
    backupSubmitting = false;
    return;
  }
  try {
    const res = await apiPost('/api/backups',{site_id:siteId});
    if (res.success) {
      toast('Backup job queued', 'success');
      pollBackupJob(res.data.job_id, 'Backup create');
    } else {
      toast('Error: '+(res.error||'Unknown'), 'error');
      backupSubmitting = false;
    }
  } catch(e) {
    toast(backupErrorMessage(e, 'Backup create failed'), 'error');
    backupSubmitting = false;
  }
}

async function showRestoreBackupModal(id) {
  try {
    const res = await api('/api/backups/' + Number(id));
    const b = res.data || {};
    const confirmValue = b.database_name || '';
    showModal('Restore Backup', `<div class="db-confirm-body">
      <p><strong>${esc(b.filename || '')}</strong></p>
      <ul>
        <li><code>full</code> restores Site files and the managed database.</li>
        <li><code>files_only</code> restores only Site files.</li>
        <li><code>database_only</code> restores only the managed database.</li>
        <li>A pre-restore recovery backup is created before destructive restore steps.</li>
      </ul>
      <select id="backup-restore-mode" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;margin-top:8px;">
        <option value="full" ${b.contains_database ? '' : 'disabled'}>full</option>
        <option value="files_only" ${b.contains_database ? '' : 'selected'}>files_only</option>
        <option value="database_only" ${b.contains_database ? '' : 'disabled'}>database_only</option>
      </select>
      <input id="backup-restore-confirm" autocomplete="off" placeholder="${esc(confirmValue || 'target site domain')}" style="width:100%;padding:8px 10px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;margin-top:8px;">
      <div style="font-size:12px;color:var(--text3);margin-top:6px;">For full restore, type the target Site domain. For database-only restore, type the target Site domain or database name.</div>
      <div id="backup-job-msg" style="margin-top:10px;"></div>
      <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:14px;"><button class="btn btn-sm" onclick="hideModal()">Cancel</button><button class="btn btn-sm btn-warning" onclick="restoreBackup(${Number(id)})">Restore</button></div>
    </div>`, 620);
  } catch(e) {
    toast(backupErrorMessage(e, 'Backup details failed'), 'error');
  }
}

async function restoreBackup(id) {
  if (backupSubmitting) return;
  const mode = $('backup-restore-mode') ? $('backup-restore-mode').value : 'full';
  const confirmation = $('backup-restore-confirm') ? $('backup-restore-confirm').value.trim() : '';
  if (!confirmation && mode !== 'files_only') {
    toast('Type the required confirmation value', 'error');
    return;
  }
  backupSubmitting = true;
  try {
    const res = await apiPost('/api/backups/' + Number(id) + '/restore', {mode, confirmation});
    if (res.success) {
      toast('Restore job queued', 'success');
      pollBackupJob(res.data.job_id, 'Backup restore');
    } else {
      toast('Error: ' + (res.error || 'Restore failed'), 'error');
      backupSubmitting = false;
    }
  } catch(e) {
    toast(backupErrorMessage(e, 'Backup restore failed'), 'error');
    backupSubmitting = false;
  }
}

async function downloadBackup(id, encodedFilename) {
  try {
    const headers = {};
    const token = getSessionToken();
    if (token) headers['X-Session-Token'] = token;
    const res = await fetch('/ui-api/api/backups/' + Number(id) + '/download', {headers});
    if (!res.ok) throw new Error('Download failed with HTTP ' + res.status);
    const blob = await res.blob();
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = encodedFilename ? decodeURIComponent(encodedFilename) : ('backup-' + Number(id) + '.tar.gz');
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(url);
  } catch(e) {
    toast(e.message || 'Backup download failed', 'error');
  }
}

async function removeBackup(id) {
  if (!confirm('Remove backup?')) return;
  try {
    const res = await apiPost('/api/backups/remove', {id});
    if (res.success) {
      toast('Backup removed', 'success');
      loadBackups($('page'), null, activeBackupsLifecycle);
    } else {
      toast('Error: ' + (res.error || 'Remove failed'), 'error');
    }
  } catch(e) {
    if (e.status) {
      toast('Error: ' + e.message, 'error');
    } else {
      toast('Network error', 'error');
    }
  }
}

const backupsPage = { mount: loadBackups, unmount() { activeBackupsLifecycle = null; backupSubmitting = false; } };
export { loadBackups, backupsPage };
Object.assign(window, { showBackupModal, createBackup, showRestoreBackupModal, restoreBackup, downloadBackup, removeBackup });
