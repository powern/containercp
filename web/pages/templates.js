import {
  api, apiPost, esc, hideModal, searchBox, selectFilter, showModal, toast
} from '../core/context.js';

/* ===== TEMPLATES ===== */
const tplState = {
  items: [],
  sites: [],
  loadError: ''
};

async function loadTemplates(p) {
  p.innerHTML = `<div class="page-header tpl-page-header"><div><h1>Web Server Templates</h1><p>Manage Apache and Nginx web server configuration profiles for site provisioning.</p></div><div class="page-actions"><button class="btn btn-sm" onclick="showCreateTemplateModal()" aria-label="Create new template">Create Template</button><button class="btn btn-sm" onclick="refreshTemplates()" aria-label="Refresh templates">Refresh</button></div></div><div id="tpl-body" aria-live="polite"><div class="empty-state">Loading templates...</div></div>`;
  await refreshTemplates();
}

async function refreshTemplates() {
  const body = document.getElementById('tpl-body');
  if (body) body.innerHTML = '<div class="empty-state">Loading templates...</div>';
  try {
    const [tplRes, sitesRes] = await Promise.all([
      api('/api/profiles'),
      api('/api/sites')
    ]);
    tplState.items = (tplRes.data || []).filter(r => r.type === 'web_server');
    tplState.sites = (sitesRes.data || []).filter(s => Number(s.id) > 0);
    tplState.loadError = '';
    renderTemplates();
  } catch(e) {
    tplState.items = [];
    tplState.loadError = 'Templates could not be loaded.';
    renderTemplates();
  }
}

function renderTemplates() {
  const body = document.getElementById('tpl-body');
  if (!body) return;
  if (tplState.loadError) {
    body.innerHTML = `<div class="empty-state">${esc(tplState.loadError)}<br><button class="btn btn-sm" style="margin-top:12px;" onclick="refreshTemplates()">Retry</button></div>`;
    return;
  }
  body.innerHTML = tplSummaryCards(tplState.items)
    + tplControlsHtml()
    + `<div class="db-inventory card"><div class="db-inventory-title"><strong>Templates</strong><span>${tplState.items.length} total</span></div>`
    + (tplState.items.length ? renderTplTable(tplState.items) : '<div class="empty-state">No web server templates found.</div>')
    + `</div>`;
}

function tplSummaryCards(items) {
  const apache = items.filter(r => r.web_server === 'apache').length;
  const nginx = items.filter(r => r.web_server !== 'apache').length;
  const def = items.filter(r => r.default).length;
  return `<div class="db-summary-grid">
    <div class="db-summary-card neutral"><div class="db-summary-label">Templates</div><div class="db-summary-value">${items.length}</div><div class="db-summary-help">Web server configuration profiles</div></div>
    <div class="db-summary-card healthy"><div class="db-summary-label">Apache</div><div class="db-summary-value">${apache}</div><div class="db-summary-help">Apache httpd backed templates</div></div>
    <div class="db-summary-card warning"><div class="db-summary-label">Nginx</div><div class="db-summary-value">${nginx}</div><div class="db-summary-help">Nginx backed templates</div></div>
    <div class="db-summary-card imported"><div class="db-summary-label">Default</div><div class="db-summary-value">${def}</div><div class="db-summary-help">Active default profile for new sites</div></div>
  </div>`;
}

function tplControlsHtml() {
  return `<div class="db-controls card">
    <div class="db-search"><label for="tpl-search-input">Search</label><input id="tpl-search-input" placeholder="Template name, web server" oninput="filterTemplates()"></div>
  </div>`;
}

function filterTemplates() {
  renderTemplates();
}

function getFilteredTemplates() {
  const q = ((document.getElementById('tpl-search-input') || {}).value || '').toLowerCase();
  if (!q) return tplState.items;
  return tplState.items.filter(t =>
    t.name.toLowerCase().includes(q) ||
    t.web_server.toLowerCase().includes(q) ||
    (t.description || '').toLowerCase().includes(q)
  );
}

function tplBadge(label, cls) {
  return `<span class="badge ${cls || 'badge-info'}">${esc(label)}</span>`;
}

function renderTplTable(rows) {
  const filtered = getFilteredTemplates();
  return `<div class="db-table-wrap"><table class="db-table"><thead><tr>
    <th>Name</th><th>Web Server</th><th>Default</th><th>Description</th><th>Actions</th>
  </tr></thead><tbody>`
    + filtered.map(t => {
      const serverBadge = t.web_server === 'apache'
        ? tplBadge('Apache', 'badge-info')
        : tplBadge('Nginx', 'badge-info');
      const defBadge = t.default
        ? tplBadge('Default', 'badge-ok')
        : tplBadge('', '');
      return `<tr>
        <td><strong>${esc(t.name)}</strong></td>
        <td>${serverBadge}</td>
        <td>${defBadge}</td>
        <td style="color:var(--text2);font-size:13px;">${esc(t.description || '')}</td>
        <td style="white-space:nowrap;">
          <button class="btn btn-sm" onclick="editTemplate(${t.id})">Edit</button>
          <button class="btn btn-sm" onclick="cloneTemplate(${t.id})">Clone</button>
          <button class="btn btn-sm" onclick="showApplyTemplateModal(${t.id})">Apply to Site</button>
          <button class="btn btn-sm btn-danger" onclick="deleteTemplate(${t.id})">Delete</button>
        </td>
      </tr>`;
    }).join('')
    + `</tbody></table></div>`;
}

