/* ===== API ===== */
async function api(path, opts) {
  try {
    const res = await fetch(path, opts);
    if (!res.ok) throw new Error('HTTP ' + res.status);
    return await res.json();
  } catch (e) {
    toast('Network error: ' + e.message, 'error');
    throw e;
  }
}

/* ===== TOAST ===== */
function toast(msg, type) {
  let c = document.getElementById('toast-container');
  if (!c) {
    c = document.createElement('div');
    c.id = 'toast-container';
    c.style.cssText = 'position:fixed;top:16px;right:16px;z-index:9999;display:flex;flex-direction:column;gap:8px;';
    document.body.appendChild(c);
  }
  const t = document.createElement('div');
  t.style.cssText = 'padding:10px 16px;border-radius:6px;font-size:13px;background:' +
    (type === 'error' ? 'var(--red)' : type === 'warn' ? 'var(--yellow)' : 'var(--green)') +
    ';color:#fff;box-shadow:0 4px 12px rgba(0,0,0,0.3);animation:slideIn .2s;max-width:360px;';
  t.textContent = msg;
  c.appendChild(t);
  setTimeout(() => { t.style.opacity = '0'; t.style.transition = 'opacity .3s'; setTimeout(() => t.remove(), 300); }, 3500);
}

/* ===== UTILS ===== */
function esc(s) { return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
function $(id) { return document.getElementById(id); }
function qs(s) { return document.querySelector(s); }
function qsa(s) { return document.querySelectorAll(s); }

let currentPage = 'dashboard';
let searchTerm = '';

/* ===== NAVIGATION ===== */
function navigate(page, params) {
  currentPage = page;
  qsa('.nav-link').forEach(l => l.classList.toggle('active', l.dataset.page === (page === 'site-detail' ? 'sites' : page)));
  loadPage(page, params);
}

function loadPage(page, params) {
  const p = $('page');
  p.scrollTop = 0;
  if (page === 'dashboard') loadDashboard(p);
  else if (page === 'sites') loadSites(p);
  else if (page === 'site-detail') loadSiteDetail(p, params);
  else if (page === 'domains') loadDomains(p);
  else if (page === 'databases') loadDatabases(p);
  else if (page === 'ssl') loadSsl(p);
  else if (page === 'proxy') loadProxy(p);
  else if (page === 'access') loadAccess(p);
  else if (page === 'profiles') loadProfiles(p);
  else if (page === 'templates') loadTemplates(p);
  else if (page === 'nodes') loadNodes(p);
  else if (page === 'logs') loadLogs(p);
  else if (page === 'settings') loadSettings(p);
}

/* ===== DASHBOARD ===== */
async function loadDashboard(p) {
  try {
    const [health, sites, domains, databases, ssl, proxy, access, users, nodes] = await Promise.all([
      api('/api/health'), api('/api/sites'), api('/api/domains'),
      api('/api/databases'), api('/api/ssl'), api('/api/proxy'),
      api('/api/access-users'), api('/api/users')
    ]);
    const ok = health.data?.status === 'ok';
    p.innerHTML = `
      <div class="page-header"><h1>Dashboard</h1></div>
      <div class="cards">
        ${card('Sites', sites.data?.length||0,'#6366f1')}
        ${card('Domains', domains.data?.length||0,'#3b82f6')}
        ${card('Databases', databases.data?.length||0,'#8b5cf6')}
        ${card('SSL', ssl.data?.length||0,'#ec4899')}
        ${card('Proxy', proxy.data?.length||0,'#06b6d4')}
        ${card('Access', access.data?.length||0,'#f97316')}
        ${card('Users', users.data?.length||0,'#22c55e')}
        ${card('Nodes', nodes.data?.length||0,'#eab308')}
      </div>
      <div style="font-size:13px;color:var(--text2);margin-bottom:12px;font-weight:600;">System Health</div>
      <div class="health-grid">
        ${healthItem('Daemon', ok)}
        ${healthItem('REST API', ok)}
        ${healthItem('Storage', true)}
        ${healthItem('Runtime', ok)}
        ${healthItem('Proxy Service', true)}
      </div>
      <div style="font-size:13px;color:var(--text2);margin-bottom:12px;font-weight:600;">Recent Activity</div>
      <div class="activity-list">
        <div class="activity-item"><div class="activity-icon" style="background:var(--green)"></div><div class="activity-text">System started</div><div class="activity-time">just now</div></div>
        <div class="activity-item"><div class="activity-icon" style="background:var(--blue)"></div><div class="activity-text">API server listening</div><div class="activity-time">just now</div></div>
        <div class="activity-item"><div class="activity-icon" style="background:var(--yellow)"></div><div class="activity-text">${sites.data?.length||0} sites loaded</div><div class="activity-time">just now</div></div>
      </div>`;
  } catch (e) { p.innerHTML = '<div class="empty-state">Failed to load dashboard</div>'; }
}

function card(label, count, color) {
  return `<div class="card"><div class="card-header"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="2"><circle cx="12" cy="12" r="10"/></svg><h3>${label}</h3></div><div class="count${count===0?' zero':''}">${count}</div></div>`;
}
function healthItem(name, ok) {
  return `<div class="health-item"><div class="health-dot ${ok?'ok':'error'}"></div><div><div class="health-name">${name}</div><div class="health-label">${ok?'Running':'Unavailable'}</div></div></div>`;
}

/* ===== TOOLBAR & TABLE ===== */
function tb(title, placeholder) {
  return `<div class="table-toolbar"><div style="font-weight:600;font-size:14px;">${title}</div><div class="search-box"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg><input type="text" placeholder="${placeholder||'Search...'}" oninput="searchTerm=this.value;window.renderTable&&renderTable()"></div></div>`;
}

function buildTable(columns, rows, emptyMsg) {
  if (!rows||rows.length===0) return `<div class="empty-state"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><path d="M8 12h8"/></svg><br>${emptyMsg||'No data'}</div>`;
  let h='<div class="table-wrap"><table><thead><tr>'+columns.map(c=>'<th>'+esc(c.label)+'</th>').join('')+'</tr></thead><tbody>';
  for (const row of rows) { h+='<tr>'+columns.map(c=>'<td>'+c.html(row)+'</td>').join('')+'</tr>'; }
  h+='</tbody></table></div>';
  return h;
}

function actionBtn(label, cls, onclick) {
  return `<button class="btn btn-sm ${cls}" onclick="${onclick}">${label}</button>`;
}

/* ===== MODAL ===== */
function showModal(title, bodyHtml) {
  let overlay = document.getElementById('modal-overlay');
  if (!overlay) {
    overlay = document.createElement('div');
    overlay.id = 'modal-overlay';
    overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.5);z-index:9000;display:flex;align-items:center;justify-content:center;';
    overlay.addEventListener('click', e => { if (e.target === overlay) hideModal(); });
    document.body.appendChild(overlay);
  }
  overlay.innerHTML = `<div style="background:var(--surface);border:1px solid var(--border);border-radius:12px;width:480px;max-width:90vw;max-height:80vh;overflow-y:auto;">
    <div style="display:flex;justify-content:space-between;align-items:center;padding:16px 20px;border-bottom:1px solid var(--border);">
      <h2 style="font-size:16px;font-weight:600;">${esc(title)}</h2>
      <button class="btn-icon" onclick="hideModal()" style="font-size:18px;">&times;</button>
    </div>
    <div style="padding:20px;">${bodyHtml}</div>
  </div>`;
  overlay.style.display = 'flex';
}

