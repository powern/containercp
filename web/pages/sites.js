import {
  api, apiPost, buildTable, card, esc, hideModal, navigate, pageHeader, pollJobProgress, pollRotationJob, renderWordPressRotationDiagnostics, showModal, summaryCards, tb, toast
} from '../core/context.js';


/* ===== SITES ===== */
let activeSitesLifecycle = null;

async function loadSites(p, params, lifecycle) {
  activeSitesLifecycle = lifecycle || activeSitesLifecycle;
  try {
    const data = await api('/api/sites');
    if (lifecycle && !lifecycle.isActive()) return;
    const sites = data.data || [];
    const statusOf = (s) => [s.web_status, s.php_status, s.https_status].filter(Boolean);
    const critical = sites.filter(s => statusOf(s).some(v => ['Stopped','Unhealthy','Error','Expired'].includes(v))).length;
    const warning = sites.filter(s => !critical && statusOf(s).some(v => ['Starting','Expiring','Issuing','Not verified'].includes(v))).length;
    const healthy = sites.filter(s => statusOf(s).some(v => ['Running','Active','Available'].includes(v))).length;
    p.innerHTML = pageHeader('Sites', 'Health-focused site inventory with runtime, HTTPS, ownership, and operational actions.', '<button class="btn btn-primary btn-sm" onclick="showCreateSiteWizard()">+ Create Site</button>', 'Hosting')
      + summaryCards([
        {label:'Total Sites', value:sites.length, tone:'neutral', help:'Managed site records'},
        {label:'Healthy', value:healthy, tone:'healthy', help:'Runtime or HTTPS reports OK'},
        {label:'Warning', value:warning, tone:'warning', help:'Needs verification or transition'},
        {label:'Critical', value:critical, tone:'critical', help:'Stopped, unhealthy, expired, or error'}
      ]);
    p.innerHTML += tb('All Sites');
    const render = () => {
      const tbl = $('sites-table');
      if (!tbl) return;
      const filtered = sites.filter(r => !window.searchTerm || r.domain.includes(window.searchTerm) || r.owner.includes(window.searchTerm));
      const rtM = {'Running':'badge-ok','Active':'badge-ok','Available':'badge-ok','Stopped':'badge-err','Unhealthy':'badge-warn','Starting':'badge-warn','Expiring':'badge-warn','Error':'badge-err','Expired':'badge-err','Disabled':'badge-info','Not verified':'badge-info','Issuing':'badge-warn','Unknown':'badge-info','N/A':'badge-info'};
      tbl.innerHTML = buildTable([
        {label:'Domain',html:r=>`<a href="#" onclick="navigate('site-detail',${r.id});return false" style="color:var(--primary);text-decoration:none;">${esc(r.domain)}</a>`},
        {label:'Web',html:r=>r.web_status?`<span class="badge ${rtM[r.web_status]||'badge-info'}">${esc(r.web_status)}</span>`:`<span data-rt-id="${r.id}" data-rt-service="web" class="badge badge-info">...</span>`},
        {label:'PHP',html:r=>r.php_status?`<span class="badge ${rtM[r.php_status]||'badge-info'}">${esc(r.php_status)}</span>`:`<span data-rt-id="${r.id}" data-rt-service="php" class="badge badge-info">...</span>`},
        {label:'HTTPS',html:r=>r.https_status?`<span class="badge ${rtM[r.https_status]||'badge-info'}">${esc(r.https_status)}</span>`:`<span data-rt-id="${r.id}" data-rt-service="https" class="badge badge-info">...</span>`},
        {label:'Owner',html:r=>esc(r.owner)},
        {label:'Backend',html:r=>r.web_server==='nginx'?'<span class="badge badge-info">Nginx</span>':'<span class="badge badge-ok">Apache2</span>'},
        {label:'Actions',html:r=>`<button class="btn-icon" onclick="navigate('site-detail',${r.id})" title="View">&#128065;</button>${r.can_delete!==false?`<button class="btn-icon" style="color:var(--red)" title="Remove" onclick="removeSite('${esc(r.domain)}')">&#10005;</button>`:''}`}
      ], filtered, 'No sites');
      // Fetch runtime + HTTPS status for each site
      filtered.forEach(site => {
        if (site.web_status || site.php_status || site.https_status) return; // already has explicit status
        api('/api/runtime/' + site.id).then(rt => {
          if (activeSitesLifecycle && !activeSitesLifecycle.isActive()) return;
          if (!rt.success) return;
          const update = (srv, val) => {
            const el = tbl.querySelector(`span[data-rt-id="${site.id}"][data-rt-service="${srv}"]`);
            if (el) { el.className = 'badge ' + (rtM[val]||'badge-info'); el.textContent = val; }
          };
          update('web', rt.data.web);
          update('php', rt.data.php);
          update('https', rt.data.https);
        }).catch(()=>{});
      });
    };
    if (lifecycle && lifecycle.setRenderTable) lifecycle.setRenderTable(render);
    else window.renderTable = render;
    p.innerHTML += `<div id="sites-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load sites</div>'; }
}



async function removeSite(domain) {
  if (!confirm('Remove site '+domain+'? This cannot be undone.')) return;
  try {
    const res = await apiPost('/api/sites/remove', {domain});
    if (res.success) { toast('Site removed: '+domain, 'success'); loadSites($('page'), null, activeSitesLifecycle); }
    else toast('Error: '+res.error, 'error');
  } catch(e) { toast('Network error', 'error'); }
}

/* ===== SITE CREATION WIZARD ===== */
let siteWizardTemplates = [];

async function showCreateSiteWizard() {
  try {
    const res = await api('/api/profiles');
    siteWizardTemplates = (res.data || []).filter(p => p.type === 'web_server');
  } catch(e) {
    siteWizardTemplates = [];
  }
  showModal('Create Site', `
    <div style="display:grid;gap:14px;">
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Owner</label><input id="wiz-owner" value="admin" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" oninput="document.getElementById('wiz-owner-err').textContent=''"><div id="wiz-owner-err" style="color:var(--red);font-size:11px;margin-top:2px;"></div></div>
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Domain</label><input id="wiz-domain" placeholder="example.com" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" oninput="document.getElementById('wiz-domain-err').textContent=''"><div id="wiz-domain-err" style="color:var(--red);font-size:11px;margin-top:2px;"></div></div>
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Backend Web Server</label>
        <select id="wiz-backend" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
          <option value="apache">Apache2</option>
          <option value="nginx">Nginx</option>
        </select>
      </div>
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Template</label>
        <select id="wiz-template" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></select>
      </div>
      <div id="wiz-summary" style="font-size:12px;color:var(--text3);background:var(--bg2);padding:8px 12px;border-radius:6px;margin-top:4px;">Backend: Apache2 with selected template</div>
      <button class="btn btn-primary" onclick="startSiteWizard()">Create Site</button>
    </div>`, 420);
  renderSiteWizardTemplateOptions();
  document.getElementById('wiz-backend').addEventListener('change', renderSiteWizardTemplateOptions);
}

function renderSiteWizardTemplateOptions() {
  const backend = document.getElementById('wiz-backend')?.value || 'apache';
  const select = document.getElementById('wiz-template');
  const summary = document.getElementById('wiz-summary');
  if (!select) return;
  const items = siteWizardTemplates.filter(t => t.web_server === backend);
  if (items.length) {
    items.sort((a, b) => (a.default === b.default ? String(a.name).localeCompare(String(b.name)) : (a.default ? -1 : 1)));
    select.innerHTML = items.map(t => `<option value="${backend}:${esc(t.name)}">${esc(t.name)}${t.default ? ' (default)' : ''}</option>`).join('');
  } else {
    select.innerHTML = `<option value="${backend}:">Use backend default</option>`;
  }
  if (summary) summary.textContent = `Backend: ${backend === 'nginx' ? 'Nginx' : 'Apache2'} with ${items.length ? 'selected template' : 'backend default template'}`;
}

async function startSiteWizard() {
  const owner = $('wiz-owner').value.trim();
  const domain = $('wiz-domain').value.trim();
  const profile = $('wiz-template') ? $('wiz-template').value : (($('wiz-backend') ? $('wiz-backend').value : 'apache') + ':');
  let valid = true;
  if (!owner) { $('wiz-owner-err').textContent = 'Owner is required'; valid = false; }
  if (!domain) { $('wiz-domain-err').textContent = 'Domain is required'; valid = false; }
  else if (!domain.includes('.')) { $('wiz-domain-err').textContent = 'Domain must contain a dot'; valid = false; }
  if (!valid) return;

  hideModal();

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
    const res = await apiPost('/api/sites/create', {owner, domain, profile});
    if (res.success) {
      // Poll job progress for real-time deployment steps
      const jobId = res.data.job_id;
      if (jobId) {
        pollJobProgress(jobId, () => {
          $('progress-bar').style.width = '100%';
          $('progress-step').textContent = 'Site created successfully';
          $('progress-status').textContent = 'Completed';
          const ctx = activeSitesLifecycle;
          const later = ctx && ctx.setTimeout ? ctx.setTimeout.bind(ctx) : setTimeout;
          later(() => { document.getElementById('progress-overlay')?.remove(); navigate('sites'); }, 1500);
        }, activeSitesLifecycle);
      } else {
        $('progress-bar').style.width = '100%';
        $('progress-step').textContent = 'Site created successfully';
        $('progress-status').textContent = 'Completed';
        const ctx = activeSitesLifecycle;
        const later = ctx && ctx.setTimeout ? ctx.setTimeout.bind(ctx) : setTimeout;
        later(() => { document.getElementById('progress-overlay')?.remove(); navigate('sites'); }, 1500);
      }
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

/* ===== JOB PROGRESS POLLING ===== *//* ===== SITE DETAIL ===== */
async function loadSiteDetail(p, siteId, lifecycle) {
  activeSitesLifecycle = lifecycle || activeSitesLifecycle;
  try {
    const data = await api('/api/sites');
    if (lifecycle && !lifecycle.isActive()) return;
    const site = (data.data||[]).find(s => s.id == siteId);
    if (!site) { p.innerHTML = '<div class="empty-state">Site not found</div>'; return; }

    var isSystem = site.system_role === 'admin-panel';

    p.innerHTML = `
      <div class="page-header">
        <h1><a href="#" onclick="navigate('sites');return false" style="color:var(--text2);text-decoration:none;">&larr;</a> ${esc(site.domain)}</h1>
        <div class="page-actions">${site.can_delete!==false?`<button class="btn btn-sm btn-danger" onclick="removeSite('${esc(site.domain)}')">Remove</button>`:''}</div>
      </div>
      <div class="details-panel" style="margin-bottom:16px;">
        <div class="details-grid">
          <div class="details-field"><div class="details-label">Domain</div><div class="details-value">${esc(site.domain)}</div></div>
          ${isSystem ? `
          <div class="details-field"><div class="details-label">Role</div><div class="details-value"><span class="badge badge-admin">${esc(site.system_role||'system')}</span></div></div>
          <div class="details-field"><div class="details-label">Proxy Upstream</div><div class="details-value"><code>${esc(site.proxy_upstream||'—')}</code></div></div>` : `
          <div class="details-field"><div class="details-label">Owner</div><div class="details-value">${esc(site.owner)}</div></div>
          <div class="details-field"><div class="details-label">Web Server</div><div class="details-value">${site.web_server==='nginx'?'Nginx':'Apache2'}</div></div>
          <div class="details-field"><div class="details-label">Node ID</div><div class="details-value">${site.node_id}</div></div>`}
        </div>
      </div>
      ${isSystem ? `
      <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px;">
        <div class="card"><h3>Web Service</h3><div style="margin-top:8px;font-size:13px;"><div>Status: <span class="badge ${({'Available':'badge-ok','Active':'badge-ok','Running':'badge-ok','Disabled':'badge-info'})[site.web_status]||'badge-info'}">${esc(site.web_status||'Not verified')}</span></div></div></div>
        <div class="card"><h3>PHP</h3><div style="margin-top:8px;font-size:13px;"><div>Status: <span class="badge badge-info">N/A</span></div></div></div>
        <div class="card"><h3>Databases</h3><div style="margin-top:8px;font-size:13px;"><div>Not applicable</div></div></div>
        <div class="card"><h3>Backups</h3><div style="margin-top:8px;font-size:13px;"><div>Not applicable</div></div></div>
      </div>
      <div style="margin-top:12px;">
        <a href="#" onclick="navigate('domain-detail',0);return false" class="btn btn-sm">View Domain Configuration (includes SSL &amp; Proxy management)</a>
      </div>` : `
      <div style="display:grid;grid-template-columns:2fr 1fr 1fr;gap:12px;margin-bottom:12px;">
        <div id="rt-card" class="card"></div>
        <div id="site-cols-left" style="display:grid;gap:12px;align-content:start;"></div>
        <div id="site-cols-right" style="display:grid;gap:12px;align-content:start;"></div>
      </div>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;" id="site-cols-bottom"></div>
      <div style="margin-top:12px;" id="site-wordpress-db"></div>
      <div style="margin-top:12px;" id="site-php-mail"></div>`}`;

    if (isSystem) return;

    const [domains, databases, ssl, proxy, backups] = await Promise.all([
      api('/api/domains'), api('/api/databases'), api('/api/ssl'), api('/api/proxy'), api('/api/backups')
    ]);
    if (lifecycle && !lifecycle.isActive()) return;
    const colLeft = $('site-cols-left');
    const colRight = $('site-cols-right');
    const colBottom = $('site-cols-bottom');
    const makeCard = (label, items, color, link) => {
      const clickable = link ? ` onclick="navigate('${link}')" style="cursor:pointer"` : '';
      return `<div class="card"${clickable}><h3>${label}</h3><div class="count${items.length===0?' zero':''}">${items.length || 0}</div><div style="font-size:11px;color:var(--text3);margin-top:4px;">${items.slice(0,2).join(', ')}${items.length>2?'...':''}</div></div>`;
    };
    colLeft.innerHTML =
      makeCard('Domains', (domains.data||[]).filter(d=>d.site_id==site.id).map(d=>d.domain), '#3b82f6') +
      makeCard('SSL', (ssl.data||[]).filter(c=>c.site_id==site.id).map(c=>c.status + (c.https_enabled?' (ON)':'')), '#ec4899', 'ssl');
    const siteDatabases = (databases.data||[]).filter(d=>d.site_id==site.id);
    colRight.innerHTML = makeCard('Databases', siteDatabases.map(d=>d.database_name || d.name || 'database'), '#8b5cf6');
    colBottom.innerHTML =
      makeCard('Proxy', (proxy.data||[]).filter(p=>p.site_id==site.id).map(p=>p.status), '#06b6d4') +
      makeCard('Backups', (backups.data||[]).filter(b=>b.site_id==site.id).map(b=>b.filename), '#f97316');
    loadWordPressCredentialCard(site.id, site.domain);
    loadPhpMailCard(site.id, site.domain);
    loadRuntimeCard(site.id, site.domain, site.web_server);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load site</div>'; }
}

/* ===== WORDPRESS DATABASE CREDENTIALS ===== */
async function loadWordPressCredentialCard(siteId, domain) {
  const el = $('site-wordpress-db');
  if (!el) return;
  el.innerHTML = '<div class="card"><h3>WordPress Database Credentials</h3><div style="font-size:12px;color:var(--text3)">Loading...</div></div>';
  try {
    const res = await api('/api/wordpress/database-credentials/status?site_id=' + siteId);
    renderWordPressCredentialCard(siteId, domain, res.data || {});
  } catch(e) {
    el.innerHTML = '<div class="card"><h3>WordPress Database Credentials</h3>'
      + '<div style="font-size:12px;color:#ef4444;margin-bottom:8px;">Unable to load WordPress credential status</div>'
      + '<button class="btn btn-sm btn-outline" onclick="loadWordPressCredentialCard(' + siteId + ',\'' + esc(domain) + '\')">Retry</button>'
      + '</div>';
  }
}

function renderWordPressCredentialCard(siteId, domain, status) {
  const el = $('site-wordpress-db');
  if (!el) return;
  const databaseId = status.database_target_available ? status.database_id : 0;
  const badgeClass = status.available ? 'badge-ok' : (status.status === 'config_missing' ? 'badge-info' : 'badge-warn');
  const targetBadgeClass = status.database_target_available ? 'badge-ok' : (status.database_target_status === 'database_target_missing' ? 'badge-info' : 'badge-warn');
  const rotateDisabled = !status.available || !status.database_target_available || !databaseId;
  const issues = (status.issues || []).map(i => '<div style="font-size:11px;color:' + (i.severity === 'error' ? '#ef4444' : '#f59e0b') + ';">' + esc(i.message || i.code) + '</div>').join('');
  const targetText = status.database_target_available ? ('database #' + databaseId) : (status.database_target_message || 'No matching database target');
  const disabledReason = !status.available
    ? 'WordPress credentials are not in a supported mutable direct-constant state.'
    : (!status.database_target_available
      ? (status.database_target_message || 'No exact backend database target was resolved.')
      : 'Rotation unavailable for this WordPress database target.');
  el.innerHTML = `
    <div class="card">
      <div style="display:flex;justify-content:space-between;align-items:center;gap:8px;margin-bottom:10px;">
        <h3 style="margin:0;">WordPress Database Credentials</h3>
        <div style="display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end;">
          <span class="badge ${badgeClass}">config: ${esc(status.status || 'unknown')}</span>
          <span class="badge ${targetBadgeClass}">target: ${esc(status.database_target_status || 'unknown')}</span>
        </div>
      </div>
      <div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:8px;font-size:12px;margin-bottom:10px;">
        <div><span style="color:var(--text3)">DB Name:</span> ${esc(status.db_name || 'Not available')}</div>
        <div><span style="color:var(--text3)">DB User:</span> ${esc(status.db_user || 'Not available')}</div>
        <div><span style="color:var(--text3)">DB Host:</span> ${esc(status.db_host || 'Not available')}</div>
        <div><span style="color:var(--text3)">Source:</span> ${esc(status.source || 'unknown')}</div>
        <div><span style="color:var(--text3)">Mutability:</span> ${esc(status.mutability || 'unknown')}</div>
        <div><span style="color:var(--text3)">Password:</span> ${status.db_password_present ? 'present' : 'not detected'}</div>
        <div><span style="color:var(--text3)">Target:</span> ${esc(targetText)}</div>
      </div>
      ${issues ? '<div style="display:grid;gap:4px;margin-bottom:10px;">' + issues + '</div>' : ''}
      <div style="border-top:1px solid var(--border);padding-top:10px;display:grid;gap:8px;">
        <div style="font-size:12px;color:var(--text2);">Rotate the database password only for supported direct WordPress configs. Type the domain to confirm.</div>
        <div style="display:flex;gap:8px;flex-wrap:wrap;align-items:center;">
          <input id="wp-rotate-confirm" placeholder="${esc(domain)}" autocomplete="off" style="min-width:220px;flex:1;padding:8px 10px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;" ${rotateDisabled ? 'disabled' : ''}>
          <button id="wp-rotate-btn" class="btn btn-sm btn-warning" onclick="rotateWordPressDatabasePassword(${siteId},${databaseId || 0},'${esc(domain)}')" ${rotateDisabled ? 'disabled' : ''}>Rotate Password</button>
        </div>
        <div id="wp-rotate-msg" style="font-size:12px;color:${rotateDisabled ? 'var(--text3)' : 'var(--text2)'};">${rotateDisabled ? esc(disabledReason) : 'No password will be shown or stored in the browser.'}</div>
      </div>
    </div>`;
}

async function rotateWordPressDatabasePassword(siteId, databaseId, domain) {
  const input = $('wp-rotate-confirm');
  const btn = $('wp-rotate-btn');
  const msg = $('wp-rotate-msg');
  const confirmation = input ? input.value.trim() : '';
  if (!databaseId) { if (msg) msg.textContent = 'No database is linked to this site.'; return; }
  if (confirmation !== domain) { if (msg) msg.textContent = 'Confirmation must match ' + domain; return; }
  if (btn) btn.disabled = true;
  if (msg) msg.textContent = 'Queueing credential rotation...';
  try {
    const res = await apiPost('/api/wordpress/database-credentials/rotate', {site_id: siteId, database_id: databaseId, confirmation});
    const jobId = res.data && res.data.job_id;
    toast('Credential rotation queued' + (jobId ? ' (job #' + jobId + ')' : ''), 'success');
    if (msg) msg.textContent = jobId ? 'Queued as job #' + jobId + '. Waiting for result...' : 'Rotation queued.';
    if (jobId) pollWordPressRotationJob(jobId, siteId, domain, 0);
  } catch(e) {
    if (btn) btn.disabled = false;
    const apiErr = e.body && e.body.error && e.body.error.message;
    if (msg) msg.textContent = apiErr || e.api_message || e.message || 'Failed to queue rotation';
  }
}

async function pollWordPressRotationJob(jobId, siteId, domain, attempts) {
  pollRotationJob(jobId, {
    lifecycle: activeSitesLifecycle,
    maxAttempts: 30,
    messageEl: 'wp-rotate-msg',
    renderFailed: renderWordPressRotationDiagnostics,
    onCompleted: () => loadWordPressCredentialCard(siteId, domain)
  }, attempts || 0);
}/* ===== PHP MAIL CARD ===== */
async function loadPhpMailCard(siteId, domain) {
  const el = $('site-php-mail');
  if (!el) return;
  el.innerHTML = '<div class="card"><h3>PHP Mail</h3><div style="font-size:12px;color:var(--text3)">Loading...</div></div>';
  try {
    const res = await api('/api/sites/' + siteId + '/mail-status');
    renderPhpMailCard(siteId, domain, res);
  } catch(e) {
    el.innerHTML = '<div class="card">'
      + '<h3>PHP Mail</h3>'
      + '<div style="font-size:12px;color:#ef4444;margin-bottom:8px;">Unable to load PHP Mail status</div>'
      + '<button class="btn btn-sm btn-outline" onclick="loadPhpMailCard(' + siteId + ',\'' + domain + '\')">Retry</button>'
      + '</div>';
  }
}

function renderPhpMailCard(siteId, domain, mailStatus) {
  const el = $('site-php-mail');
  if (!el) return;
  const s = (mailStatus && mailStatus.data) || {};
  const enabled = s.enabled;
  const credExists = s.credential_exists;
  const msmtprc = s.msmtprc;
  const network = s.network;
  const hasMailDomain = s.mail_domain;
  const allOk = enabled && credExists && msmtprc && network;

  let statusText, statusColor;
  if (!enabled) {
    statusText = 'Disabled';
    statusColor = 'var(--text3)';
  } else if (allOk) {
    statusText = 'Enabled';
    statusColor = 'var(--green,#22c55e)';
  } else {
    statusText = 'Degraded';
    statusColor = '#f59e0b';
  }

  let missing = '';
  if (enabled) {
    const parts = [];
    if (!credExists) parts.push('credentials');
    if (!msmtprc) parts.push('msmtprc');
    if (!network) parts.push('network');
    if (parts.length) missing = '<div style="font-size:11px;color:#ef4444;margin-top:4px;">Missing: ' + parts.join(', ') + '</div>';
  }

  el.innerHTML = `
    <div class="card">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
        <h3 style="margin:0;">PHP Mail</h3>
        <div style="display:flex;gap:6px;align-items:center;">
          <span style="font-size:12px;font-weight:600;color:${statusColor};">● ${statusText}</span>
        </div>
      </div>
      <div style="display:grid;grid-template-columns:1fr 1fr 1fr 1fr;gap:8px;font-size:12px;margin-bottom:8px;">
        <div><span style="color:var(--text3)">Mail Domain:</span> ${hasMailDomain ? '✅' : '❌'}</div>
        <div><span style="color:var(--text3)">Credentials:</span> ${credExists ? '✅' : '❌'}</div>
        <div><span style="color:var(--text3)">msmtprc:</span> ${msmtprc ? '✅' : '❌'}</div>
        <div><span style="color:var(--text3)">Network:</span> ${network ? '✅' : '❌'}</div>
      </div>
      ${missing}
      <div style="display:flex;gap:6px;margin-top:8px;">
        ${enabled ? `
          <button class="btn btn-sm btn-danger" onclick="disablePhpMail('${siteId}','${domain}')">Disable PHP Mail</button>
        ` : `
          <button class="btn btn-sm btn-primary" onclick="enablePhpMail('${siteId}','${domain}')">Enable PHP Mail</button>
        `}
      </div>
      <div id="php-mail-msg" style="font-size:12px;margin-top:8px;"></div>
    </div>`;
}

async function enablePhpMail(siteId, domain) {
  if (!confirm('Enable PHP mail for ' + domain + '?\n\nThis will create SMTP credentials and enable PHP mail()/wp_mail() support.')) return;
  const el = $('php-mail-msg');
  if (el) el.innerHTML = '<span style="color:var(--text3)">Enabling PHP mail...</span>';
  try {
    const r = await apiPost('/api/sites/' + siteId + '/enable-mail');
    const ms = await api('/api/sites/' + siteId + '/mail-status');
    renderPhpMailCard(siteId, domain, ms);
    if (el) el.innerHTML = '<span style="color:var(--green,#22c55e)">✅ PHP mail enabled</span>';
  } catch(e) {
    if (e.code === 'mail_domain_missing') {
      // MailDomain does not exist — offer to create it via backend endpoint
      if (!confirm('Mail Domain for ' + domain + ' does not exist.\n\nCreate it now with DKIM and enable PHP Mail?')) return;
      if (el) el.innerHTML = '<span style="color:var(--text3)">Creating Mail Domain...</span>';
      try {
        // Use backend endpoint that safely resolves domain_id/site_id
        const md = await apiPost('/api/sites/' + siteId + '/mail-domain');
        if (md.data && md.data.id) {
          await apiPost('/api/mail/domains/' + md.data.id + '/dkim/generate');
          if (el) el.innerHTML = '<span style="color:var(--green,#22c55e)">✅ Mail Domain created. Retrying enable...</span>';
          const retry = await apiPost('/api/sites/' + siteId + '/enable-mail');
          if (retry.success) {
            const ms = await api('/api/sites/' + siteId + '/mail-status');
            renderPhpMailCard(siteId, domain, ms);
            if (el) el.innerHTML = '<span style="color:var(--green,#22c55e)">✅ PHP mail enabled. Add DKIM DNS record in Mail → Domains.</span>';
          }
        }
      } catch(e2) {
        if (el) el.innerHTML = '<span style="color:#ef4444">❌ ' + (e2.api_message || e2.message || 'Create failed') + '</span>';
      }
    } else if (e.code === 'mail_domain_disabled') {
      if (confirm('Mail Domain for ' + domain + ' exists but is disabled.\n\nActivate it now and retry Enable PHP Mail?')) {
        if (el) el.innerHTML = '<span style="color:var(--text3)">Activating Mail Domain...</span>';
        try {
          // Fetch mail domains to find the disabled one
          const mailDomains = await api('/api/mail/domains');
          const disabledMd = (mailDomains.data||[]).find(md => md.domain === domain || md.domain_name === domain);
          if (disabledMd) {
            await api('/api/mail/domains/' + disabledMd.id, {method:'PATCH', headers:{'Content-Type':'application/json'}, body:JSON.stringify({enabled: true, mode: 'local-primary'})});
            if (el) el.innerHTML = '<span style="color:var(--green,#22c55e)">✅ Mail Domain activated. Retrying enable...</span>';
            // Retry enable-mail
            const retry = await apiPost('/api/sites/' + siteId + '/enable-mail');
            if (retry.success) {
              const ms = await api('/api/sites/' + siteId + '/mail-status');
              renderPhpMailCard(siteId, domain, ms);
              if (el) el.innerHTML = '<span style="color:var(--green,#22c55e)">✅ PHP mail enabled.</span>';
            }
          } else {
            if (el) el.innerHTML = '<span style="color:#ef4444">❌ Mail Domain not found via API</span>';
          }
        } catch(e2) {
          if (el) el.innerHTML = '<span style="color:#ef4444">❌ ' + (e2.api_message || e2.message) + '</span>';
        }
      }
    } else {
      if (el) el.innerHTML = '<span style="color:#ef4444">❌ ' + (e.api_message || e.message) + '</span>';
    }
  }
}

async function disablePhpMail(siteId, domain) {
  if (!confirm('Disable PHP mail for ' + domain + '?\n\nPHP mail() and wp_mail() will stop working.')) return;
  const el = $('php-mail-msg');
  if (el) el.innerHTML = '<span style="color:var(--text3)">Disabling PHP mail...</span>';
  try {
    const r = await apiPost('/api/sites/' + siteId + '/disable-mail');
    if (r.success) {
      const ms = await api('/api/sites/' + siteId + '/mail-status');
      renderPhpMailCard(siteId, domain, ms);
      if (el) el.innerHTML = '<span style="color:var(--green,#22c55e)">✅ PHP mail disabled</span>';
    } else {
      if (el) el.innerHTML = '<span style="color:#ef4444">❌ ' + (r.error||'Failed') + '</span>';
    }
  } catch(e) {
    if (el) el.innerHTML = '<span style="color:#ef4444">❌ ' + e.message + '</span>';
  }
}

/* ===== RUNTIME CARD ===== */
let _rtSiteId = null;
function loadRuntimeCard(siteId, domain, backend) {
  _rtSiteId = siteId;
  const card = $('rt-card');
  if (!card) return;
  card.innerHTML = `
    <h3>Runtime</h3>
    <div style="margin-top:8px;display:grid;gap:6px;" id="rt-status-list">
      <div style="font-size:12px;color:var(--text3);">Loading...</div>
    </div>
    <div style="margin-top:12px;display:grid;gap:6px;" id="rt-actions"></div>
    <div style="margin-top:8px;border-top:1px solid var(--border);padding-top:8px;" id="rt-actions-all"></div>
  `;
  refreshRuntimeCard(siteId, domain, backend);
}

function refreshRuntimeCard(siteId, domain, backend) {
  api('/api/runtime/' + siteId).then(rt => {
    if (activeSitesLifecycle && !activeSitesLifecycle.isActive()) return;
    if (!rt.success) return;
    const badge = (s) => {
      const m={'Running':'badge-ok','Healthy':'badge-ok','Active':'badge-ok','Stopped':'badge-err','Unhealthy':'badge-warn','Starting':'badge-warn','Expiring':'badge-warn','Error':'badge-err','Expired':'badge-err','Disabled':'badge-info','Issuing':'badge-warn','Unknown':'badge-info'};
      return `<span class="badge ${m[s]||'badge-info'}">${s}</span>`;
    };
    const list = $('rt-status-list');
    if (!list) return;
    // Show all four roles with their compose service hint
    const roles = [
      {role:'Frontend', svc:'web', status: rt.data.web, backend: backend},
      {role:'PHP', svc:'php', status: rt.data.php, backend: ''},
      {role:'Database', svc:'mariadb', status: rt.data.db || 'Unknown', backend: ''},
      {role:'Redis', svc:'redis', status: rt.data.cache || 'Unknown', backend: ''}
    ];
    list.innerHTML = roles.map(r => {
      const impl = r.backend ? `<span style="font-size:11px;color:var(--text3);margin-left:4px;">(${r.backend === 'nginx' ? 'Nginx' : 'Apache'})</span>` : '';
      return `<div style="display:flex;justify-content:space-between;align-items:center;">
        <div><span style="font-weight:500;">${r.role}</span>${impl}</div>
        <div>${badge(r.status)}</div>
      </div>`;
    }).join('');
  }).catch(() => {});
  // Rebuild action buttons (domain captured via closure)
  buildRuntimeActions(siteId, domain);
}

function buildRuntimeActions(siteId, domain) {
  const actionsDiv = $('rt-actions');
  const allDiv = $('rt-actions-all');
  if (!actionsDiv || !allDiv) return;
  const btn = (action, label, style) =>
    `<button class="btn btn-sm ${style||''}" onclick="runRuntimeAction(${siteId},'${esc(domain)}','${action}')" style="width:100%;">${label}</button>`;

  actionsDiv.innerHTML =
    btn('restart-web', 'Restart Web') +
    btn('restart-php', 'Restart PHP') +
    btn('restart-db', 'Restart DB') +
    btn('restart-redis', 'Restart Redis');
  allDiv.innerHTML = btn('restart-all', 'Restart Entire Site', 'btn-warning');
}

function runRuntimeAction(siteId, domain, action) {
  if (action === 'restart-db') {
    if (!confirm('Restart database container for ' + domain + '?\nThis may cause brief downtime.')) return;
  } else if (action === 'restart-redis') {
    if (!confirm('Restart Redis container for ' + domain + '?')) return;
  } else if (action === 'restart-all') {
    if (!confirm('Restart ALL containers for ' + domain + '?\nThis will restart web, PHP, database, and Redis.')) return;
  } else {
    if (!confirm('Restart ' + (action === 'restart-web' ? 'web' : 'PHP') + ' for ' + domain + '?')) return;
  }

  toast('Restarting...', 'info');
  apiPost('/api/runtime/' + siteId + '/' + action, {}).then(res => {
    if (res.success && res.data) {
      toast('Restart queued (job #' + res.data.job_id + ')', 'success');
    } else if (res.success) {
      toast('Restart queued', 'success');
    } else {
      toast('Error: ' + (res.error || 'Unknown error'), 'error');
      return;
    }
    // Refresh runtime status after delay
    const ctx = activeSitesLifecycle;
    const later = ctx && ctx.setTimeout ? ctx.setTimeout.bind(ctx) : setTimeout;
    later(() => {
      const domainEl = document.querySelector('#rt-card');
      if (domainEl) refreshRuntimeCard(siteId, domain, '');
    }, 1500);
  }).catch(e => {
    toast('Error: ' + e.message, 'error');
  });
}

const sitesPage = { mount: loadSites, unmount() { activeSitesLifecycle = null; } };
const siteDetailPage = { mount: loadSiteDetail, unmount() { activeSitesLifecycle = null; } };
export { loadSites, loadSiteDetail, sitesPage, siteDetailPage };
Object.assign(window, { loadSites, removeSite, showCreateSiteWizard, renderSiteWizardTemplateOptions, startSiteWizard, loadSiteDetail, loadWordPressCredentialCard, renderWordPressCredentialCard, rotateWordPressDatabasePassword, pollWordPressRotationJob, loadPhpMailCard, renderPhpMailCard, enablePhpMail, disablePhpMail, loadRuntimeCard, refreshRuntimeCard, buildRuntimeActions, runRuntimeAction });
