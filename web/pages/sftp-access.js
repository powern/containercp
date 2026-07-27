import {
  api, apiPost, badge, buildTable, esc, healthBadge, hideModal, loadingState,
  pageHeader, showModal, statusBadge, summaryCards, tb, toast
} from '../core/context.js';

let activeLifecycle = null;
const state = { users: [], selected: null, selectedDetail: null, status: null, reconciling: false };

function load() {
  activeLifecycle = lifecycle;
  state.selected = null; state.selectedDetail = null;
  renderStatus();
  renderUsers();
}

/* ===== STATUS ===== */
async function renderStatus() {
  const p = $('sftp-status');
  if (!p) return;
  try {
    const res = await api('/api/access/sftp/status');
    state.status = res.data;
    if (!state.status) { p.innerHTML = '<div class="empty-state ui-state-error" role="alert">Empty status response</div>'; return; }
    const st = state.status.runtimeState || 'unknown';
    const healthy = st === 'healthy';
    const reconf = state.status.reconciliation || '';
    let errorsHtml = '';
    if (state.status.errors && state.status.errors.length) {
      errorsHtml = '<details style="margin-top:8px;"><summary style="cursor:pointer;font-size:12px;">Errors (' + state.status.errors.length + ')</summary><div style="font-size:11px;color:var(--text3);margin-top:4px;">' + state.status.errors.map(e => '<div>&bull; ' + esc(e) + '</div>').join('') + '</div></details>';
    }
    p.innerHTML = '<div class="ui-summary-grid">'
      + '<div class="ui-summary-card ' + (healthy ? 'healthy' : st === 'degraded' ? 'warning' : 'critical') + '"><div class="ui-summary-label">Runtime</div><div class="ui-summary-value">' + esc(st) + '</div></div>'
      + '<div class="ui-summary-card neutral"><div class="ui-summary-label">Inspected</div><div class="ui-summary-value">' + (state.status.recordsInspected ?? 0) + '</div></div>'
      + '<div class="ui-summary-card healthy"><div class="ui-summary-label">Fixed</div><div class="ui-summary-value">' + (state.status.recordsFixed ?? 0) + '</div></div>'
      + '<div class="ui-summary-card ' + ((state.status.recordsFailed || 0) > 0 ? 'critical' : 'neutral') + '"><div class="ui-summary-label">Failed</div><div class="ui-summary-value">' + (state.status.recordsFailed ?? 0) + '</div></div>'
      + '</div>'
      + (state.status.unsafeForeignStateDetected ? '<div class="ui-callout ui-callout-warning" role="alert" style="margin-top:8px;">Unsafe foreign state detected. Review mounts and run reconcile.</div>' : '')
      + errorsHtml
      + '<div style="margin-top:8px;"><button class="btn btn-sm" onclick="reconcileSftp()" id="reconcile-btn" ' + (state.reconciling ? 'disabled' : '') + '>' + (state.reconciling ? 'Running...' : 'Reconcile') + '</button></div>';
    if (typeof window.renderSftpStatusTable === 'function') window.renderSftpStatusTable();
  } catch(e) {
    p.innerHTML = '<div class="empty-state ui-state-error" role="alert">Failed to load SFTP status: ' + esc(e.message) + '</div>';
  }
}

async function reconcile() {
  if (state.reconciling) return;
  state.reconciling = true;
  const btn = $('reconcile-btn');
  if (btn) btn.disabled = true;
  try {
    const res = await apiPost('/api/access/sftp/reconcile');
    toast('Reconciliation completed', 'success');
  } catch(e) {
    if (e.status === 409) { toast('Reconciliation already running', 'warning'); }
    else { toast('Reconciliation failed: ' + (e.api_message || e.message), 'error'); }
  }
  state.reconciling = false;
  if (btn) btn.disabled = false;
  renderStatus();
  renderUsers();
}