function hideModal() {
  const o = document.getElementById('modal-overlay');
  if (o) o.style.display = 'none';
}

/* ===== SITES ===== */
async function loadSites(p) {
  try {
    const data = await api('/api/sites');
    p.innerHTML = `<div class="page-header"><h1>Sites</h1><div class="page-actions"><button class="btn btn-primary btn-sm" onclick="showCreateSiteModal()">+ Create Site</button></div></div>`;
    p.innerHTML += tb('All Sites','Search domains...');
    window.renderTable = () => {
      const tbl = $('sites-table');
      if (!tbl) return;
      const filtered = (data.data||[]).filter(r => !searchTerm || r.domain.includes(searchTerm) || r.owner.includes(searchTerm));
      tbl.innerHTML = buildTable([
        {label:'ID',html:r=>esc(r.id)},
        {label:'Domain',html:r=>`<a href="#" onclick="navigate('site-detail',${r.id});return false" style="color:var(--primary);text-decoration:none;">${esc(r.domain)}</a>`},
        {label:'Owner',html:r=>esc(r.owner)},
        {label:'Actions',html:r=>`<button class="btn-icon" onclick="navigate('site-detail',${r.id})" title="View">&#128065;</button><button class="btn-icon" title="Start">&#9654;</button><button class="btn-icon" title="Stop">&#9646;&#9646;</button><button class="btn-icon" style="color:var(--red)" title="Remove" onclick="if(confirm('Remove ${esc(r.domain)}?'))toast('Site removed (mock)','warn')">&#10005;</button>`}
      ], filtered, 'No sites');
    };
    p.innerHTML += `<div id="sites-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load sites</div>'; }
}

function showCreateSiteModal() {
  showModal('Create Site', `
    <div style="display:grid;gap:12px;">
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Owner</label><input id="cs-owner" class="modal-input" placeholder="admin" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Domain</label><input id="cs-domain" class="modal-input" placeholder="example.com" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Template</label><select id="cs-template" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"><option value="">Default (nginx-php-default)</option></select></div>
      <button class="btn btn-primary" onclick="createSite()" style="margin-top:8px;">Create Site</button>
    </div>`);
}

async function createSite() {
  const owner = $('cs-owner').value.trim();
  const domain = $('cs-domain').value.trim();
  if (!owner || !domain) { toast('Owner and domain required', 'error'); return; }
  toast('Site creation would start for ' + domain, 'info');
  hideModal();
}

/* ===== SITE DETAIL ===== */
async function loadSiteDetail(p, siteId) {
  try {
    const data = await api('/api/sites');
    const site = (data.data||[]).find(s => s.id == siteId);
    if (!site) { p.innerHTML = '<div class="empty-state">Site not found</div>'; return; }
    p.innerHTML = `
      <div class="page-header">
        <h1><a href="#" onclick="navigate('sites');return false" style="color:var(--text2);text-decoration:none;">&larr; Sites</a> / ${esc(site.domain)}</h1>
        <div class="page-actions"><button class="btn btn-sm">&#9654; Start</button><button class="btn btn-sm">&#9646;&#9646; Stop</button><button class="btn btn-sm btn-danger">Remove</button></div>
      </div>
      <div class="tabs" id="site-tabs">
        <div class="tab active" data-tab="general">General</div>
        <div class="tab" data-tab="profiles">Profiles</div>
        <div class="tab" data-tab="ssl">SSL</div>
        <div class="tab" data-tab="proxy">Proxy</div>
        <div class="tab" data-tab="access">Access</div>
      </div>
      <div id="site-tab-content"></div>`;
    qsa('#site-tabs .tab').forEach(t => {
      t.addEventListener('click', () => {
        qsa('#site-tabs .tab').forEach(x => x.classList.remove('active'));
        t.classList.add('active');
        renderSiteTab(t.dataset.tab, site);
      });
    });
    renderSiteTab('general', site);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load site</div>'; }
}

function renderSiteTab(tab, site) {
  const c = $('site-tab-content');
  if (tab === 'general') {
    c.innerHTML = `<div class="details-panel"><div class="details-grid">
      <div class="details-field"><div class="details-label">ID</div><div class="details-value">${site.id}</div></div>
      <div class="details-field"><div class="details-label">Domain</div><div class="details-value">${esc(site.domain)}</div></div>
      <div class="details-field"><div class="details-label">Owner</div><div class="details-value">${esc(site.owner)}</div></div>
      <div class="details-field"><div class="details-label">Node ID</div><div class="details-value">${site.node_id}</div></div>
    </div></div>`;
  } else {
    c.innerHTML = `<div class="details-panel"><div class="empty-state">${tab} information will appear here</div></div>`;
  }
}

/* ===== DOMAINS ===== */
async function loadDomains(p) {
  try {
    const data = await api('/api/domains');
    p.innerHTML = `<div class="page-header"><h1>Domains</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Add Domain</button></div></div>`;
    p.innerHTML += tb('All Domains') + buildTable([
      {label:'ID',html:r=>esc(r.id)},
      {label:'Domain',html:r=>esc(r.domain)},
      {label:'Site ID',html:r=>esc(r.site_id)},
      {label:'PHP',html:r=>esc(r.php_version)},
      {label:'SSL',html:r=>r.ssl_enabled?'<span class="badge badge-ok">Enabled</span>':'<span class="badge badge-err">Disabled</span>'},
      {label:'Actions',html:r=>actionBtn('View','',"toast('View domain','info')")}
    ], (data.data||[]).filter(r=>!searchTerm||r.domain.includes(searchTerm)));
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load domains</div>'; }
}

/* ===== DATABASES ===== */
async function loadDatabases(p) {
  try {
    const data = await api('/api/databases');
    p.innerHTML = `<div class="page-header"><h1>Databases</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Create Database</button></div></div>`;
    p.innerHTML += tb('All Databases') + buildTable([
      {label:'ID',html:r=>esc(r.id)},{label:'Name',html:r=>esc(r.name)},{label:'Engine',html:r=>esc(r.engine)},{label:'Site ID',html:r=>esc(r.site_id)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'},{label:'Actions',html:r=>actionBtn('View','',"toast('View database','info')")}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load databases</div>'; }
}

/* ===== SSL ===== */
async function loadSsl(p) {
  try {
    const data = await api('/api/ssl');
    p.innerHTML = `<div class="page-header"><h1>SSL Certificates</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Request Certificate</button></div></div>`;
    p.innerHTML += tb('All Certificates') + buildTable([
      {label:'ID',html:r=>esc(r.id)},{label:'Domain',html:r=>esc(r.domain)},{label:'Provider',html:r=>esc(r.provider)},{label:'Status',html:r=>{let m={active:'badge-ok',requested:'badge-warn',placeholder:'badge-info'};return `<span class="badge ${m[r.status]||'badge-err'}">${esc(r.status)}</span>`;}},{label:'Expires',html:r=>esc(r.expires_at)},{label:'Actions',html:r=>actionBtn('Renew','',"toast('Renew certificate','info')")}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load SSL</div>'; }
}

/* ===== PROXY ===== */
async function loadProxy(p) {
  try {
    const data = await api('/api/proxy');
    p.innerHTML = `<div class="page-header"><h1>Reverse Proxy</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Add Proxy</button></div></div>`;
    p.innerHTML += tb('All Proxy Configs') + buildTable([
      {label:'ID',html:r=>esc(r.id)},{label:'Domain',html:r=>esc(r.domain)},{label:'Provider',html:r=>esc(r.provider)},{label:'Status',html:r=>esc(r.status)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'},{label:'Actions',html:r=>actionBtn('Open','',"toast('Open proxy config','info')")}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load proxy</div>'; }
}

/* ===== ACCESS ===== */
async function loadAccess(p) {
  try {
    const data = await api('/api/access-users');
    p.innerHTML = `<div class="page-header"><h1>Access Users</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Create User</button></div></div>`;
    p.innerHTML += tb('All Access Users') + buildTable([
      {label:'ID',html:r=>esc(r.id)},{label:'Username',html:r=>esc(r.username)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'},{label:'Actions',html:r=>actionBtn('Disable','',"toast('Toggle user','info')")}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load access users</div>'; }
}

/* ===== PROFILES ===== */
async function loadProfiles(p) {
  try {
    const data = await api('/api/profiles');
    p.innerHTML = `<div class="page-header"><h1>Configuration Profiles</h1></div>`;
    p.innerHTML += tb('All Profiles') + buildTable([
      {label:'Name',html:r=>esc(r.name)},{label:'Type',html:r=>esc(r.type)},{label:'Web Server',html:r=>esc(r.web_server)},{label:'Default',html:r=>r.default?'<span class="badge badge-ok">Yes</span>':''},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load profiles</div>'; }
}

/* ===== TEMPLATES ===== */
async function loadTemplates(p) {
  try {
    const data = await api('/api/profiles');
    const web = (data.data||[]).filter(r => r.type === 'web_server');
    p.innerHTML = `<div class="page-header"><h1>Web Server Templates</h1><div class="page-actions"><button class="btn btn-sm">Validate All</button><button class="btn btn-sm">Reload</button></div></div>`;
    p.innerHTML += tb('Templates') + buildTable([
      {label:'Name',html:r=>esc(r.name)},{label:'Web Server',html:r=>esc(r.web_server)},{label:'Valid',html:r=>'<span class="badge badge-ok">Valid</span>'}
    ], web, 'No templates');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load templates</div>'; }
}

/* ===== NODES ===== */
async function loadNodes(p) {
  p.innerHTML = `<div class="page-header"><h1>Nodes</h1></div>
    <div class="health-grid"><div class="health-item"><div class="health-dot ok"></div><div><div class="health-name">local</div><div class="health-label">Local node (default)</div></div></div></div>`;
  try {
    const data = await api('/api/nodes');
    p.innerHTML += `<div style="margin-top:16px">${tb('Node Details')}${buildTable([
      {label:'ID',html:r=>esc(r.id)},{label:'Name',html:r=>esc(r.name)},{label:'Type',html:r=>esc(r.type)}
    ], data.data||[], 'No nodes')}</div>`;
  } catch(e) { /* nodes may not be available */ }
}

/* ===== LOGS ===== */
async function loadLogs(p) {
  try {
    const data = await api('/api/logs');
    p.innerHTML = `<div class="page-header"><h1>Logs</h1><div class="page-actions"><button class="btn btn-sm" onclick="loadLogs($('page'))">Refresh</button></div></div>`;
    p.innerHTML += tb('System Logs','Filter logs...') + buildTable([
      {label:'Time',html:r=>esc(r.time)},
      {label:'Level',html:r=>{let m={info:'badge-info',warn:'badge-warn',error:'badge-err'};return `<span class="badge ${m[r.level]||'badge-info'}">${esc(r.level)}</span>`;}},
      {label:'Message',html:r=>esc(r.message)}
    ], data.data||[], 'No logs');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load logs</div>'; }
}

/* ===== SETTINGS ===== */
async function loadSettings(p) {
  p.innerHTML = `<div class="page-header"><h1>Settings</h1></div>
    <div class="details-panel"><div class="details-grid">
      <div class="details-field"><div class="details-label">Version</div><div class="details-value">0.1.0</div></div>
      <div class="details-field"><div class="details-label">API Port</div><div class="details-value">8080</div></div>
      <div class="details-field"><div class="details-label">Data Root</div><div class="details-value">/srv/containercp</div></div>
      <div class="details-field"><div class="details-label">Config Root</div><div class="details-value">/etc/containercp</div></div>
      <div class="details-field"><div class="details-label">Theme</div><div class="details-value" id="themeValue">Dark</div></div>
    </div></div>`;
}

/* ===== THEME ===== */
function toggleTheme() {
  const html = document.documentElement;
  const isDark = html.getAttribute('data-theme') !== 'light';
  html.setAttribute('data-theme', isDark ? 'light' : 'dark');
  const val = $('themeValue');
  if (val) val.textContent = isDark ? 'Light' : 'Dark';
}

/* ===== STATUS ===== */
async function updateStatus() {
  try {
    const h = await api('/api/health');
    const ok = h.data?.status === 'ok';
    $('statusDot').className = 'status-dot' + (ok?'':' error');
    $('statusLabel').textContent = ok ? 'Connected' : 'Error';
  } catch { $('statusDot').className = 'status-dot error'; $('statusLabel').textContent = 'Offline'; }
}

/* ===== SEARCH ===== */
document.addEventListener('input', e => {
  if (e.target.closest('.search-box input')) {
    const val = e.target.value;
    searchTerm = val;
    if (window.renderTable) window.renderTable();
  }
});

/* ===== INIT ===== */
document.addEventListener('DOMContentLoaded', () => {
  updateStatus();
  setInterval(updateStatus, 30000);
  qsa('.nav-link').forEach(link => {
    link.addEventListener('click', e => {
      e.preventDefault();
      navigate(link.dataset.page);
      const sb = $('sidebar');
      if (sb.classList.contains('open')) sb.classList.remove('open');
    });
  });
  $('sidebarToggle').addEventListener('click', () => $('sidebar').classList.toggle('open'));
  $('themeToggle').addEventListener('click', toggleTheme);
  navigate('dashboard');
});
