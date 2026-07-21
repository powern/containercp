import {
  api, apiPost, buildTable, card, copyText, esc, hideModal, navigate, showModal, toast
} from '../core/context.js';


/* ===== MAIL ===== */
async function loadMail(p) {
  try {
    // Check module status first
    const status = await api('/api/mail/status');
    const state = status.data?.state || 'inactive';
    const dCount = status.data?.domains ?? 0;
    const mbCount = status.data?.mailboxes ?? 0;
    const aCount = status.data?.aliases ?? 0;

    let html = `<div class="page-header"><h1>Mail</h1><div class="page-actions">`;
    if (state === 'inactive') {
      html += `<button class="btn btn-primary btn-sm" onclick="activateMail()">Activate Mail Module</button>`;
    } else {
      html += `<button class="btn btn-sm" onclick="loadMailHealth($('page'))">Health</button> `;
      html += `<button class="btn btn-sm" onclick="deactivateMail()">Deactivate</button>`;
    }
    html += `</div></div>`;

    // Module state card
    const stateLabel = state === 'active' ? '<span class="badge badge-ok">Active</span>' : '<span class="badge badge-info">Inactive</span>';
    html += `<div class="card" style="margin-bottom:12px;">
      <div class="card-header"><h3>Module Status</h3></div>
      <div style="padding:12px;font-size:13px;">
        <div style="display:flex;gap:24px;flex-wrap:wrap;">
          <div><span style="color:var(--text2)">State:</span> ${stateLabel}</div>
          <div><span style="color:var(--text2)">Domains:</span> <strong>${dCount}</strong></div>
          <div><span style="color:var(--text2)">Mailboxes:</span> <strong>${mbCount}</strong></div>
          <div><span style="color:var(--text2)">Aliases:</span> <strong>${aCount}</strong></div>
        </div>
      </div>
    </div>`;

    if (state === 'active') {
      // List mail domains
      const res = await api('/api/mail/domains');
      const domains = res.data || [];
      html += `<div class="page-header" style="margin-top:8px;"><h3>Mail Domains</h3><div class="page-actions"><button class="btn btn-primary btn-sm" onclick="showCreateMailDomain()">+ Add Domain</button></div></div>`;
      window.renderTable = () => {
        const filtered = domains.filter(r => !window.searchTerm || r.domain.includes(window.searchTerm));
        html += buildTable([
          {label:'Domain',html:r=>`<a href="#" onclick="navigate('mail-domain',${r.id});return false" style="color:var(--primary);text-decoration:none;">${esc(r.domain)}</a>`},
          {label:'Mode',html:r=>`<span class="badge badge-info">${esc(r.mode)}</span>`},
          {label:'Mailboxes',html:r=>esc(r.max_mailboxes||'∞')},
          {label:'Relay',html:r=>esc(r.relay_host||'-')},
          {label:'DKIM',html:r=>r.dkim_public_key_dns ? '<span class="badge badge-ok">✓</span>' : '<span class="badge badge-info">—</span>'},
          {label:'Actions',html:r=>`<button class="btn-icon" onclick="navigate('mail-domain',${r.id})" title="View">&#128065;</button><button class="btn-icon" style="color:var(--red)" title="Remove" onclick="removeMailDomain(${r.id})">&#10005;</button>`}
        ], filtered, 'No mail domains');
      };
      window.renderTable();
    }
    p.innerHTML = html;
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load mail</div>'; }
}

async function activateMail() {
  try {
    const res = await apiPost('/api/mail/activate', {});
    if (res.success) { toast('Mail module activated', 'success'); navigate('mail'); }
    else toast('Error: ' + (res.error||'Activation failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function deactivateMail() {
  if (!confirm('Deactivate mail module? This will stop containers.')) return;
  try {
    const res = await apiPost('/api/mail/deactivate', {});
    if (res.success) { toast('Mail module deactivated', 'success'); navigate('mail'); }
    else toast('Error: ' + (res.error||'Deactivation failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

function showCreateMailDomain() {
  // Fetch existing ContainerCP domains for the dropdown
  api('/api/domains').then(res => {
    const domains = res.data || [];
    // Store for onchange handler
    window._mailDomains = domains;
    let domainOptions = '<option value="0">External (no site)</option>';
    for (const d of domains) {
      domainOptions += `<option value="${d.id}">${esc(d.domain)} (site #${d.site_id})</option>`;
    }
    showModal('Add Mail Domain', `
      <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Domain Name</label><input id="md-domain" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" placeholder="example.com"></div>
      <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Mode</label>
        <select id="md-mode" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
          <option value="local-primary">Local Primary</option>
          <option value="external-relay">External Relay</option>
          <option value="split-m365">Split M365</option>
          <option value="disabled">Disabled</option>
        </select></div>
      <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Linked ContainerCP Domain</label>
        <select id="md-domain-id" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" onchange="onMailDomainSelectChange(this)">${domainOptions}</select></div>
      <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Relay Host (for external-relay/split-m365)</label><input id="md-relay" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" placeholder="smtp.example.com"></div>
      <button class="btn btn-primary btn-sm" onclick="createMailDomain()" style="margin-top:8px;">Create</button>
    `);
  }).catch(() => {
    // Fallback if domains API fails
    showCreateMailDomainSimple();
  });
}

function showCreateMailDomainSimple() {
  showModal('Add Mail Domain', `
    <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Domain Name</label><input id="md-domain" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" placeholder="example.com"></div>
    <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Mode</label>
      <select id="md-mode" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
        <option value="local-primary">Local Primary</option>
        <option value="external-relay">External Relay</option>
        <option value="split-m365">Split M365</option>
        <option value="disabled">Disabled</option>
      </select></div>
    <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Domain ID (from ContainerCP sites, 0 = external)</label><input id="md-domain-id" type="number" value="0" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
    <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Relay Host (for external-relay/split-m365)</label><input id="md-relay" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" placeholder="smtp.example.com"></div>
    <button class="btn btn-primary btn-sm" onclick="createMailDomain()" style="margin-top:8px;">Create</button>
  `);
}

function onMailDomainSelectChange(sel) {
  const val = parseInt(sel.value);
  const domainInput = $('md-domain');
  if (val === 0) {
    // External — clear and make editable
    domainInput.value = '';
    domainInput.placeholder = 'example.com';
    domainInput.readOnly = false;
    domainInput.style.background = '';
  } else {
    // Find domain in stored list and auto-fill
    const domains = window._mailDomains || [];
    const found = domains.find(d => d.id === val);
    if (found) {
      domainInput.value = found.domain;
      domainInput.readOnly = true;
      domainInput.style.background = 'var(--bg2)';
    }
  }
}

async function createMailDomain() {
  const domain = $('md-domain').value.trim();
  const mode = $('md-mode').value;
  const domain_id = parseInt($('md-domain-id').value) || 0;
  const relay_host = $('md-relay').value.trim();
  if (!domain) { toast('Enter a domain', 'error'); return; }
  try {
    const res = await apiPost('/api/mail/domains', {domain, mode, domain_id, relay_host});
    if (res.success) { toast('Mail domain created', 'success'); hideModal(); navigate('mail'); }
    else toast('Error: ' + (res.error||'Creation failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function removeMailDomain(id) {
  if (!confirm('Remove mail domain?')) return;
  try {
    const res = await apiPost('/api/mail/domains/' + id, {}, 'DELETE');
    if (res.success) { toast('Mail domain removed', 'success'); navigate('mail'); }
    else toast('Error: ' + (res.error||'Remove failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function loadMailDomain(p, id) {
  try {
    const dd = await api('/api/mail/domains/' + id);
    const domain = dd.data;
    if (!domain) { p.innerHTML = '<div class="empty-state">Mail domain not found</div>'; return; }

    // Fetch mailboxes and aliases
    let mailboxes = [], aliases = [];
    try { const mb = await api('/api/mail/domains/' + id + '/mailboxes'); mailboxes = mb.data||[]; } catch(e) {}
    try { const al = await api('/api/mail/domains/' + id + '/aliases'); aliases = al.data||[]; } catch(e) {}

    const modeLabel = `<span class="badge badge-info">${esc(domain.mode)}</span>`;
    const dkimStatus = domain.dkim_public_key_dns ? '<span class="badge badge-ok">✓ Generated</span>' : '<span class="badge badge-info">Not generated</span>';

    let html = `<div class="page-header">
      <h1><a href="#" onclick="navigate('mail');return false" style="color:var(--text2);text-decoration:none;">Mail</a> / ${esc(domain.domain)}</h1>
      <div class="page-actions">
        <button class="btn btn-sm" onclick="generateMailDkim(${id})">Generate DKIM</button>
        <button class="btn btn-sm" onclick="regenMailConfig()">Regenerate Config</button>
      </div>
    </div>
    <div class="details-panel"><div class="details-grid">
      <div class="details-field"><div class="details-label">Domain</div><div class="details-value">${esc(domain.domain)}</div></div>
      <div class="details-field"><div class="details-label">Mode</div><div class="details-value">${modeLabel}</div></div>
      <div class="details-field"><div class="details-label">Domain ID</div><div class="details-value">${domain.domain_id || 'External'}</div></div>
      <div class="details-field"><div class="details-label">Site ID</div><div class="details-value">${domain.site_id || 'None'}</div></div>
      <div class="details-field"><div class="details-label">Relay Host</div><div class="details-value">${esc(domain.relay_host) || '-'}</div></div>
      <div class="details-field"><div class="details-label">DKIM</div><div class="details-value">${dkimStatus}</div></div>
    </div></div>`;

    // Mailboxes section
    html += `<div class="page-header" style="margin-top:16px;"><h3>Mailboxes</h3><div class="page-actions"><button class="btn btn-primary btn-sm" onclick="showCreateMailbox(${id})">+ Add Mailbox</button></div></div>`;
    html += buildTable([
      {label:'Local Part',html:r=>esc(r.local_part)},
      {label:'Address',html:r=>esc(r.address)},
      {label:'Forward',html:r=>r.forward_to ? esc(r.forward_to) : '-'},
      {label:'Enabled',html:r=>r.enabled ? '<span class="badge badge-ok">Yes</span>' : '<span class="badge badge-err">No</span>'},
      {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeMailbox(${r.id}, ${id})">&#10005;</button>`}
    ], mailboxes, 'No mailboxes');

    // Aliases section
    html += `<div class="page-header" style="margin-top:16px;"><h3>Aliases</h3><div class="page-actions"><button class="btn btn-primary btn-sm" onclick="showCreateAlias(${id})">+ Add Alias</button></div></div>`;
    html += buildTable([
      {label:'Source',html:r=>esc(r.source)+'@'+esc(domain.domain)},
      {label:'Destination',html:r=>esc(r.destination)},
      {label:'Enabled',html:r=>r.enabled ? '<span class="badge badge-ok">Yes</span>' : '<span class="badge badge-err">No</span>'},
      {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeAlias(${r.id}, ${id})">&#10005;</button>`}
    ], aliases, 'No aliases');

    // DKIM DNS record if exists — structured display with copy buttons
    if (domain.dkim_public_key_dns) {
      const sel = domain.dkim_selector || 'dkim';
      const parts = domain.domain.split('.');
      const subdomain = parts.length > 2 ? parts.slice(0, -2).join('.') : '';
      const suggestedZone = parts.length > 2 ? parts.slice(-2).join('.') : domain.domain;
      const hostLocal = sel + '._domainkey' + (subdomain ? '.' + subdomain : '');
      const fqdn = hostLocal + '.' + suggestedZone;
      const value = domain.dkim_public_key_dns;
      const fullRecord = fqdn + '. 3600 IN TXT "' + value + '"';

      // Copy buttons use data-copy attributes — no inline onclick with DKIM values.
      // Event listeners are attached after p.innerHTML = html below.
      const btn = (id, label) =>
        `<button class="btn btn-sm btn-outline" data-copy="${esc(id)}">${label}</button>`;

      html += `<div class="card" style="margin-top:16px">
        <div class="card-header"><h3>DKIM DNS Record</h3></div>
        <div style="padding:12px">
          <div class="details-grid">
            <div class="details-field">
              <div class="details-label">Selector</div>
              <div class="details-value">${esc(sel)}</div>
            </div>
            <div class="details-field">
              <div class="details-label">Host / Name</div>
              <div class="details-value" style="font-family:monospace;font-size:13px;">${esc(hostLocal)}</div>
            </div>
            <div class="details-field">
              <div class="details-label">Type</div>
              <div class="details-value">TXT</div>
            </div>
            <div class="details-field">
              <div class="details-label">FQDN</div>
              <div class="details-value" style="font-family:monospace;font-size:13px;">${esc(fqdn)}</div>
            </div>
            <div class="details-field" style="grid-column:1/-1;">
              <div class="details-label">Value (TXT record)</div>
              <div class="details-value" style="font-family:monospace;font-size:12px;word-break:break-all;background:var(--bg3);padding:8px;border-radius:4px;">${esc(value)}</div>
            </div>
          </div>
          <div style="margin-top:12px;display:flex;gap:6px;flex-wrap:wrap;">
            ${btn('dkim-host', 'Copy Host')}
            ${btn('dkim-value', 'Copy Value')}
            ${btn('dkim-fqdn', 'Copy FQDN')}
            ${btn('dkim-full', 'Copy Full Record')}
          </div>
          <div style="margin-top:8px;font-size:11px;color:var(--text3);">
            Add this TXT record to the DNS zone that contains the domain.
            Suggested zone: <strong style="font-family:monospace;">${esc(suggestedZone)}</strong>
            → host <strong style="font-family:monospace;">${esc(hostLocal)}</strong>
          </div>
        </div>
      </div>`;

      // Store DKIM data for event listeners (attached after p.innerHTML below)
      window._dkimData = {hostLocal, value, fqdn, fullRecord};
    } else {
      // DKIM not yet generated
      html += `<div class="card" style="margin-top:16px">
        <div class="card-header"><h3>DKIM DNS Record</h3></div>
        <div style="padding:12px;text-align:center;color:var(--text3);">
          DKIM not generated.
          <button class="btn btn-sm btn-primary" style="margin-left:8px;" onclick="generateMailDkim(${id})">Generate DKIM</button>
        </div>
      </div>`;
    }

    p.innerHTML = html;

    // Attach DKIM copy button listeners — no inline onclick with DKIM values
    if (window._dkimData) {
      const d = window._dkimData;
      const bind = (suffix, text, msg) => {
        const el = p.querySelector(`[data-copy="dkim-${suffix}"]`);
        if (el) el.addEventListener('click', () => copyText(text, msg));
      };
      bind('host', d.hostLocal, 'Host copied');
      bind('value', d.value, 'Value copied');
      bind('fqdn', d.fqdn, 'FQDN copied');
      bind('full', d.fullRecord, 'Full record copied');
      window._dkimData = null;
    }
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load mail domain</div>'; }
}

function showCreateMailbox(domainId) {
  showModal('Add Mailbox', `
    <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Local Part</label><input id="mb-local" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" placeholder="user"></div>
    <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Password</label><input id="mb-pass" type="password" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
    <button class="btn btn-primary btn-sm" onclick="createMailbox(${domainId})">Create</button>
  `);
}

async function createMailbox(did) {
  const local_part = $('mb-local').value.trim();
  const password = $('mb-pass').value;
  if (!local_part || !password) { toast('Fill in all fields', 'error'); return; }
  try {
    const res = await apiPost('/api/mail/domains/' + did + '/mailboxes', {local_part, password});
    if (res.success) { toast('Mailbox created', 'success'); hideModal(); navigate('mail-domain', did); }
    else toast('Error: ' + (res.error||'Failed'), 'error');
  } catch(e) { toast(e.message || 'Network error', 'error'); }
}

async function removeMailbox(id, domainId) {
  if (!confirm('Remove mailbox?')) return;
  try {
    const res = await apiPost('/api/mail/mailboxes/' + id, {}, 'DELETE');
    if (res.success) { toast('Mailbox removed', 'success'); navigate('mail-domain', domainId); }
    else toast('Error: ' + (res.error||'Failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

function showCreateAlias(domainId) {
  showModal('Add Alias', `
    <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Source (local part)</label><input id="al-source" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" placeholder="info"></div>
    <div style="margin-bottom:8px"><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Destination (full email)</label><input id="al-dest" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" placeholder="user@example.com"></div>
    <button class="btn btn-primary btn-sm" onclick="createAlias(${domainId})">Create</button>
  `);
}

async function createAlias(did) {
  const source = $('al-source').value.trim();
  const dest = $('al-dest').value.trim();
  if (!source || !dest) { toast('Fill in all fields', 'error'); return; }
  try {
    const res = await apiPost('/api/mail/domains/' + did + '/aliases', {source, destination: dest});
    if (res.success) { toast('Alias created', 'success'); hideModal(); navigate('mail-domain', did); }
    else toast('Error: ' + (res.error||'Failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function removeAlias(id, domainId) {
  if (!confirm('Remove alias?')) return;
  try {
    const res = await apiPost('/api/mail/aliases/' + id, {}, 'DELETE');
    if (res.success) { toast('Alias removed', 'success'); navigate('mail-domain', domainId); }
    else toast('Error: ' + (res.error||'Failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function generateMailDkim(did) {
  try {
    const res = await apiPost('/api/mail/domains/' + did + '/dkim/generate', {});
    if (res.success) { toast('DKIM key generated', 'success'); navigate('mail-domain', did); }
    else toast('Error: ' + (res.error||'Failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function regenMailConfig() {
  try {
    const res = await apiPost('/api/mail/regenerate', {});
    if (res.success) { toast('Config regenerated', 'success'); }
    else toast('Error: ' + (res.error||'Failed'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function loadMailHealth(p) {
  try {
    const res = await api('/api/mail/health');
    const d = res.data || {};
    const services = d.services || [];
    let html = `<div class="page-header"><h1>Mail Health</h1><div class="page-actions"><button class="btn btn-sm" onclick="navigate('mail')">Back to Mail</button></div></div>`;
    html += `<div class="card"><div class="card-header"><h3>Status: <span class="badge ${d.status === 'ok' ? 'badge-ok' : d.status === 'degraded' ? 'badge-warn' : 'badge-err'}">${esc(d.status)}</span></h3></div><div style="padding:12px">`;
    for (const svc of services) {
      const dot = svc.status === 'ok' ? '🟢' : svc.status === 'degraded' ? '🟡' : '🔴';
      html += `<div style="display:flex;justify-content:space-between;padding:6px 0;border-bottom:1px solid var(--border);font-size:13px;"><span>${dot} ${esc(svc.name)}</span><span style="color:var(--text2)">${esc(svc.message)}</span></div>`;
    }
    if (d.details) {
      html += `<div style="margin-top:12px;font-size:12px;color:var(--text3);">`;
      for (const [k,v] of Object.entries(d.details)) {
        html += `<div>${esc(k)}: ${esc(String(v))}</div>`;
      }
      html += `</div>`;
    }
    html += `</div></div>`;
    p.innerHTML = html;
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load mail health</div>'; }
}

export { loadMail, loadMailDomain, loadMailHealth };
Object.assign(window, { loadMail, activateMail, deactivateMail, showCreateMailDomain, showCreateMailDomainSimple, onMailDomainSelectChange, createMailDomain, removeMailDomain, loadMailDomain, showCreateMailbox, createMailbox, removeMailbox, showCreateAlias, createAlias, removeAlias, generateMailDkim, regenMailConfig, loadMailHealth });