/* ===== USERS ===== */
async function renderUsers() {
  const p = $('sftp-users');
  if (!p) return;
  try {
    const res = await api('/api/access/sftp/users');
    state.users = res.data || [];
    p.innerHTML = '';
    const tbl = p;
    const rows = state.users;
    const lifecycleBadge = (u) => {
      const ls = (u.lifecycleState || 'none').toLowerCase();
      const cls = ls === 'active' ? 'badge-ok' : ls === 'error' ? 'badge-err' : ls === 'provisioning' ? 'badge-warn' : ls === 'removing' ? 'badge-warn' : 'badge-info';
      return '<span class="badge ' + cls + '">' + esc(ls) + '</span>';
    };
    const renderActions = (u) => {
      let acts = '<button class="btn btn-sm" onclick="selectSftpUser(' + u.id + ')" title="Manage user">Manage</button>';
      if (u.lifecycleState === 'error') acts += '<button class="btn btn-sm" onclick="retrySftpUser(' + u.id + ')" style="margin-left:4px;">Retry</button>';
      acts += '<button class="btn btn-sm" onclick="toggleSftpUser(' + u.id + ')" style="margin-left:4px;">' + (u.enabled ? 'Disable' : 'Enable') + '</button>';
      acts += '<button class="btn btn-sm" style="margin-left:4px;color:var(--red);" onclick="deleteSftpUser(' + u.id + ')">Delete</button>';
      return acts;
    };
    const html = '<div class="table-toolbar"><div style="font-weight:600;font-size:14px;">SFTP Users</div><div><button class="btn btn-primary btn-sm" onclick="showCreateSftpUser()">+ Create User</button></div></div>'
      + buildTable([
        {label:'Username', html: r => esc(r.username)},
        {label:'Linux User', html: r => esc(r.linuxUsername || '-')},
        {label:'Status', html: r => lifecycleBadge(r)},
        {label:'Enabled', html: r => r.enabled ? '<span class="badge badge-ok">Yes</span>' : '<span class="badge badge-info">No</span>'},
        {label:'SSH Keys', html: r => String(r.keyCount ?? 0)},
        {label:'Grants', html: r => String(r.grantCount ?? 0)},
        {label:'Last Error', html: r => esc(r.lastError || '')},
        {label:'Actions', html: r => renderActions(r)}
      ], rows, 'No SFTP users. Create one to get started.');
    tbl.innerHTML = html;
  } catch(e) {
    p.innerHTML = '<div class="empty-state ui-state-error" role="alert">Failed to load users: ' + esc(e.message) + '</div>';
  }
}

/* ===== SELECT USER ===== */
async function selectUser(id) {
  state.selected = id;
  try {
    const res = await api('/api/access/sftp/users/' + id);
    state.selectedDetail = res.data;
  } catch(e) {
    state.selectedDetail = null;
    toast('Failed to load user details: ' + e.message, 'error');
  }
  renderDetail();
}

