import {
  api, apiPost, esc, escAttr, hideModal, setModalCleanup, showModal, toast
} from '../core/context.js';

const tplState = {
  items: [],
  search: '',
  backend: 'all',
  loadError: ''
};

async function loadTemplates(p) {
  p.innerHTML = `<div class="page-header tpl-page-header"><div><h1>Web Server Templates</h1><p>Manage Apache and Nginx configuration profiles used during site provisioning.</p></div><div class="page-actions"><button class="btn btn-sm" onclick="showCreateTemplateModal()">Create Template</button><button class="btn btn-sm" onclick="refreshTemplates()">Refresh</button></div></div><div id="tpl-body" aria-live="polite"><div class="empty-state">Loading templates...</div></div>`;
  await refreshTemplates();
}

async function refreshTemplates() {
  const body = document.getElementById('tpl-body');
  if (body) body.innerHTML = '<div class="empty-state">Loading templates...</div>';
  try {
    const res = await api('/api/profiles');
    tplState.items = (res.data || []).filter(r => r.type === 'web_server');
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
  const rows = getFilteredTemplates();
  body.innerHTML = tplSummaryCards(tplState.items)
    + tplControlsHtml()
    + `<div class="db-inventory card"><div class="db-inventory-title"><strong>Template Catalog</strong><span>${rows.length} shown</span></div>`
    + (rows.length ? renderTplTable(rows) : '<div class="empty-state">No templates match the current filter.</div>')
    + `</div>`;
}

function tplSummaryCards(items) {
  const apache = items.filter(r => r.web_server === 'apache').length;
  const nginx = items.filter(r => r.web_server === 'nginx').length;
  const apacheDefault = items.find(r => r.web_server === 'apache' && r.default);
  const nginxDefault = items.find(r => r.web_server === 'nginx' && r.default);
  return `<div class="db-summary-grid">
    <div class="db-summary-card neutral"><div class="db-summary-label">Templates</div><div class="db-summary-value">${items.length}</div><div class="db-summary-help">Editable disk-backed profiles</div></div>
    <div class="db-summary-card healthy"><div class="db-summary-label">Apache</div><div class="db-summary-value">${apache}</div><div class="db-summary-help">Default: ${esc(apacheDefault ? apacheDefault.name : 'missing')}</div></div>
    <div class="db-summary-card warning"><div class="db-summary-label">Nginx</div><div class="db-summary-value">${nginx}</div><div class="db-summary-help">Default: ${esc(nginxDefault ? nginxDefault.name : 'missing')}</div></div>
    <div class="db-summary-card imported"><div class="db-summary-label">Backends</div><div class="db-summary-value">2</div><div class="db-summary-help">Apache and Nginx defaults are independent</div></div>
  </div>`;
}

function tplControlsHtml() {
  return `<div class="db-controls card">
    <div class="db-search"><label for="tpl-search-input">Search</label><input id="tpl-search-input" value="${escAttr(tplState.search)}" placeholder="Template name, backend, description" oninput="tplState.search=this.value;renderTemplates();"></div>
    <label class="db-filter"><span>Backend</span><select id="tpl-backend-filter" onchange="tplState.backend=this.value;renderTemplates();">
      <option value="all" ${tplState.backend === 'all' ? 'selected' : ''}>All</option>
      <option value="apache" ${tplState.backend === 'apache' ? 'selected' : ''}>Apache</option>
      <option value="nginx" ${tplState.backend === 'nginx' ? 'selected' : ''}>Nginx</option>
    </select></label>
    <button class="btn btn-sm" onclick="resetTemplateFilters()">Reset filters</button>
  </div>`;
}

function resetTemplateFilters() {
  tplState.search = '';
  tplState.backend = 'all';
  renderTemplates();
}

function getFilteredTemplates() {
  const q = (tplState.search || '').toLowerCase();
  return tplState.items.filter(t => {
    if (tplState.backend !== 'all' && t.web_server !== tplState.backend) return false;
    return !q
      || String(t.name || '').toLowerCase().includes(q)
      || String(t.web_server || '').toLowerCase().includes(q)
      || String(t.description || '').toLowerCase().includes(q);
  });
}

function tplBadge(label, cls) {
  return `<span class="badge ${cls || 'badge-info'}">${esc(label)}</span>`;
}

function renderTplTable(rows) {
  return `<div class="db-table-wrap"><table class="db-table"><thead><tr><th>Name</th><th>Backend</th><th>Default</th><th>Description</th><th>Actions</th></tr></thead><tbody>`
    + rows.map(t => `<tr>
      <td><strong>${esc(t.name)}</strong></td>
      <td>${tplBadge(t.web_server === 'apache' ? 'Apache' : 'Nginx', 'badge-info')}</td>
      <td>${t.default ? tplBadge((t.web_server === 'apache' ? 'Apache' : 'Nginx') + ' default', 'badge-ok') : tplBadge('Custom', 'badge-info')}</td>
      <td style="color:var(--text2);font-size:13px;">${esc(t.description || '')}</td>
      <td style="white-space:nowrap;">
        <button class="btn btn-sm" onclick="editTemplate(${Number(t.id)})">Edit</button>
        <button class="btn btn-sm" onclick="cloneTemplate(${Number(t.id)})">Clone</button>
        <button class="btn btn-sm btn-danger" onclick="deleteTemplate(${Number(t.id)})">Delete</button>
      </td>
    </tr>`).join('')
    + `</tbody></table></div>`;
}

function showCreateTemplateModal() {
  showTemplateModal(null, 'Create Template', {web_server:'apache'});
}

function cloneTemplate(id) {
  const t = tplState.items.find(x => Number(x.id) === Number(id));
  if (!t) return;
  showTemplateModal(null, 'Clone Template', {
    name: t.name + '-copy',
    web_server: t.web_server,
    description: t.description || '',
    content: t.content || '',
    default: false
  });
}

function editTemplate(id) {
  const t = tplState.items.find(x => Number(x.id) === Number(id));
  if (!t) return;
  showTemplateModal(id, 'Edit Template', t);
}

function showTemplateModal(editId, title, defaults) {
  const isCreate = editId === null || editId === undefined;
  const submitAction = isCreate ? 'submitCreateTemplate()' : `submitEditTemplate(${Number(editId)})`;
  const serverVal = defaults.web_server || 'apache';
  showModal(title, `<div class="db-confirm-body">
    <label class="db-filter"><span>Name</span><input id="tpl-name" value="${escAttr(defaults.name || '')}" placeholder="apache-custom" ${isCreate ? '' : 'disabled'}></label>
    <label class="db-filter"><span>Backend</span><select id="tpl-web-server">
      <option value="apache" ${serverVal === 'apache' ? 'selected' : ''}>Apache</option>
      <option value="nginx" ${serverVal === 'nginx' ? 'selected' : ''}>Nginx</option>
    </select></label>
    <label class="db-filter"><span>Description</span><input id="tpl-desc" value="${escAttr(defaults.description || '')}" placeholder="Optional description"></label>
    <label class="db-filter" style="grid-column:1/-1;"><span>Template Content</span>
      <textarea id="tpl-content" rows="14" style="width:100%;padding:10px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-family:var(--font-mono);font-size:12px;outline:none;resize:vertical;tab-size:2;">${esc(defaults.content || '')}</textarea>
    </label>
    <label class="db-filter" style="flex-direction:row;gap:8px;align-items:center;">
      <input type="checkbox" id="tpl-default" ${defaults.default ? 'checked' : ''} style="width:auto;"> <span>Set as default for selected backend</span>
    </label>
    <div id="tpl-modal-msg" style="font-size:12px;color:var(--text3);margin-top:6px;"></div>
    <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:14px;">
      <button class="btn btn-sm" onclick="hideModal()">Cancel</button>
      <button class="btn btn-sm btn-primary" onclick="${submitAction}">${isCreate ? 'Create' : 'Update'}</button>
    </div>
  </div>`, 720);
}

function readTemplateForm() {
  const name = document.getElementById('tpl-name');
  const webServer = document.getElementById('tpl-web-server');
  const desc = document.getElementById('tpl-desc');
  const content = document.getElementById('tpl-content');
  const def = document.getElementById('tpl-default');
  return {
    name: name ? name.value.trim() : '',
    web_server: webServer ? webServer.value : 'apache',
    description: desc ? desc.value : '',
    content: content ? content.value : '',
    default: def ? def.checked : false
  };
}

async function submitCreateTemplate() {
  const msg = document.getElementById('tpl-modal-msg');
  const body = readTemplateForm();
  if (!body.name) { if (msg) msg.textContent = 'Name is required.'; return; }
  if (!body.content.trim()) { if (msg) msg.textContent = 'Content is required.'; return; }
  if (msg) msg.textContent = 'Creating template...';
  try {
    await apiPost('/api/profiles', body);
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
  const body = readTemplateForm();
  delete body.name;
  if (!body.content.trim()) { if (msg) msg.textContent = 'Content is required.'; return; }
  if (msg) msg.textContent = 'Updating template...';
  try {
    await apiPost('/api/profiles/' + Number(id), body);
    hideModal();
    toast('Template updated', 'success');
    refreshTemplates();
  } catch(e) {
    const apiErr = e.body && e.body.error;
    if (msg) msg.textContent = apiErr || e.api_message || e.message || 'Failed to update template';
  }
}

function deleteTemplate(id) {
  const t = tplState.items.find(x => Number(x.id) === Number(id));
  if (!t) return;
  const target = t.name;
  let submitting = false;
  showModal('Delete Template', `<div class="db-confirm-body">
    <p><strong>${esc(target)}</strong> (${esc(t.web_server)})</p>
    <ul><li>The template profile and disk file will be removed.</li><li>Existing sites keep their already-rendered config.</li><li>The last default template for a backend cannot be deleted.</li></ul>
    <p style="color:var(--danger);font-size:12px;">Type the template name to confirm deletion.</p>
    <input id="tpl-delete-confirm" autocomplete="off" placeholder="${escAttr(target)}" style="width:100%;padding:8px 10px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;margin-top:8px;">
    <div id="tpl-delete-msg" style="font-size:12px;color:var(--text3);margin-top:6px;">Enter the exact template name.</div>
    <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:14px;"><button class="btn btn-sm" onclick="hideModal()">Cancel</button><button id="tpl-delete-submit" class="btn btn-sm btn-danger" disabled>Delete</button></div>
  </div>`, 540);
  const input = document.getElementById('tpl-delete-confirm');
  const submit = document.getElementById('tpl-delete-submit');
  const msg = document.getElementById('tpl-delete-msg');
  if (!input || !submit) return;
  const update = () => {
    const ok = input.value.trim() === target;
    submit.disabled = submitting || !ok;
    if (msg) msg.textContent = ok ? 'Name matched. Click Delete to confirm.' : 'Enter the exact template name.';
  };
  const onSubmit = async () => {
    if (submitting || input.value.trim() !== target) return;
    submitting = true;
    submit.disabled = true;
    if (msg) msg.textContent = 'Deleting...';
    try {
      await apiPost('/api/profiles/' + Number(id), {}, 'DELETE');
      hideModal();
      toast('Template deleted', 'success');
      refreshTemplates();
    } catch(e) {
      const apiErr = e.body && e.body.error;
      if (msg) msg.textContent = apiErr || e.api_message || e.message || 'Failed to delete template';
      submitting = false;
      update();
    }
  };
  input.addEventListener('input', update);
  submit.addEventListener('click', onSubmit);
  setModalCleanup(() => {
    input.removeEventListener('input', update);
    submit.removeEventListener('click', onSubmit);
    submitting = false;
  });
  setTimeout(() => input.focus(), 0);
}

const templatesPage = { mount: loadTemplates, unmount() { hideModal(); } };
export { loadTemplates, templatesPage };
Object.assign(window, {
  tplState, loadTemplates, refreshTemplates, renderTemplates, resetTemplateFilters,
  showCreateTemplateModal, cloneTemplate, editTemplate, deleteTemplate,
  submitCreateTemplate, submitEditTemplate
});
