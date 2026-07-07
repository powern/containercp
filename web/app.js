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

async function apiPost(path, body) {
  return api(path, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
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
  else if (page === 'backups') loadBackups(p);
  else if (page === 'profiles') loadProfiles(p);
  else if (page === 'templates') loadTemplates(p);
  else if (page === 'nodes') loadNodes(p);
  else if (page === 'logs') loadLogs(p);
  else if (page === 'settings') loadSettings(p);
}

/* ===== DASHBOARD ===== */
async function loadDashboard(p) {
  try {
    const [health, sites, jobs] = await Promise.all([
      api('/api/health'), api('/api/sites'), api('/api/jobs')
    ]);
    const ok = health.data?.status === 'ok';
    const recentJobs = (jobs.data||[]).slice(-5).reverse();
    p.innerHTML = `
      <div class="page-header"><h1>Dashboard</h1></div>
      <div class="cards">
        ${card('Sites', (sites.data||[]).length, '#6366f1')}
        ${card('Domains', 0, '#3b82f6', 'loading...')}
        ${card('Backups', 0, '#8b5cf6', 'loading...')}
        ${card('SSL', 0, '#ec4899', 'loading...')}
      </div>
      <div class="health-grid">
        <div class="health-item"><div class="health-dot ${ok?'ok':'error'}"></div><div><div class="health-name">Daemon</div><div class="health-label">${ok?'Running':'Unavailable'}</div></div></div>
        <div class="health-item"><div class="health-dot ok"></div><div><div class="health-name">REST API</div><div class="health-label">Online</div></div></div>
        <div class="health-item"><div class="health-dot ok"></div><div><div class="health-name">Storage</div><div class="health-label">Loaded</div></div></div>
      </div>
      <div style="font-size:13px;color:var(--text2);margin-bottom:12px;font-weight:600;">Recent Jobs</div>
      <div class="activity-list">
        ${recentJobs.length ? recentJobs.map(j => `<div class="activity-item"><div class="activity-icon" style="background:${j.status==='completed'?'var(--green)':j.status==='failed'?'var(--red)':'var(--yellow)'}"></div><div class="activity-text">${esc(j.type)}: ${esc(j.message||j.status)}</div><div class="activity-time">${j.progress}%</div></div>`).join('') : '<div class="activity-item"><div class="activity-text" style="color:var(--text3)">No recent jobs</div></div>'}
      </div>`;
    // Load dynamic counts
    Promise.all([api('/api/domains'), api('/api/backups'), api('/api/ssl')]).then(([d,b,s])=>{
      const cards = qsa('.card .count');
      if (cards.length >= 4) { cards[1].textContent = (d.data||[]).length; cards[1].className='count'+(cards[1].textContent==='0'?' zero':''); }
      if (cards.length >= 4) { cards[2].textContent = (b.data||[]).length; cards[2].className='count'+(cards[2].textContent==='0'?' zero':''); }
      if (cards.length >= 4) { cards[3].textContent = (s.data||[]).length; cards[3].className='count'+(cards[3].textContent==='0'?' zero':''); }
    }).catch(()=>{});
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load dashboard</div>'; }
}

function card(label, count, color, sub) {
  return `<div class="card"><div class="card-header"><svg width="18" height="18" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="2"><circle cx="12" cy="12" r="10"/></svg><h3>${label}</h3></div><div class="count${count===0?' zero':''}">${sub||count}</div></div>`;
}

/* ===== TOOLBAR & TABLE ===== */
function tb(title) {
  return `<div class="table-toolbar"><div style="font-weight:600;font-size:14px;">${title}</div><div class="search-box"><svg width="14" height="14" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>&nbsp;<input type="text" placeholder="Search..." oninput="searchTerm=this.value;window.renderTable&&renderTable()"></div></div>`;
}

function buildTable(columns, rows, emptyMsg) {
  if (!rows||rows.length===0) return `<div class="empty-state"><svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="12" cy="12" r="10"/><path d="M8 12h8"/></svg><br>${emptyMsg||'No data'}</div>`;
  let h='<div class="table-wrap"><table><thead><tr>'+columns.map(c=>'<th>'+esc(c.label)+'</th>').join('')+'</tr></thead><tbody>';
  for (const row of rows) { h+='<tr>'+columns.map(c=>'<td>'+c.html(row)+'</td>').join('')+'</tr>'; }
  h+='</tbody></table></div>';
  return h;
}

/* ===== MODAL ===== */
function showModal(title, bodyHtml, width) {
  let overlay = document.getElementById('modal-overlay');
  if (!overlay) {
    overlay = document.createElement('div');
    overlay.id = 'modal-overlay';
    overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.5);z-index:9000;display:flex;align-items:center;justify-content:center;';
    overlay.addEventListener('click', e => { if (e.target === overlay) hideModal(); });
    document.body.appendChild(overlay);
  }
  overlay.innerHTML = `<div style="background:var(--surface);border:1px solid var(--border);border-radius:12px;width:${width||480}px;max-width:90vw;max-height:80vh;overflow-y:auto;">
    <div style="display:flex;justify-content:space-between;align-items:center;padding:16px 20px;border-bottom:1px solid var(--border);"><h2 style="font-size:16px;font-weight:600;">${esc(title)}</h2><button class="btn-icon" onclick="hideModal()" style="font-size:18px;">&times;</button></div>
    <div style="padding:20px;">${bodyHtml}</div></div>`;
  overlay.style.display = 'flex';
}

function hideModal() { const o=document.getElementById('modal-overlay'); if(o)o.style.display='none'; }

/* ===== SITES ===== */
async function loadSites(p) {
  try {
    const data = await api('/api/sites');
    p.innerHTML = `<div class="page-header"><h1>Sites</h1><div class="page-actions"><button class="btn btn-primary btn-sm" onclick="showCreateSiteWizard()">+ Create Site</button></div></div>`;
    p.innerHTML += tb('All Sites');
    window.renderTable = () => {
      const tbl = $('sites-table');
      if (!tbl) return;
      const filtered = (data.data||[]).filter(r => !searchTerm || r.domain.includes(searchTerm) || r.owner.includes(searchTerm));
      tbl.innerHTML = buildTable([
        {label:'Domain',html:r=>`<a href="#" onclick="navigate('site-detail',${r.id});return false" style="color:var(--primary);text-decoration:none;">${esc(r.domain)}</a>`},
        {label:'Owner',html:r=>esc(r.owner)},
        {label:'Actions',html:r=>`<button class="btn-icon" onclick="navigate('site-detail',${r.id})" title="View">&#128065;</button><button class="btn-icon" style="color:var(--red)" title="Remove" onclick="removeSite('${esc(r.domain)}')">&#10005;</button>`}
      ], filtered, 'No sites');
    };
    p.innerHTML += `<div id="sites-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load sites</div>'; }
}

async function removeSite(domain) {
  if (!confirm('Remove site '+domain+'? This cannot be undone.')) return;
  try {
    const res = await apiPost('/api/sites/remove', {domain});
    if (res.success) { toast('Site removed: '+domain, 'success'); loadSites($('page')); }
    else toast('Error: '+res.error, 'error');
  } catch(e) { toast('Network error', 'error'); }
}

/* ===== SITE CREATION WIZARD ===== */
function showCreateSiteWizard() {
  showModal('Create Site', `
    <div style="display:grid;gap:14px;">
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Owner</label><input id="wiz-owner" value="admin" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" oninput="document.getElementById('wiz-owner-err').textContent=''"><div id="wiz-owner-err" style="color:var(--red);font-size:11px;margin-top:2px;"></div></div>
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Domain</label><input id="wiz-domain" placeholder="example.com" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" oninput="document.getElementById('wiz-domain-err').textContent=''"><div id="wiz-domain-err" style="color:var(--red);font-size:11px;margin-top:2px;"></div></div>
      <button class="btn btn-primary" onclick="startSiteWizard()">Create Site</button>
    </div>`, 420);
}

async function startSiteWizard() {
  const owner = $('wiz-owner').value.trim();
  const domain = $('wiz-domain').value.trim();
  let valid = true;
  if (!owner) { $('wiz-owner-err').textContent = 'Owner is required'; valid = false; }
  if (!domain) { $('wiz-domain-err').textContent = 'Domain is required'; valid = false; }
  else if (!domain.includes('.')) { $('wiz-domain-err').textContent = 'Domain must contain a dot'; valid = false; }
  if (!valid) return;

  hideModal();

  // Show progress overlay
  const overlay = document.createElement('div');
  overlay.id = 'progress-overlay';
  overlay.style.cssText = 'position:fixed;inset:0;background:rgba(0,0,0,0.6);z-index:9500;display:flex;flex-direction:column;align-items:center;justify-content:center;';
  overlay.innerHTML = `
    <div style="background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:30px;width:400px;max-width:90vw;">
      <h3 style="margin-bottom:16px;">Deploying ${esc(domain)}</h3>
      <div style="height:8px;background:var(--bg3);border-radius:4px;overflow:hidden;margin-bottom:12px;"><div id="progress-bar" style="height:100%;width:0%;background:var(--primary);border-radius:4px;transition:width .3s;"></div></div>
      <div id="progress-step" style="font-size:13px;color:var(--text2);margin-bottom:4px;">Starting...</div>
      <div id="progress-status" style="font-size:11px;color:var(--text3);"></div>
    </div>`;
  document.body.appendChild(overlay);

  try {
    const res = await apiPost('/api/sites/create', {owner, domain});
    if (res.success) {
      $('progress-bar').style.width = '100%';
      $('progress-step').textContent = 'Site created successfully';
      $('progress-status').textContent = 'Completed';
      setTimeout(() => { document.getElementById('progress-overlay')?.remove(); navigate('sites'); }, 1500);
    } else {
      $('progress-step').textContent = 'Error: ' + (res.error||'Unknown');
      $('progress-status').textContent = 'Failed';
      $('progress-bar').style.background = 'var(--red)';
    }
  } catch(e) {
    $('progress-step').textContent = 'Network error';
    $('progress-status').textContent = 'Failed';
    $('progress-bar').style.background = 'var(--red)';
  }
}

/* ===== SITE DETAIL ===== */
async function loadSiteDetail(p, siteId) {
  try {
    const data = await api('/api/sites');
    const site = (data.data||[]).find(s => s.id == siteId);
    if (!site) { p.innerHTML = '<div class="empty-state">Site not found</div>'; return; }
    p.innerHTML = `
      <div class="page-header">
        <h1><a href="#" onclick="navigate('sites');return false" style="color:var(--text2);text-decoration:none;">&larr;</a> ${esc(site.domain)}</h1>
        <div class="page-actions"><button class="btn btn-sm btn-danger" onclick="removeSite('${esc(site.domain)}')">Remove</button></div>
      </div>
      <div class="details-panel" style="margin-bottom:16px;">
        <div class="details-grid">
          <div class="details-field"><div class="details-label">Domain</div><div class="details-value">${esc(site.domain)}</div></div>
          <div class="details-field"><div class="details-label">Owner</div><div class="details-value">${esc(site.owner)}</div></div>
          <div class="details-field"><div class="details-label">Node ID</div><div class="details-value">${site.node_id}</div></div>
        </div>
      </div>
      <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px;" id="site-rel"></div>`;
    // Load related resources
    const [domains, databases, ssl, proxy, backups] = await Promise.all([
      api('/api/domains'), api('/api/databases'), api('/api/ssl'), api('/api/proxy'), api('/api/backups')
    ]);
    const rel = $('site-rel');
    const related = [
      {label:'Domains',items:(domains.data||[]).filter(d=>d.site_id==site.id).map(d=>d.domain),color:'#3b82f6'},
      {label:'Databases',items:(databases.data||[]).filter(d=>d.site_id==site.id).map(d=>d.name),color:'#8b5cf6'},
      {label:'SSL',items:(ssl.data||[]).filter(c=>c.site_id==site.id).map(c=>c.status),color:'#ec4899'},
      {label:'Proxy',items:(proxy.data||[]).filter(p=>p.site_id==site.id).map(p=>p.status),color:'#06b6d4'},
      {label:'Backups',items:(backups.data||[]).filter(b=>b.site_id==site.id).map(b=>b.filename),color:'#f97316'}
    ];
    rel.innerHTML = related.map(r => `<div class="card"><h3>${r.label}</h3><div class="count${r.items.length===0?' zero':''}">${r.items.length || 0}</div><div style="font-size:11px;color:var(--text3);margin-top:4px;">${r.items.slice(0,2).join(', ')}${r.items.length>2?'...':''}</div></div>`).join('');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load site</div>'; }
}

/* ===== DOMAINS ===== */
async function loadDomains(p) {
  try {
    const data = await api('/api/domains');
    p.innerHTML = `<div class="page-header"><h1>Domains</h1></div>`;
    p.innerHTML += tb('All Domains');
    window.renderTable = () => {
      const tbl = $('domains-table');
      if (!tbl) return;
      tbl.innerHTML = buildTable([
        {label:'Domain',html:r=>esc(r.domain)},{label:'Site ID',html:r=>esc(r.site_id)},{label:'SSL',html:r=>r.ssl_enabled?'<span class="badge badge-ok">Enabled</span>':'<span class="badge badge-err">Disabled</span>'},
        {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeDomain('${esc(r.domain)}')">&#10005;</button>`}
      ], (data.data||[]).filter(r=>!searchTerm||r.domain.includes(searchTerm)));
    };
    p.innerHTML += `<div id="domains-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load domains</div>'; }
}

async function removeDomain(domain) {
  if (!confirm('Remove domain '+domain+'?')) return;
  try { const res = await apiPost('/api/domains/remove',{domain}); if(res.success){toast('Domain removed','success');loadDomains($('page'));}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

/* ===== DATABASES ===== */
async function loadDatabases(p) {
  try {
    const data = await api('/api/databases');
    p.innerHTML = `<div class="page-header"><h1>Databases</h1></div>`;
    p.innerHTML += tb('All Databases');
    window.renderTable = () => {
      const tbl = $('dbs-table');
      if (!tbl) return;
      tbl.innerHTML = buildTable([
        {label:'Name',html:r=>esc(r.name)},{label:'Engine',html:r=>esc(r.engine)},{label:'Site ID',html:r=>esc(r.site_id)},
        {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeDatabase('${esc(r.name)}')">&#10005;</button>`}
      ], data.data||[]);
    };
    p.innerHTML += `<div id="dbs-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load databases</div>'; }
}

async function removeDatabase(name) {
  if (!confirm('Remove database?')) return;
  try { const res = await apiPost('/api/databases/remove',{name}); if(res.success){toast('Database removed','success');loadDatabases($('page'));}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

/* ===== SSL ===== */
async function loadSsl(p) {
  try {
    const data = await api('/api/ssl');
    p.innerHTML = `<div class="page-header"><h1>SSL Certificates</h1></div>`;
    p.innerHTML += tb('All Certificates');
    window.renderTable = () => {
      const tbl = $('ssl-table');
      if (!tbl) return;
      tbl.innerHTML = buildTable([
        {label:'Domain',html:r=>esc(r.domain)},{label:'Provider',html:r=>esc(r.provider)},
        {label:'Status',html:r=>{let m={active:'badge-ok',requested:'badge-warn',placeholder:'badge-info',disabled:'badge-err'};return `<span class="badge ${m[r.status]||'badge-err'}">${esc(r.status)}</span>`;}},
        {label:'Actions',html:r=>`<button class="btn-icon" onclick="toggleSsl('${esc(r.domain)}',${!r.enabled})" title="${r.enabled?'Disable':'Enable'}">${r.enabled?'&#9646;&#9646;':'&#9654;'}</button><button class="btn-icon" style="color:var(--red)" onclick="removeSsl('${esc(r.domain)}')">&#10005;</button>`}
      ], data.data||[]);
    };
    p.innerHTML += `<div id="ssl-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load SSL</div>'; }
}

async function toggleSsl(domain, enable) {
  const ep = enable ? '/api/ssl/enable' : '/api/ssl/disable';
  try { const res = await apiPost(ep,{domain}); if(res.success){toast('SSL '+(enable?'enabled':'disabled'),'success');loadSsl($('page'));}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}
async function removeSsl(domain) {
  if (!confirm('Remove SSL certificate?')) return;
  try { const res = await apiPost('/api/ssl/remove',{domain}); if(res.success){toast('SSL removed','success');loadSsl($('page'));}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

/* ===== PROXY ===== */
async function loadProxy(p) {
  try {
    const data = await api('/api/proxy');
    p.innerHTML = `<div class="page-header"><h1>Reverse Proxy</h1></div>`;
    p.innerHTML += tb('All Proxy Configs');
    window.renderTable = () => {
      const tbl = $('proxy-table');
      if (!tbl) return;
      tbl.innerHTML = buildTable([
        {label:'Domain',html:r=>esc(r.domain)},{label:'Provider',html:r=>esc(r.provider)},{label:'Status',html:r=>esc(r.status)},
        {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeProxy('${esc(r.domain)}')">&#10005;</button>`}
      ], data.data||[]);
    };
    p.innerHTML += `<div id="proxy-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load proxy</div>'; }
}

async function removeProxy(domain) {
  if (!confirm('Remove proxy config?')) return;
  try { const res = await apiPost('/api/proxy/remove',{domain}); if(res.success){toast('Proxy removed','success');loadProxy($('page'));}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

/* ===== ACCESS ===== */
async function loadAccess(p) {
  try {
    const data = await api('/api/access-users');
    p.innerHTML = `<div class="page-header"><h1>Access Users</h1></div>`;
    p.innerHTML += tb('All Access Users');
    window.renderTable = () => {
      const tbl = $('access-table');
      if (!tbl) return;
      tbl.innerHTML = buildTable([
        {label:'Username',html:r=>esc(r.username)},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'},
        {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeAccessUser('${esc(r.username)}')">&#10005;</button>`}
      ], data.data||[]);
    };
    p.innerHTML += `<div id="access-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load access users</div>'; }
}

async function removeAccessUser(username) {
  if (!confirm('Remove access user?')) return;
  try { const res = await apiPost('/api/access-users/remove',{username}); if(res.success){toast('User removed','success');loadAccess($('page'));}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

/* ===== BACKUPS ===== */
async function loadBackups(p) {
  try {
    const data = await api('/api/backups');
    p.innerHTML = `<div class="page-header"><h1>Backups</h1><div class="page-actions"><button class="btn btn-primary btn-sm" onclick="showBackupModal()">+ Create Backup</button></div></div>`;
    p.innerHTML += tb('All Backups') + buildTable([
      {label:'Filename',html:r=>esc(r.filename)},{label:'Size',html:r=>esc(r.size)+' bytes'},{label:'Status',html:r=>{let m={completed:'badge-ok',failed:'badge-err'};return `<span class="badge ${m[r.status]||'badge-info'}">${esc(r.status)}</span>`;}},{label:'Date',html:r=>esc(r.created_at)},
      {label:'Actions',html:r=>r.status==='completed'?`<button class="btn-icon" title="Restore">&#8635;</button><button class="btn-icon" style="color:var(--red)" onclick="removeBackup(${r.id})">&#10005;</button>`:''}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load backups</div>'; }
}