/* ===== DETAIL PANEL ===== */
function renderDetail() {
  const p = $('sftp-detail');
  if (!p) return;
  const u = state.selectedDetail;
  if (!u) {
    p.innerHTML = '<div class="empty-state">Select a user to view details</div>';
    return;
  }
  const actionsHtml = '<button class="btn btn-sm" onclick="toggleSftpUser(' + u.id + ')">' + (u.enabled ? 'Disable' : 'Enable') + '</button>'
    + '<button class="btn btn-sm" onclick="retrySftpUser(' + u.id + ')" style="margin-left:4px;">Retry</button>'
    + '<button class="btn btn-sm" style="margin-left:4px;color:var(--red);" onclick="deleteSftpUser(' + u.id + ')">Delete</button>';
  p.innerHTML = '<div class="card" style="margin-bottom:12px;"><div style="padding:16px;">'
    + '<div style="font-weight:600;font-size:14px;margin-bottom:8px;">' + esc(u.username) + '</div>'
    + '<div style="display:grid;gap:6px;font-size:13px;">'
    + '<div><strong>Linux user:</strong> ' + esc(u.linuxUsername || '-') + '</div>'
    + '<div><strong>Lifecycle:</strong> ' + statusBadge(u.lifecycleState || 'none') + '</div>'
    + '<div><strong>Enabled:</strong> ' + (u.enabled ? 'Yes' : 'No') + '</div>'
    + '<div><strong>SSH Keys:</strong> ' + (u.keyCount ?? 0) + '</div>'
    + '<div><strong>Grants:</strong> ' + (u.grantCount ?? 0) + '</div>'
    + (u.lastError ? '<div><strong>Last error:</strong> ' + esc(u.lastError) + '</div>' : '')
    + '<details style="margin-top:6px;"><summary style="cursor:pointer;font-size:12px;">Advanced</summary><div style="font-size:12px;color:var(--text3);margin-top:4px;">'
    + '<div><strong>Home:</strong> ' + esc(u.home || '-') + '</div>'
    + '</div></details>'
    + '</div><div style="margin-top:12px;">' + actionsHtml + '</div>'
    // Quick start checklist
    + '<div style="margin-top:12px;padding:12px;background:var(--bg2);border-radius:8px;font-size:12px;">'
    + '<div style="font-weight:600;margin-bottom:6px;">Setup checklist</div>'
    + '<div>' + (u.lifecycleState === 'active' ? '✓' : '○') + ' User provisioned</div>'
    + '<div>' + ((u.keyCount || 0) > 0 ? '✓' : '○') + ' <button class="btn-link" onclick="showAddSftpKey()" style="padding:0;font-size:12px;">Add SSH key</button></div>'
    + '<div>' + ((u.grantCount || 0) > 0 ? '✓' : '○') + ' <button class="btn-link" onclick="showAddSftpGrant()" style="padding:0;font-size:12px;">Grant site access</button></div>'
    + '<div>' + (u.linuxUsername ? '✓' : '○') + ' Test SFTP login: <code style="font-size:11px;">sftp ' + esc(u.linuxUsername || 'user') + '@host</code></div>'
    + '</div>'
    + '</div></div>';
  renderKeys();
  renderGrants();
}

/* ===== KEYS ===== */
async function renderKeys() {
  const p = $('sftp-keys');
  if (!p || !state.selected) { if (p) p.innerHTML = ''; return; }
  try {
    const res = await api('/api/access/sftp/users/' + state.selected + '/keys');
    const keys = res.data || [];
    const html = '<div class="table-toolbar"><div style="font-weight:600;font-size:13px;">SSH Keys</div><div><button class="btn btn-sm btn-primary" onclick="showAddSftpKey()">+ Add Key</button>'
      + '<button class="btn btn-sm" onclick="rebuildSftpKeys()" style="margin-left:4px;">Rebuild</button></div></div>'
      + buildTable([
        {label:'Type', html: k => esc(k.keyType || '-')},
        {label:'Fingerprint', html: k => '<code style="font-size:11px;">' + esc(k.fingerprint || '') + '</code>'},
        {label:'Comment', html: k => esc(k.comment || '-')},
        {label:'Enabled', html: k => k.enabled ? '<span class="badge badge-ok">Yes</span>' : '<span class="badge badge-info">No</span>'},
        {label:'Actions', html: k => '<button class="btn btn-sm" onclick="toggleSftpKey(' + k.id + ')">' + (k.enabled ? 'Disable' : 'Enable') + '</button><button class="btn btn-sm" style="margin-left:4px;color:var(--red);" onclick="deleteSftpKey(' + k.id + ')">Delete</button>'}
      ], keys, 'No SSH keys');
    p.innerHTML = html;
  } catch(e) {
    p.innerHTML = '<div class="empty-state" role="alert">Failed to load keys: ' + esc(e.message) + '</div>';
  }
}

async function rebuildKeys() {
  if (!state.selected) return;
  try {
    const res = await apiPost('/api/access/sftp/users/' + state.selected + '/keys/rebuild');
    toast('Authorized keys rebuilt', 'success');
    renderKeys();
  } catch(e) {
    toast('Rebuild failed: ' + (e.api_message || e.message), 'error');
  }
}

