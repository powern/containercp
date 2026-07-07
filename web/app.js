/* ===== API ===== */
async function api(path) {
  const res = await fetch(path);
  return res.json();
}

/* ===== UTILS ===== */
function esc(s) { return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
function $(id) { return document.getElementById(id); }
function qs(s) { return document.querySelector(s); }
function qsa(s) { return document.querySelectorAll(s); }

/* ===== STATE ===== */
let currentPage = 'dashboard';
let searchTerm = '';

/* ===== NAVIGATION ===== */
function navigate(page) {
  currentPage = page;
  qsa('.nav-link').forEach(l => l.classList.toggle('active', l.dataset.page===page));
  loadPage(page);
}

function loadPage(page) {
  const p = $('page');
  if (page==='dashboard') loadDashboard(p);
  else if (page==='sites') loadSites(p);
  else if (page==='domains') loadDomains(p);
  else if (page==='databases') loadDatabases(p);
  else if (page==='ssl') loadSsl(p);
  else if (page==='proxy') loadProxy(p);
  else if (page==='access') loadAccess(p);
  else if (page==='profiles') loadProfiles(p);
  else if (page==='templates') loadTemplates(p);
  else if (page==='nodes') loadNodes(p);
  else if (page==='logs') loadLogs(p);
  else if (page==='settings') loadSettings(p);
}

/* ===== DASHBOARD ===== */
async function loadDashboard(p) {
  const [health, sites, domains, databases, ssl, proxy, access, users, nodes] = await Promise.all([
    api('/api/health'), api('/api/sites'), api('/api/domains'),
    api('/api/databases'), api('/api/ssl'), api('/api/proxy'),
    api('/api/access-users'), api('/api/users'), api('/api/nodes')
  ]);

  const ok = health.data?.status === 'ok';

  p.innerHTML = `
    <div class="page-header"><h1>Dashboard</h1></div>
    <div class="cards">
      ${card('Sites', sites.data?.length||0, '#6366f1')}
      ${card('Domains', domains.data?.length||0, '#3b82f6')}
      ${card('Databases', databases.data?.length||0, '#8b5cf6')}
      ${card('SSL', ssl.data?.length||0, '#ec4899')}
      ${card('Proxy', proxy.data?.length||0, '#06b6d4')}
      ${card('Access', access.data?.length||0, '#f97316')}
      ${card('Users', users.data?.length||0, '#22c55e')}
      ${card('Nodes', nodes.data?.length||0, '#eab308')}
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
      <div class="activity-item"><div class="activity-icon" style="background:var(--blue)"></div><div class="activity-text">API server listening on 127.0.0.1:8080</div><div class="activity-time">just now</div></div>
      <div class="activity-item"><div class="activity-icon" style="background:var(--yellow)"></div><div class="activity-text">Storage loaded (${sites.data?.length||0} sites, ${domains.data?.length||0} domains)</div><div class="activity-time">just now</div></div>
    </div>`;
}

function card(label, count, color) {
  return `<div class="card"><div class="card-header"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="2"><circle cx="12" cy="12" r="10"/></svg><h3>${label}</h3></div><div class="count${count===0?' zero':''}">${count}</div></div>`;
}

function healthItem(name, ok) {
  return `<div class="health-item"><div class="health-dot ${ok?'ok':'error'}"></div><div><div class="health-name">${name}</div><div class="health-label">${ok?'Running':'Unavailable'}</div></div></div>`;
}

/* ===== TABLE BUILDER ===== */
function tableToolbar(title, placeholder) {
  return `<div class="table-toolbar"><div style="font-weight:600;font-size:14px;">${title}</div><div class="search-box"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg><input type="text" placeholder="${placeholder||'Search...'}" oninput="searchTerm=this.value;renderTable()"></div></div>`;
}

function buildTable(columns, rows, emptyMsg) {
  if (!rows || rows.length===0) return `<div class="empty-state"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><path d="M8 12h8"/></svg><br>${emptyMsg||'No data'}</div>`;
  let h = '<div class="table-wrap"><table><thead><tr>'+columns.map(c=>'<th>'+esc(c.label)+'</th>').join('')+'</tr></thead><tbody>';
  for (const row of rows) {
    h += '<tr>'+columns.map(c=>'<td>'+c.html(row)+'</td>').join('')+'</tr>';
  }
  h += '</tbody></table></div>';
  return h;
}

function td(v) { return esc(v); }

/* ===== SITES ===== */
async function loadSites(p) {
  const data = await api('/api/sites');
  p.innerHTML = `<div class="page-header"><h1>Sites</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Create Site</button></div></div>`;
  p.innerHTML += tableToolbar('All Sites', 'Search domains...');
  p.innerHTML += buildTable(
    [{label:'ID',html:r=>td(r.id)},{label:'Domain',html:r=>td(r.domain)},{label:'Owner',html:r=>td(r.owner)},{label:'Actions',html:r=>`<button class="btn-icon" title="Open">&#128279;</button><button class="btn-icon" title="Start">&#9654;</button><button class="btn-icon" title="Stop">&#9646;&#9646;</button><button class="btn-icon" title="Remove" style="color:var(--red)">&#10005;</button>`}],
    (data.data||[]).filter(r=>!searchTerm||r.domain.includes(searchTerm)||r.owner.includes(searchTerm))
  );
}

/* ===== DOMAINS ===== */
async function loadDomains(p) {
  const data = await api('/api/domains');
  p.innerHTML = `<div class="page-header"><h1>Domains</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Add Domain</button></div></div>`;
  p.innerHTML += tableToolbar('All Domains');
  p.innerHTML += buildTable(
    [{label:'ID',html:r=>td(r.id)},{label:'Domain',html:r=>td(r.domain)},{label:'Site ID',html:r=>td(r.site_id)},{label:'PHP',html:r=>td(r.php_version)},{label:'SSL',html:r=>r.ssl_enabled?'<span class="badge badge-ok">Enabled</span>':'<span class="badge badge-err">Disabled</span>'}],
    (data.data||[]).filter(r=>!searchTerm||r.domain.includes(searchTerm))
  );
}

/* ===== DATABASES ===== */
async function loadDatabases(p) {
  const data = await api('/api/databases');
  p.innerHTML = `<div class="page-header"><h1>Databases</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Create Database</button></div></div>`;
  p.innerHTML += tableToolbar('All Databases');
  p.innerHTML += buildTable(
    [{label:'ID',html:r=>td(r.id)},{label:'Name',html:r=>td(r.name)},{label:'Engine',html:r=>td(r.engine)},{label:'Site ID',html:r=>td(r.site_id)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'}],
    data.data||[]
  );
}

/* ===== SSL ===== */
async function loadSsl(p) {
  const data = await api('/api/ssl');
  p.innerHTML = `<div class="page-header"><h1>SSL Certificates</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Request Certificate</button></div></div>`;
  p.innerHTML += tableToolbar('All Certificates');
  p.innerHTML += buildTable(
    [{label:'ID',html:r=>td(r.id)},{label:'Domain',html:r=>td(r.domain)},{label:'Provider',html:r=>td(r.provider)},{label:'Status',html:r=>{let m={active:'badge-ok',requested:'badge-warn',placeholder:'badge-info'};return `<span class="badge ${m[r.status]||'badge-err'}">${esc(r.status)}</span>`;}},{label:'Expires',html:r=>td(r.expires_at)}],
    data.data||[]
  );
}

/* ===== PROXY ===== */
async function loadProxy(p) {
  const data = await api('/api/proxy');
  p.innerHTML = `<div class="page-header"><h1>Reverse Proxy</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Add Proxy</button></div></div>`;
  p.innerHTML += tableToolbar('All Proxy Configs');
  p.innerHTML += buildTable(
    [{label:'ID',html:r=>td(r.id)},{label:'Domain',html:r=>td(r.domain)},{label:'Provider',html:r=>td(r.provider)},{label:'Status',html:r=>td(r.status)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'}],
    data.data||[]
  );
}

/* ===== ACCESS ===== */
async function loadAccess(p) {
  const data = await api('/api/access-users');
  p.innerHTML = `<div class="page-header"><h1>Access Users</h1><div class="page-actions"><button class="btn btn-primary btn-sm">+ Create User</button></div></div>`;
  p.innerHTML += tableToolbar('All Access Users');
  p.innerHTML += buildTable(
    [{label:'ID',html:r=>td(r.id)},{label:'Username',html:r=>td(r.username)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'}],
    data.data||[]
  );
}

/* ===== PROFILES ===== */
async function loadProfiles(p) {
  p.innerHTML = `<div class="page-header"><h1>Configuration Profiles</h1></div>
  <div class="tabs"><div class="tab active">Web Server</div><div class="tab">PHP</div><div class="tab">Docker</div><div class="tab">SSL</div></div>
  <div class="table-container">${tableToolbar('Profiles')}${buildTable(
    [{label:'Name',html:r=>td(r.name)},{label:'Type',html:r=>td(r.type)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'}],
    [{name:'nginx-php-default',type:'web_server',enabled:true},{name:'nginx-wordpress',type:'web_server',enabled:true},{name:'apache-php-default',type:'web_server',enabled:true}]
  )}</div>`;
}

/* ===== TEMPLATES ===== */
async function loadTemplates(p) {
  p.innerHTML = `<div class="page-header"><h1>Web Server Templates</h1><div class="page-actions"><button class="btn btn-sm">Validate All</button><button class="btn btn-sm">Reload</button></div></div>`;
  p.innerHTML += tableToolbar('Templates');
  p.innerHTML += buildTable(
    [{label:'Name',html:r=>td(r.name)},{label:'Web Server',html:r=>td(r.server)},{label:'Path',html:r=>td(r.path)},{label:'Valid',html:r=>r.valid?'<span class="badge badge-ok">Valid</span>':'<span class="badge badge-warn">Unknown</span>'}],
    [{name:'nginx-php-default',server:'nginx',path:'/etc/containercp/templates/web/nginx-php-default.conf.template',valid:true},
     {name:'nginx-wordpress',server:'nginx',path:'/etc/containercp/templates/web/nginx-wordpress.conf.template',valid:true},
     {name:'nginx-laravel',server:'nginx',path:'/etc/containercp/templates/web/nginx-laravel.conf.template',valid:true},
     {name:'apache-php-default',server:'apache',path:'/etc/containercp/templates/web/apache-php-default.conf.template',valid:true}]
  );
}

/* ===== NODES ===== */
async function loadNodes(p) {
  const data = await api('/api/health');
  p.innerHTML = `<div class="page-header"><h1>Nodes</h1></div>
  <div class="health-grid">
    <div class="health-item"><div class="health-dot ok"></div><div><div class="health-name">local</div><div class="health-label">Local node (default)</div></div></div>
  </div>
  <div class="table-container" style="margin-top:16px">${tableToolbar('Node Details')}${buildTable(
    [{label:'Property',html:r=>td(r.prop)},{label:'Value',html:r=>td(r.val)}],
    [{prop:'Name',val:'local'},{prop:'Type',val:'local'},{prop:'Status',val:'online'},{prop:'Docker',val:'checking...'},{prop:'Proxy',val:'nginx'}]
  )}</div>`;
}

/* ===== LOGS ===== */
async function loadLogs(p) {
  p.innerHTML = `<div class="page-header"><h1>Logs</h1><div class="page-actions"><button class="btn btn-sm">Refresh</button></div></div>
  <div class="table-container">${tableToolbar('System Logs','Filter logs...')}${buildTable(
    [{label:'Time',html:r=>td(r.time)},{label:'Level',html:r=>{let m={info:'badge-info',warn:'badge-warn',error:'badge-err'};return `<span class="badge ${m[r.level]||'badge-info'}">${esc(r.level)}</span>`;}},{label:'Message',html:r=>td(r.msg)}],
    [{time:new Date().toISOString(),level:'info',msg:'ContainerCP daemon started'},{time:new Date().toISOString(),level:'info',msg:'REST API listening on 127.0.0.1:8080'}]
  )}</div>`;
}

/* ===== SETTINGS ===== */
async function loadSettings(p) {
  p.innerHTML = `<div class="page-header"><h1>Settings</h1></div>
  <div class="details-panel">
    <div class="details-grid">
      <div class="details-field"><div class="details-label">Version</div><div class="details-value">0.1.0</div></div>
      <div class="details-field"><div class="details-label">API Port</div><div class="details-value">8080</div></div>
      <div class="details-field"><div class="details-label">Data Root</div><div class="details-value">/srv/containercp</div></div>
      <div class="details-field"><div class="details-label">Config Root</div><div class="details-value">/etc/containercp</div></div>
      <div class="details-field"><div class="details-label">Theme</div><div class="details-value" id="themeValue">Dark</div></div>
    </div>
  </div>`;
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
  } catch {
    $('statusDot').className = 'status-dot error';
    $('statusLabel').textContent = 'Offline';
  }
}

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

  $('sidebarToggle').addEventListener('click', () => {
    $('sidebar').classList.toggle('open');
  });

  $('themeToggle').addEventListener('click', toggleTheme);

  navigate('dashboard');
});