function showBackupModal() {
  showModal('Create Backup', '<div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Domain</label><input id="bk-domain" placeholder="example.com" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div><button class="btn btn-primary" onclick="createBackup()" style="margin-top:12px;">Create Backup</button>', 400);
}

async function createBackup() {
  const domain = $('bk-domain').value.trim();
  if (!domain) { toast('Domain required', 'error'); return; }
  hideModal();
  try {
    const res = await apiPost('/api/backups/create',{domain});
    if (res.success) { toast('Backup created: '+res.data.filename, 'success'); loadBackups($('page')); }
    else toast('Error: '+(res.error||'Unknown'), 'error');
  } catch(e) { toast('Network error', 'error'); }
}

async function removeBackup(id) {
  if (!confirm('Remove backup?')) return;
  const res = await apiPost('/api/backups/remove',{id});
}

/* ===== PROFILES, TEMPLATES, NODES, LOGS, SETTINGS ===== */
async function loadProfiles(p) {
  try {
    const data = await api('/api/profiles');
    p.innerHTML = `<div class="page-header"><h1>Configuration Profiles</h1></div>`;
    p.innerHTML += tb('All Profiles') + buildTable([
      {label:'Name',html:r=>esc(r.name)},{label:'Type',html:r=>esc(r.type)},{label:'Web Server',html:r=>esc(r.web_server)},{label:'Default',html:r=>r.default?'<span class="badge badge-ok">Yes</span>':''},{label:'Enabled',html:r=>r.enabled?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-err">No</span>'}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load profiles</div>'; }
}

async function loadTemplates(p) {
  try {
    const data = await api('/api/profiles');
    const web = (data.data||[]).filter(r => r.type === 'web_server');
    p.innerHTML = `<div class="page-header"><h1>Web Server Templates</h1></div>`;
    p.innerHTML += tb('Templates') + buildTable([
      {label:'Name',html:r=>esc(r.name)},{label:'Web Server',html:r=>esc(r.web_server)},{label:'Valid',html:r=>'<span class="badge badge-ok">Valid</span>'}
    ], web, 'No templates');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load templates</div>'; }
}

async function loadNodes(p) {
  try {
    const data = await api('/api/nodes');
    p.innerHTML = `<div class="page-header"><h1>Nodes</h1></div>`;
    p.innerHTML += tb('Node Details') + buildTable([
      {label:'ID',html:r=>esc(r.id)},{label:'Name',html:r=>esc(r.name)},{label:'Type',html:r=>esc(r.type)}
    ], data.data||[], 'No nodes');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load nodes</div>'; }
}

async function loadLogs(p) {
  try {
    const data = await api('/api/logs');
    p.innerHTML = `<div class="page-header"><h1>Logs</h1><div class="page-actions"><button class="btn btn-sm" onclick="loadLogs($('page'))">Refresh</button></div></div>`;
    p.innerHTML += tb('System Logs') + buildTable([
      {label:'Time',html:r=>esc(r.time)},{label:'Level',html:r=>{let m={info:'badge-info',warn:'badge-warn',error:'badge-err'};return `<span class="badge ${m[r.level]||'badge-info'}">${esc(r.level)}</span>`;}},{label:'Message',html:r=>esc(r.message)}
    ], data.data||[], 'No logs');
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load logs</div>'; }
}

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
    searchTerm = e.target.value;
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