/* ===== GRANTS ===== */
async function renderGrants() {
  const p = $('sftp-grants');
  if (!p || !state.selected) { if (p) p.innerHTML = ''; return; }
  try {
    const res = await api('/api/access/sftp/users/' + state.selected + '/grants');
    const grants = res.data || [];
    const permBadge = (p) => {
      if (p === 'read_write') return '<span class="badge badge-ok">RW</span>';
      if (p === 'read_only') return '<span class="badge badge-info">RO</span>';
      return '<span class="badge badge-info">' + esc(p) + '</span>';
    };
    const html = '<div class="table-toolbar"><div style="font-weight:600;font-size:13px;">Site Grants</div><div><button class="btn btn-sm btn-primary" onclick="showAddSftpGrant()">+ Add Grant</button></div></div>'
      + buildTable([
        {label:'Site', html: g => esc(g.domain || 'Site #' + g.siteId)},
        {label:'Permission', html: g => permBadge(g.permission)},
        {label:'Actions', html: g => '<button class="btn btn-sm" onclick="showChangeSftpGrantPermission(' + g.siteId + ')">Change</button><button class="btn btn-sm" style="margin-left:4px;color:var(--red);" onclick="revokeSftpGrant(' + g.siteId + ')">Revoke</button><button class="btn btn-sm" style="margin-left:4px;" onclick="retrySftpGrant(' + g.siteId + ')">Retry</button>'}
      ], grants, 'No grants');
    p.innerHTML = html;
  } catch(e) {
    p.innerHTML = '<div class="empty-state" role="alert">Failed to load grants: ' + esc(e.message) + '</div>';
  }
}

/* ===== CREATE USER ===== */
function showCreateUser() {
  showModal('Create SFTP User',
    '<form id="create-sftp-form" onsubmit="event.preventDefault();doCreateSftpUser()">'
    + '<div style="margin-bottom:12px;"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Username</label><input id="cu-username" required maxlength="64" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;"></div>'
    + '<div style="margin-bottom:12px;"><label style="display:flex;align-items:center;gap:8px;font-size:13px;"><input id="cu-enabled" type="checkbox" checked> Enable immediately</label></div>'
    + '<div id="cu-error" style="color:var(--red);font-size:12px;display:none;"></div>'
    + '<button class="btn btn-primary" type="submit" id="cu-submit">Create</button>'
    + '<button class="btn" type="button" style="margin-left:8px;" onclick="hideModal()">Cancel</button>'
    + '</form>');
}

async function doCreateUser() {
  const btn = $('cu-submit'); if (btn) btn.disabled = true;
  const errEl = $('cu-error');
  const un = $('cu-username').value.trim();
  if (!un) { if (errEl) { errEl.textContent = 'Username is required'; errEl.style.display = 'block'; } if (btn) btn.disabled = false; return; }
  try {
    const res = await apiPost('/api/access/sftp/users', { username: un, enabled: $('cu-enabled').checked });
    hideModal();
    toast('User created: ' + un, 'success');
    renderUsers();
    if (res.data && res.data.id) selectUser(res.data.id);
  } catch(e) {
    if (errEl) { errEl.textContent = e.api_message || e.message || e.body?.error || 'Creation failed'; errEl.style.display = 'block'; }
    if (btn) btn.disabled = false;
  }
}

/* ===== TOGGLE USER ===== */
async function toggleUser(id) {
  const u = state.users.find(u => u.id === id);
  if (!u) return;
  if (u.enabled && !confirm('Disable this user? They will lose SFTP access.')) return;
  try {
    await apiPost('/api/access/sftp/users/' + id, { enabled: !u.enabled }, 'PATCH');
    toast(u.enabled ? 'User disabled' : 'User enabled', 'success');
    renderUsers();
    if (state.selected === id) selectUser(id);
  } catch(e) {
    toast('Failed: ' + (e.api_message || e.message), 'error');
  }
}