function showCreateTemplateModal() {
  showTemplateModal(null, 'Create Template', {});
}

function cloneTemplate(id) {
  const t = tplState.items.find(x => x.id === id);
  if (!t) return;
  showTemplateModal(id, 'Clone Template', {
    name: t.name + '-copy',
    web_server: t.web_server,
    description: t.description || '',
    content: t.content || ''
  });
}

function editTemplate(id) {
  const t = tplState.items.find(x => x.id === id);
  if (!t) return;
  showTemplateModal(id, 'Edit Template', {
    name: t.name,
    web_server: t.web_server,
    description: t.description || '',
    content: t.content || '',
    default: t.default
  });
}

function showTemplateModal(editId, title, defaults) {
  const isCreate = editId === null || editId === undefined;
  const submitLabel = isCreate ? 'Create' : 'Update';
  const submitAction = isCreate ? 'submitCreateTemplate()' : `submitEditTemplate(${editId})`;
  const nameDisabled = !isCreate ? 'disabled' : '';
  const nameVal = esc(defaults.name || '');
  const serverVal = defaults.web_server || 'apache';
  const descVal = esc(defaults.description || '');
  const contentVal = esc(defaults.content || '');
  const defChecked = defaults.default ? 'checked' : '';

  showModal(title, `<div class="db-confirm-body">
    <label class="db-filter"><span>Name</span><input id="tpl-name" value="${nameVal}" placeholder="my-template" ${nameDisabled}></label>
    <label class="db-filter"><span>Web Server</span><select id="tpl-web-server">
      <option value="apache" ${serverVal === 'apache' ? 'selected' : ''}>Apache</option>
      <option value="nginx" ${serverVal === 'nginx' ? 'selected' : ''}>Nginx</option>
    </select></label>
    <label class="db-filter"><span>Description</span><input id="tpl-desc" value="${descVal}" placeholder="Optional description"></label>
    <label class="db-filter" style="grid-column:1/-1;"><span>Template Content</span>
      <textarea id="tpl-content" rows="12" style="width:100%;padding:10px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-family:var(--font-mono);font-size:12px;outline:none;resize:vertical;tab-size:2;">${contentVal}</textarea>
    </label>
    <label class="db-filter" style="flex-direction:row;gap:8px;align-items:center;">
      <input type="checkbox" id="tpl-default" ${defChecked} style="width:auto;"> <span>Set as default template</span>
    </label>
    <div id="tpl-modal-msg" style="font-size:12px;color:var(--text3);margin-top:6px;"></div>
    <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:14px;">
      <button class="btn btn-sm" onclick="hideModal()">Cancel</button>
      <button class="btn btn-sm btn-primary" onclick="${submitAction}">${submitLabel}</button>
    </div>
  </div>`, 680);
}

async function submitCreateTemplate() {
  const msg = document.getElementById('tpl-modal-msg');
  const name = document.getElementById('tpl-name');
  const web_server = document.getElementById('tpl-web-server');
  const desc = document.getElementById('tpl-desc');
  const content = document.getElementById('tpl-content');
  const def = document.getElementById('tpl-default');
  if (!name || !web_server || !content) return;
  if (!name.value.trim()) { if (msg) msg.textContent = 'Name is required.'; return; }
  if (!content.value.trim()) { if (msg) msg.textContent = 'Content is required.'; return; }
  if (msg) msg.textContent = 'Creating template...';
  try {
    await apiPost('/api/profiles', {
      name: name.value.trim(),
      web_server: web_server.value,
      description: (desc ? desc.value : ''),
      content: content.value,
      default: def ? def.checked : false
    });
    hideModal();
    toast('Template created', 'success');
    refreshTemplates();
  } catch(e) {
    const apiErr = e.body && e.body.error;
    if (msg) msg.textContent = apiErr || e.api_message || e.message || 'Failed to create template';
  }
}

async function submitEditTemplate(id) {
  const msg = document.getElementById('tpl-modal-msg');
  const web_server = document.getElementById('tpl-web-server');
  const desc = document.getElementById('tpl-desc');
  const content = document.getElementById('tpl-content');
  const def = document.getElementById('tpl-default');
  if (!msg) return;
  const body = {};
  if (web_server) body.web_server = web_server.value;
  if (desc) body.description = desc.value;
  if (content) body.content = content.value;
  if (def) body.default = def.checked;
  if (msg) msg.textContent = 'Updating template...';
  try {
    await apiPost('/api/profiles/' + id, body);
    hideModal();
    toast('Template updated', 'success');
    refreshTemplates();
  } catch(e) {
    const apiErr = e.body && e.body.error;
    if (msg) msg.textContent = apiErr || e.api_message || e.message || 'Failed to update template';
  }
}

