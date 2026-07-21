import {
  api, apiPost, card, esc, pageHeader, summaryCards, toast
} from '../core/context.js';


async function loadSettings(p) {
  let settings = {version:'...', server_hostname:''};
  try {
    const res = await api('/api/settings');
    if (res.success) settings = res.data;
  } catch(e) {}
  const host = esc(settings.server_hostname || '');

  p.innerHTML = `${pageHeader('Settings', 'Control panel settings, admin HTTPS, and password management.', '', 'Administration')}
    ${summaryCards([
      {label:'Version', value:'v' + (settings.version || 'unknown'), tone:'neutral', help:'ContainerCP version'},
      {label:'Hostname', value:settings.server_hostname ? 'Set' : 'Missing', tone:settings.server_hostname ? 'healthy' : 'warning', help:'Admin panel hostname'},
      {label:'Theme', value:'Dark', tone:'info', help:'Current UI preference'}
    ])}
    <div class="details-panel"><div class="details-grid">
      <div class="details-field"><div class="details-label">Version</div><div class="details-value">v${esc(settings.version)}</div></div>
      <div class="details-field"><div class="details-label">Data Root</div><div class="details-value">/srv/containercp</div></div>
      <div class="details-field"><div class="details-label">Theme</div><div class="details-value" id="themeValue">Dark</div></div>
    </div></div>
    <div class="card" style="margin-top:16px">
      <h3>Admin Panel HTTPS</h3>
      <div style="padding:12px">
        <label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Server Hostname</label>
        <input id="srv-hostname" value="${host}" placeholder="admin.example.com" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
        <div style="margin-top:8px;display:flex;gap:8px;flex-wrap:wrap">
          <button class="btn btn-primary btn-sm" onclick="saveHostname()">Save Hostname</button>
          <button class="btn btn-sm" onclick="issueAdminSsl()">Issue SSL</button>
          <button class="btn btn-sm" onclick="renewAdminSsl()">Renew SSL</button>
        </div>
        <div id="admin-ssl-status" style="margin-top:8px;font-size:12px;color:var(--text3);"></div>
      </div>
    </div>
    <div class="card" style="margin-top:16px">
      <h3>Change Password</h3>
      <div style="padding:12px">
        <div style="margin-bottom:8px">
          <label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Current Password</label>
          <input id="cp-old-pass" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
        </div>
        <div style="margin-bottom:8px">
          <label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">New Password</label>
          <input id="cp-new-pass" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
        </div>
        <div style="margin-bottom:8px">
          <label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Confirm New Password</label>
          <input id="cp-confirm-pass" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
        </div>
        <button class="btn btn-primary btn-sm" onclick="changeAdminPassword()">Change Password</button>
        <div id="cp-status" style="margin-top:8px;font-size:12px;color:var(--text3);"></div>
      </div>
    </div>`;
}

async function saveHostname() {
  const h = $('srv-hostname').value.trim();
  if (!h) { toast('Enter a hostname', 'error'); return; }
  try {
    const res = await apiPost('/api/settings', {server_hostname: h});
    if (res.success) { toast('Hostname saved: ' + h, 'success'); }
    else { toast('Error: ' + (res.error || 'Save failed'), 'error'); }
  } catch(e) { toast('Network error', 'error'); }
}

async function issueAdminSsl() {
  const h = $('srv-hostname').value.trim();
  if (!h) { toast('Save a hostname first', 'error'); return; }
  const status = $('admin-ssl-status');
  status.textContent = 'Issuing certificate for ' + h + '...';
  try {
    const res = await apiPost('/api/ssl/' + encodeURIComponent(h) + '/issue', {provider_id:'letsencrypt'});
    status.textContent = res.data && res.data.job_id ? 'Job queued (ID: ' + res.data.job_id + ')' : 'Certificate issued';
    if (res.success) toast('SSL issuance queued', 'success');
    else toast('Error: ' + ((res.error && res.error.message) || res.error), 'error');
  } catch(e) { toast('Network error', 'error'); status.textContent = ''; }
}

async function renewAdminSsl() {
  const h = $('srv-hostname').value.trim();
  if (!h) { toast('Save a hostname first', 'error'); return; }
  try {
    const res = await apiPost('/api/ssl/' + encodeURIComponent(h) + '/renew', {});
    if (res.success) toast('Renewal queued', 'success');
    else toast('Error: ' + ((res.error && res.error.message) || res.error), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function changeAdminPassword() {
  const oldP = $('cp-old-pass').value;
  const newP = $('cp-new-pass').value;
  const confirmP = $('cp-confirm-pass').value;
  const status = $('cp-status');
  if (!oldP || !newP || !confirmP) { status.textContent = 'Fill in all fields'; return; }
  if (newP !== confirmP) { status.textContent = 'New passwords do not match'; return; }
  if (newP.length < 4) { status.textContent = 'Password too short (min 4 chars)'; return; }
  status.textContent = 'Changing password...';
  try {
    const res = await apiPost('/auth/change-password', {old_password: oldP, new_password: newP});
    if (res.success) {
      status.textContent = '';
      $('cp-old-pass').value = '';
      $('cp-new-pass').value = '';
      $('cp-confirm-pass').value = '';
      toast('Password changed successfully', 'success');
    } else {
      status.textContent = res.error || 'Failed to change password';
    }
  } catch(e) { status.textContent = 'Network error'; }
}

const settingsPage = { mount: loadSettings };
export { loadSettings, settingsPage };
Object.assign(window, { saveHostname, issueAdminSsl, renewAdminSsl, changeAdminPassword });