/* ===== DELETE USER ===== */
function confirmDeleteUser(id) {
  const u = state.users.find(u => u.id === id);
  if (!u) return;
  showModal('Delete User',
    '<p style="font-size:13px;margin-bottom:12px;">Are you sure you want to delete <strong>' + esc(u.username) + '</strong>? This will:</p>'
    + '<ul style="font-size:13px;margin-bottom:12px;padding-left:20px;"><li>Remove the Linux account</li><li>Clean up authorized_keys</li><li>Remove all grants and mounts</li></ul>'
    + '<div id="du-error" style="color:var(--red);font-size:12px;display:none;"></div>'
    + '<button class="btn" style="background:var(--red);color:#fff;" onclick="doDeleteSftpUser(' + id + ')" id="du-submit">Delete</button>'
    + '<button class="btn" style="margin-left:8px;" onclick="hideModal()">Cancel</button>');
}

async function doDeleteUser(id) {
  const btn = $('du-submit'); if (btn) btn.disabled = true;
  try {
    await apiPost('/api/access/sftp/users/' + id, {}, 'DELETE');
    hideModal();
    toast('User deleted', 'success');
    if (state.selected === id) { state.selected = null; state.selectedDetail = null; renderDetail(); }
    renderUsers();
  } catch(e) {
    const errEl = $('du-error');
    if (errEl) { errEl.textContent = e.api_message || e.message || 'Delete failed'; errEl.style.display = 'block'; }
    if (btn) btn.disabled = false;
  }
}

/* ===== RETRY USER ===== */
async function retryUser(id) {
  try {
    await apiPost('/api/access/sftp/users/' + id + '/retry');
    toast('Retry initiated', 'success');
    renderUsers();
    if (state.selected === id) selectUser(id);
  } catch(e) {
    toast('Retry failed: ' + (e.api_message || e.message), 'error');
  }
}

/* ===== ADD KEY ===== */
function showAddKey() {
  showModal('Add SSH Key',
    '<div style="margin-bottom:12px;"><label style="font-weight:600;font-size:13px;">Option A: Generate new key pair</label></div>'
    + '<form id="add-key-form-gen" onsubmit="event.preventDefault();doGenerateSftpKey()">'
    + '<div style="margin-bottom:8px;"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Key type</label><select id="ak-type" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;">'
    + '<option value="ed25519">ED25519 (recommended)</option><option value="rsa">RSA 4096</option></select></div>'
    + '<div style="margin-bottom:8px;"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Comment</label><input id="ak-comment-gen" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;" placeholder="user@hostname"></div>'
    + '<div style="margin-bottom:8px;"><label style="display:flex;align-items:center;gap:8px;font-size:13px;"><input id="ak-enabled-gen" type="checkbox" checked> Enabled</label></div>'
    + '<button class="btn btn-primary" type="submit">Generate and Add</button>'
    + '</form>'
    + '<hr style="margin:16px 0;border:none;border-top:1px solid var(--border);">'
    + '<div style="margin-bottom:12px;"><label style="font-weight:600;font-size:13px;">Option B: Import existing public key</label></div>'
    + '<form id="add-key-form" onsubmit="event.preventDefault();doAddSftpKey()">'
    + '<div style="margin-bottom:8px;"><label for="ak-key" style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Public Key</label><textarea id="ak-key" rows="3" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:12px;font-family:monospace;" placeholder="ssh-ed25519 AAAA..."></textarea></div>'
    + '<div style="margin-bottom:8px;"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Comment</label><input id="ak-comment" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;" placeholder="my-laptop"></div>'
    + '<div style="margin-bottom:8px;"><label style="display:flex;align-items:center;gap:8px;font-size:13px;"><input id="ak-enabled" type="checkbox" checked> Enabled</label></div>'
    + '<div id="ak-error" style="color:var(--red);font-size:12px;display:none;"></div>'
    + '<button class="btn btn-primary" type="submit" id="ak-submit">Add Key</button>'
    + '<button class="btn" type="button" style="margin-left:8px;" onclick="hideModal()">Cancel</button>'
    + '</form>');
}