async function deleteTemplate(id) {
  const t = tplState.items.find(x => x.id === id);
  if (!t) return;
  const target = t.name;
  let submitting = false;
  showModal('Delete Template', `<div class="db-confirm-body">
    <p><strong>${esc(target)}</strong> (${esc(t.web_server)})</p>
    <ul>
      <li>The template profile and its disk file will be permanently removed.</li>
      <li>Sites using this template will keep their current configuration.</li>
      <li>The last default web server template cannot be deleted.</li>
    </ul>
    <p style="color:var(--danger);font-size:12px;">Type the template name to confirm deletion.</p>
    <input id="tpl-delete-confirm" autocomplete="off" placeholder="${esc(target)}" style="width:100%;padding:8px 10px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;margin-top:8px;">
    <div id="tpl-delete-msg" style="font-size:12px;color:var(--text3);margin-top:6px;">Enter the template name to enable deletion.</div>
    <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:14px;">
      <button class="btn btn-sm" onclick="hideModal()">Cancel</button>
      <button id="tpl-delete-submit" class="btn btn-sm btn-danger" disabled>Delete</button>
    </div>
  </div>`, 520);
  const input = document.getElementById('tpl-delete-confirm');
  const submit = document.getElementById('tpl-delete-submit');
  const dmsg = document.getElementById('tpl-delete-msg');
  if (!input || !submit) return;
  const updateState = () => {
    const ok = input.value.trim() === target;
    submit.disabled = submitting || !ok;
    if (dmsg) dmsg.textContent = ok ? 'Name matched. Click Delete to confirm.' : 'Enter the exact template name to confirm.';
  };
  const onSubmit = async () => {
    if (submitting || input.value.trim() !== target) return;
    submitting = true;
    submit.disabled = true;
    if (dmsg) dmsg.textContent = 'Deleting...';
    try {
      await apiPost('/api/profiles/' + id, {}, 'DELETE');
      hideModal();
      toast('Template deleted', 'success');
      refreshTemplates();
    } catch(e) {
      const apiErr = e.body && e.body.error;
      if (dmsg) dmsg.textContent = apiErr || e.api_message || e.message || 'Failed to delete';
      submitting = false;
      updateState();
    }
  };
  input.addEventListener('input', updateState);
  submit.addEventListener('click', onSubmit);
  const cleanup = () => { input.removeEventListener('input', updateState); submit.removeEventListener('click', onSubmit); submitting = false; };
  setModalCleanup(cleanup);
  setTimeout(() => input.focus(), 0);
}

function showApplyTemplateModal(id) {
  const t = tplState.items.find(x => x.id === id);
  if (!t || !tplState.sites.length) {
    toast('No available sites to apply this template to.', 'info');
    return;
  }
  let submitting = false;
  showModal('Apply Template', `<div class="db-confirm-body">
    <p><strong>${esc(t.name)}</strong> — apply this web server template to an existing site.</p>
    <ul>
      <li>The site's <code>default.conf</code> will be regenerated from this template.</li>
      <li>The web container will be restarted (brief downtime).</li>
      <li>Site content, database, and SSL are not affected.</li>
    </ul>
    <label class="db-filter"><span>Site</span><select id="tpl-apply-site">
      ${tplState.sites.map(s => `<option value="${s.id}">${esc(s.domain || s.name || 'site #' + s.id)}</option>`).join('')}
    </select></label>
    <div id="tpl-apply-msg" style="font-size:12px;color:var(--text3);margin-top:6px;">Select a site and apply the template.</div>
    <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:14px;">
      <button class="btn btn-sm" onclick="hideModal()">Cancel</button>
      <button id="tpl-apply-submit" class="btn btn-sm btn-primary">Apply Template</button>
    </div>
  </div>`, 520);
  const submit = document.getElementById('tpl-apply-submit');
  const msg = document.getElementById('tpl-apply-msg');
  const siteSelect = document.getElementById('tpl-apply-site');
  if (!submit || !siteSelect) return;
  const onSubmit = async () => {
    if (submitting) return;
    submitting = true;
    submit.disabled = true;
    const siteId = siteSelect.value;
    if (msg) msg.textContent = 'Applying template...';
    try {
      await apiPost('/api/sites/' + siteId + '/apply-template', {
        template_id: String(id),
        template_name: t.name
      });
      hideModal();
      toast('Template applied to site', 'success');
    } catch(e) {
      const apiErr = e.body && e.body.error;
      if (msg) msg.textContent = apiErr || e.api_message || e.message || 'Failed to apply template';
      submitting = false;
      submit.disabled = false;
    }
  };
  submit.addEventListener('click', onSubmit);
  const cleanup = () => { submit.removeEventListener('click', onSubmit); submitting = false; };
  setModalCleanup(cleanup);
}

const templatesPage = { mount: loadTemplates };
export { loadTemplates, templatesPage };
Object.assign(window, {
  loadTemplates, refreshTemplates, renderTemplates,
  showCreateTemplateModal, cloneTemplate, editTemplate, deleteTemplate,
  submitCreateTemplate, submitEditTemplate, showApplyTemplateModal,
  filterTemplates
});
