import {
  api, apiPost, buildTable, esc, hideModal, showModal, tb, toast
} from '../core/context.js';


/* ===== BACKUPS ===== */
async function loadBackups(p) {
  try {
    const data = await api('/api/backups');
    p.innerHTML = `<div class="page-header"><h1>Backups</h1><div class="page-actions"><button class="btn btn-primary btn-sm" onclick="showBackupModal()">+ Create Backup</button></div></div>`;
    p.innerHTML += tb('All Backups') + buildTable([
      {label:'Filename',html:r=>esc(r.filename)},{label:'Size',html:r=>esc(r.size)+' bytes'},{label:'Status',html:r=>{let m={completed:'badge-ok',failed:'badge-err'};return `<span class="badge ${m[r.status]||'badge-info'}">${esc(r.status)}</span>`;}},{label:'Date',html:r=>esc(r.created_at)},
      {label:'Actions',html:r=>r.status==='completed'?`<button class="btn-icon" title="Restore" onclick="restoreBackup(${r.id},'${esc(r.filename)}')">&#8635;</button><button class="btn-icon" style="color:var(--red)" onclick="removeBackup(${r.id})">&#10005;</button>`:''}
    ], data.data||[]);
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
      sitesHtml = list.map(s => '<option value="'+esc(s.domain)+'">'+esc(s.domain)+'</option>').join('');
    }
  } catch(e) {
    sitesHtml = '<option value="" disabled selected>Failed to load sites</option>';
  }
  showModal('Create Backup', '<div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Site</label><select id="bk-domain" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">'+sitesHtml+'</select><div style="margin-top:8px;font-size:11px;color:var(--text3);">Or type a domain manually:</div><input id="bk-domain-manual" placeholder="example.com" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;margin-top:4px;"><button class="btn btn-primary" onclick="createBackup()" style="margin-top:12px;">Create Backup</button>', 420);
}

let creatingBackup = false;

async function createBackup() {
  if (creatingBackup) return;
  creatingBackup = true;
  const sel = $('bk-domain');
  const manual = $('bk-domain-manual');
  const domain = (sel && sel.value) ? sel.value : (manual ? manual.value.trim() : '');
  if (!domain) {
    toast('Select a site or enter a domain', 'error');
    creatingBackup = false;
    return;
  }
  hideModal();
  try {
    const res = await apiPost('/api/backups/create',{domain});
    if (res.success) {
      toast('Backup created: '+res.data.filename, 'success');
      loadBackups($('page'));
    } else {
      toast('Error: '+(res.error||'Unknown'), 'error');
    }
  } catch(e) {
    toast('Network error', 'error');
  } finally {
    creatingBackup = false;
  }
}

let restoringBackup = false;

async function restoreBackup(id, filename) {
  if (restoringBackup) return;
  if (!confirm('Restore backup ' + filename + '?')) return;
  restoringBackup = true;
  try {
    const res = await apiPost('/api/backups/restore', {id});
    if (res.success) {
      toast('Backup restored successfully', 'success');
      loadBackups($('page'));
    } else {
      toast('Error: ' + (res.error || 'Restore failed'), 'error');
    }
  } catch(e) {
    if (e.status) {
      toast('Error: ' + e.message, 'error');
    } else {
      toast('Network error', 'error');
    }
  } finally {
    restoringBackup = false;
  }
}

async function removeBackup(id) {
  if (!confirm('Remove backup?')) return;
  try {
    const res = await apiPost('/api/backups/remove', {id});
    if (res.success) {
      toast('Backup removed', 'success');
      loadBackups($('page'));
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

export { loadBackups };
Object.assign(window, { loadBackups, showBackupModal, createBackup, restoreBackup, removeBackup });