async function doGenerateKey() {
  if (!state.selected) return;
  const keyType = $('ak-type').value;
  const comment = $('ak-comment-gen').value.trim();
  try {
    // Generate key via SSH backend
    const genRes = await apiPost('/api/access/sftp/users/' + state.selected + '/keys/gen', { type: keyType, comment: comment, enabled: $('ak-enabled-gen').checked });
    hideModal();
    if (genRes.data && genRes.data.privateKey) {
      const fp = genRes.data.fingerprint || '';
      showModal('SSH Key Generated',
        '<div style="margin-bottom:12px;"><strong>Public Key Fingerprint:</strong> <code>' + esc(fp) + '</code></div>'
        + '<div style="margin-bottom:12px;"><label style="font-size:12px;">Public Key</label><textarea readonly rows="3" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:11px;font-family:monospace;">' + esc(genRes.data.publicKey) + '</textarea></div>'
        + '<button class="btn btn-sm" onclick="copyText(\'' + esc(genRes.data.publicKey) + '\')">Copy Public Key</button>'
        + '<div style="margin-top:12px;margin-bottom:8px;"><label style="font-size:12px;"><strong>Private Key</strong> (shown only once)</label><textarea readonly rows="6" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:11px;font-family:monospace;">' + esc(genRes.data.privateKey) + '</textarea></div>'
        + '<div style="background:var(--bg2);padding:8px 12px;border-radius:6px;font-size:12px;color:var(--red);margin-bottom:8px;">⚠ The private key is shown only once. Save it now.</div>'
        + '<button class="btn btn-sm" onclick="downloadSftpKey(\'' + esc(genRes.data.filename || 'id_' + keyType) + '\',\'' + esc(genRes.data.privateKey) + '\')">Download Private Key</button>'
        + '<button class="btn btn-sm" style="margin-left:8px;" onclick="hideModal()">Done</button>');
    }
    renderKeys();
    renderDetail();
  } catch(e) {
    toast('Key generation failed: ' + (e.api_message || e.message), 'error');
  }
}

function downloadKey(filename, content) {
  const blob = new Blob([content], { type: 'application/octet-stream' });
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob);
  a.download = filename;
  a.click();
  URL.revokeObjectURL(a.href);
}

async function doAddKey() {
  if (!state.selected) return;
  const btn = $('ak-submit'); if (btn) btn.disabled = true;
  const errEl = $('ak-error');
  const pk = $('ak-key').value.trim();
  if (!pk) { if (errEl) { errEl.textContent = 'Public key is required'; errEl.style.display = 'block'; } if (btn) btn.disabled = false; return; }
  try {
    const res = await apiPost('/api/access/sftp/users/' + state.selected + '/keys', {
      publicKey: pk, comment: $('ak-comment').value.trim(), enabled: $('ak-enabled').checked
    });
    hideModal();
    if (res.data && res.data.fingerprint) { toast('Key added: ' + res.data.fingerprint, 'success'); }
    else { toast('Key added', 'success'); }
    renderKeys();
    renderDetail();
  } catch(e) {
    if (e.status === 409) { if (errEl) { errEl.textContent = 'Duplicate key — this key already exists for this user'; errEl.style.display = 'block'; } }
    else if (e.status === 422) { if (errEl) { errEl.textContent = e.api_message || e.message || 'Invalid key'; errEl.style.display = 'block'; } }
    else { if (errEl) { errEl.textContent = e.api_message || e.message || 'Failed to add key'; errEl.style.display = 'block'; } }
    if (btn) btn.disabled = false;
  }
}

/* ===== TOGGLE KEY ===== */
async function toggleKey(id) {
  if (!state.selected) return;
  try {
    const key = state.selectedDetail?.keys?.find(k => k.id === id);
    await apiPost('/api/access/sftp/users/' + state.selected + '/keys/' + id, { enabled: !(key ? key.enabled : true) }, 'PATCH');
    toast('Key updated', 'success');
    renderKeys();
  } catch(e) {
    toast('Failed: ' + (e.api_message || e.message), 'error');
  }
}

/* ===== DELETE KEY ===== */
async function deleteKey(id) {
  if (!state.selected || !confirm('Delete this SSH key?')) return;
  try {
    await apiPost('/api/access/sftp/users/' + state.selected + '/keys/' + id, {}, 'DELETE');
    toast('Key deleted', 'success');
    renderKeys();
  } catch(e) {
    toast('Failed: ' + (e.api_message || e.message), 'error');
  }
}

/* ===== ADD GRANT ===== */
async function showAddGrant() {
  try {
    const sitesRes = await api('/api/sites');
    const sites = sitesRes.data || [];
    let options = '<option value="">Select site...</option>';
    (sites || []).forEach(s => { options += '<option value="' + s.id + '">' + esc(s.domain || 'Site #' + s.id) + '</option>'; });
    showModal('Add Site Grant',
      '<form id="add-grant-form" onsubmit="event.preventDefault();doAddSftpGrant()">'
      + '<div style="margin-bottom:12px;"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Site</label><select id="ag-site" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;">' + options + '</select></div>'
      + '<div style="margin-bottom:12px;"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Permission</label><select id="ag-perm" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;">'
      + '<option value="ro">Read only</option><option value="rw">Read and write</option></select></div>'
      + '<div id="ag-error" style="color:var(--red);font-size:12px;display:none;"></div>'
      + '<button class="btn btn-primary" type="submit" id="ag-submit">Add Grant</button>'
      + '<button class="btn" type="button" style="margin-left:8px;" onclick="hideModal()">Cancel</button>'
      + '</form>');
  } catch(e) {
    toast('Failed to load sites: ' + e.message, 'error');
  }
}

async function doAddGrant() {
  if (!state.selected) return;
  const btn = $('ag-submit'); if (btn) btn.disabled = true;
  const errEl = $('ag-error');
  const siteId = parseInt($('ag-site').value, 10);
  if (!siteId) { if (errEl) { errEl.textContent = 'Select a site'; errEl.style.display = 'block'; } if (btn) btn.disabled = false; return; }
  const perm = $('ag-perm').value;
  try {
    await apiPost('/api/access/sftp/users/' + state.selected + '/grants', { siteId, permission: perm });
    hideModal();
    toast('Grant added', 'success');
    renderGrants();
  } catch(e) {
    if (e.status === 409) { if (errEl) { errEl.textContent = 'Grant already exists for this site'; errEl.style.display = 'block'; } }
    else { if (errEl) { errEl.textContent = e.api_message || e.message || 'Failed to add grant'; errEl.style.display = 'block'; } }
    if (btn) btn.disabled = false;
  }
}

/* ===== CHANGE GRANT PERMISSION ===== */
function showChangeGrantPermission(siteId) {
  if (!state.selected) return;
  const grant = (state.selectedDetail?.grants || []).find(g => g.siteId === siteId);
  showModal('Change Permission',
    '<form id="ch-grant-form" onsubmit="event.preventDefault();doChangeSftpGrantPermission(' + siteId + ')">'
    + '<div style="margin-bottom:12px;"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">New Permission</label>'
    + '<select id="ch-perm" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;">'
    + '<option value="ro" ' + (grant?.permission === 'read_only' ? 'selected' : '') + '>Read only</option>'
    + '<option value="rw" ' + (grant?.permission === 'read_write' ? 'selected' : '') + '>Read and write</option></select></div>'
    + '<div id="ch-error" style="color:var(--red);font-size:12px;display:none;"></div>'
    + '<button class="btn btn-primary" type="submit">Change</button>'
    + '<button class="btn" type="button" style="margin-left:8px;" onclick="hideModal()">Cancel</button>'
    + '</form>');
}

async function doChangeGrantPermission(siteId) {
  if (!state.selected) return;
  try {
    await apiPost('/api/access/sftp/users/' + state.selected + '/grants/' + siteId, { permission: $('ch-perm').value }, 'PATCH');
    hideModal();
    toast('Permission updated', 'success');
    renderGrants();
  } catch(e) {
    const errEl = $('ch-error');
    if (errEl) { errEl.textContent = e.api_message || e.message || 'Failed to update'; errEl.style.display = 'block'; }
  }
}

/* ===== REVOKE GRANT ===== */
async function revokeGrant(siteId) {
  if (!state.selected || !confirm('Revoke this grant? The user will lose access to this site.')) return;
  try {
    await apiPost('/api/access/sftp/users/' + state.selected + '/grants/' + siteId, {}, 'DELETE');
    toast('Grant revoked', 'success');
    renderGrants();
  } catch(e) {
    toast('Failed: ' + (e.api_message || e.message), 'error');
  }
}

/* ===== RETRY GRANT ===== */
async function retryGrant(siteId) {
  if (!state.selected) return;
  try {
    await apiPost('/api/access/sftp/users/' + state.selected + '/grants/' + siteId + '/retry');
    toast('Grant retry initiated', 'success');
    renderGrants();
  } catch(e) {
    toast('Retry failed: ' + (e.api_message || e.message), 'error');
  }
}

/* ===== PAGE MOUNT ===== */
let lifecycle = null;

function mount(p, params, lc) {
  lifecycle = lc;
  activeLifecycle = lc;
  p.innerHTML = pageHeader('SFTP Access', 'Manage SFTP users, SSH keys, and Site grants.', '<button class="btn btn-primary btn-sm" onclick="showCreateSftpUser()">+ Create User</button>', 'Admin')
    + '<div id="sftp-status" style="margin-bottom:16px;">' + loadingState('Loading runtime status...') + '</div>'
    + '<div style="display:grid;gap:16px;grid-template-columns:1fr 1fr;">'
    + '<div><div id="sftp-users">' + loadingState('Loading users...') + '</div></div>'
    + '<div><div id="sftp-detail"><div class="empty-state">Select a user to view details</div></div>'
    + '<div style="margin-top:12px;"><div style="font-weight:600;font-size:13px;margin-bottom:8px;">SSH Keys</div><div id="sftp-keys"></div></div>'
    + '<div style="margin-top:12px;"><div style="font-weight:600;font-size:13px;margin-bottom:8px;">Site Grants</div><div id="sftp-grants"></div></div>'
    + '</div></div>';
  renderStatus();
  renderUsers();
}

function unmount() { activeLifecycle = null; lifecycle = null; state.selected = null; state.selectedDetail = null; }

const sftpAccessPage = { mount, unmount };
export { sftpAccessPage, mount as loadSftpAccess };
Object.assign(window, {
  showCreateSftpUser: showCreateUser, doCreateSftpUser: doCreateUser,
  selectSftpUser: selectUser, toggleSftpUser: toggleUser, deleteSftpUser: confirmDeleteUser, doDeleteSftpUser: doDeleteUser, retrySftpUser: retryUser,
  reconcileSftp: reconcile,
  showAddSftpKey: showAddKey, doAddSftpKey: doAddKey,
  doGenerateSftpKey: doGenerateKey, downloadSftpKey: downloadKey,
  toggleSftpKey: toggleKey, deleteSftpKey: deleteKey, rebuildSftpKeys: rebuildKeys,
  showAddSftpGrant: showAddGrant, doAddSftpGrant: doAddGrant,
  showChangeSftpGrantPermission: showChangeGrantPermission, doChangeSftpGrantPermission: doChangeGrantPermission,
  revokeSftpGrant: revokeGrant, retrySftpGrant: retryGrant
});
