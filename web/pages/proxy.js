import {
  api, apiPost, buildTable, card, esc, navigate, tb, toast
} from '../core/context.js';


/* ===== PROXY ===== */
let activeProxyLifecycle = null;

async function loadProxy(p, params, lifecycle) {
  activeProxyLifecycle = lifecycle || activeProxyLifecycle;
  if (p._loading) return;
  p._loading = true;

  try {
    const [proxyData, healthData] = await Promise.all([
      api('/api/proxy'),
      api('/api/proxy/health')
    ]);
    if (lifecycle && !lifecycle.isActive()) return;

    const health = healthData.data || {};
    const container = health.container || {};
    const proxyInfo = health.proxy || {};
    const configTest = proxyInfo.config_test || {};
    const entries = health.entries || {};
    const recoveryInfo = health.recovery || {};

    const stateBadge = (state) => state === 'running'
      ? '<span class="badge badge-ok">Running</span>'
      : '<span class="badge badge-err">Stopped</span>';

    const testBadge = (conf) => {
      if (!conf.success && (!conf.message || conf.message.indexOf('Not tested') >= 0)) return '<span class="badge badge-info">Not tested</span>';
      if (conf.success === null || conf.success === undefined) return '<span class="badge badge-info">Not tested</span>';
      return conf.success ? '<span class="badge badge-ok">Valid</span>' : '<span class="badge badge-err">Failed</span>';
    };

    const fmtTime = (ts) => {
      if (!ts) return 'Never since daemon start';
      try { return new Date(ts).toLocaleString(); } catch(e) { return ts; }
    };

    const recoveryRunningBadge = (running) => running
      ? '<span class="badge badge-ok">Running</span>'
      : '<span class="badge badge-err">Stopped</span>';

    const recoveryProgressBadge = (prog) => prog
      ? '<span class="badge badge-warn">In progress</span>'
      : '<span class="badge badge-info">Idle</span>';

    const recoveryResultBadge = (res) => {
      if (!res) return '<span class="badge badge-info">Never</span>';
      return res === 'success' ? '<span class="badge badge-ok">Success</span>' : '<span class="badge badge-err">Failed</span>';
    };

    const actionBtns = `
      <div style="display:flex;gap:6px;flex-wrap:wrap;" id="proxy-actions">
        <button class="btn btn-sm" onclick="proxyAction('test')" id="proxy-btn-test">Test</button>
        <button class="btn btn-sm" onclick="proxyAction('reload')" id="proxy-btn-reload">Reload</button>
        <button class="btn btn-sm" onclick="proxyAction('sync')" id="proxy-btn-sync">Sync</button>
        <button class="btn btn-sm btn-primary" onclick="proxyAction('recover')" id="proxy-btn-recover">Recover</button>
      </div>`;

    const entryBadges = () => {
      let html = '';
      if (entries.total) html += `<span class="badge badge-info" style="margin-right:4px;">${entries.total} Total</span>`;
      if (entries.system) html += `<span class="badge badge-info" style="margin-right:4px;">${entries.system} System</span>`;
      if (entries.site) html += `<span class="badge badge-info">${entries.site} Sites</span>`;
      return html || '0';
    };

    p.innerHTML = `
      <div class="page-header"><h1>Reverse Proxy</h1></div>
      <div class="card" style="margin-bottom:12px;" id="proxy-health-card">
        <h3 style="margin-bottom:8px;">Global Health</h3>
        <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(180px,1fr));gap:16px;">
          <div><div style="font-size:11px;color:var(--text3);">Container</div><div style="margin-top:2px;">${stateBadge(container.state)}</div></div>
          <div><div style="font-size:11px;color:var(--text3);">Version</div><div style="margin-top:2px;font-size:12px;color:var(--text2);">nginx ${esc(proxyInfo.version||'?')}</div></div>
          <div><div style="font-size:11px;color:var(--text3);">Configuration</div><div style="margin-top:2px;">${testBadge(configTest)}</div></div>
          <div><div style="font-size:11px;color:var(--text3);">Config Detail</div><div style="margin-top:2px;font-size:12px;color:var(--text2);word-break:break-word;">${esc(configTest.message||'Never tested')}</div></div>
          <div><div style="font-size:11px;color:var(--text3);">Recovery Manager</div><div style="margin-top:2px;">${recoveryRunningBadge(recoveryInfo.manager_running)}</div></div>
          <div><div style="font-size:11px;color:var(--text3);">Recovery In Progress</div><div style="margin-top:2px;">${recoveryProgressBadge(recoveryInfo.recovery_in_progress)}</div></div>
          <div><div style="font-size:11px;color:var(--text3);">Last Recovery</div><div style="margin-top:2px;font-size:12px;color:var(--text2);">${fmtTime(recoveryInfo.last_recovery_at)}</div></div>
          <div><div style="font-size:11px;color:var(--text3);">Last Result</div><div style="margin-top:2px;">${recoveryResultBadge(recoveryInfo.last_recovery_result)}</div></div>
          <div><div style="font-size:11px;color:var(--text3);">Proxy Entries</div><div style="margin-top:4px;">${entryBadges()}</div></div>
        </div>
        <div style="margin-top:12px;padding-top:12px;border-top:1px solid var(--border);">${actionBtns}</div>
      </div>`;
    p.innerHTML += tb('Proxy Entries');
    const render = () => {
      const tbl = $('proxy-table');
      if (!tbl) return;
      const rows = (proxyData.data||[]).filter(r=>!window.searchTerm||r.domain.includes(window.searchTerm));
      tbl.innerHTML = buildTable([
        {label:'Domain',html:r=>`<span style="font-weight:500;">${esc(r.domain)}</span>${r.protected?` <span class="badge badge-info">system</span>`:''}`},
        {label:'Type / Site',html:r=>r.entry_type==='system'?'<span class="badge badge-info">Admin Panel</span>':`<span style="font-size:12px;">${esc(r.site_name||'Unlinked')}</span>`},
        {label:'Upstream',html:r=>`<code style="font-size:12px;">${esc(r.upstream)}</code>`},
        {label:'HTTP',html:()=>'<span class="badge badge-ok">ON</span>'},
        {label:'HTTPS',html:r=>r.https_enabled?'<span class="badge badge-ok">ON</span>':'<span class="badge badge-info">OFF</span>'},
        {label:'State',html:r=>{
          const m={'active':'badge-ok','disabled':'badge-err','error':'badge-warn'};
          return `<span class="badge ${m[r.configured_state]||'badge-info'}">${esc(r.configured_state)}</span>`;
        }},
        {label:'Backend',html:r=>{
          const m={'Running':'badge-ok','Active':'badge-ok','Healthy':'badge-ok','Stopped':'badge-err','Unhealthy':'badge-warn','Starting':'badge-warn','Error':'badge-err','Unknown':'badge-info','Missing':'badge-err'};
          if (r.backend_health === 'Admin UI') return '<span class="badge badge-info">Admin UI</span>';
          return `<span class="badge ${m[r.backend_health]||'badge-info'}">${esc(r.backend_health)}</span>`;
        }},
        {label:'Actions',html:r=>{
          let acts = `<button class="btn-icon" onclick="window.open('${r.ssl_status === 'Active' || r.ssl_status === 'Expiring' ? 'https' : 'http'}://${esc(r.domain)}','_blank')" title="Open">&#8599;</button>`;
          if (r.site_id > 0) acts += `<button class="btn-icon" onclick="navigate('site-detail',${r.site_id})" title="View site">&#128065;</button>`;
          if (!r.protected) {
            acts += `<button class="btn-icon" style="color:var(--red)" onclick="removeProxy('${esc(r.domain)}')" title="Remove">&#10005;</button>`;
          }
          return acts;
        }}
      ], rows, 'No proxy entries');
    };
    if (lifecycle && lifecycle.setRenderTable) lifecycle.setRenderTable(render);
    else window.renderTable = render;
    p.innerHTML += `<div id="proxy-table"></div>`;
    window.renderTable();
  } catch (e) {
    p.innerHTML = '<div class="empty-state">Failed to load proxy</div>';
  } finally {
    p._loading = false;
  }
}

let _proxyActionPending = false;

function setProxyButtonsEnabled(enabled) {
  for (const id of ['proxy-btn-test', 'proxy-btn-reload', 'proxy-btn-sync', 'proxy-btn-recover']) {
    const btn = document.getElementById(id);
    if (btn) btn.disabled = !enabled;
  }
}

function setProxyButtonText(action) {
  const labels = {'test':'Testing...','reload':'Reloading...','sync':'Syncing...','recover':'Recovering...'};
  for (const [a, label] of Object.entries(labels)) {
    const btn = document.getElementById('proxy-btn-' + a);
    if (btn) btn.textContent = (a === action) ? label : {test:'Test',reload:'Reload',sync:'Sync',recover:'Recover'}[a];
  }
}

async function proxyAction(action) {
  if (_proxyActionPending) return;
  _proxyActionPending = true;

  setProxyButtonsEnabled(false);
  setProxyButtonText(action);

  try {
    const res = await apiPost('/api/proxy/' + action, {});
    if (res.success) {
      toast(action + ' completed', 'success');
    } else {
      toast('Error: ' + (res.error || 'Unknown'), 'error');
    }
  } catch(e) {
    toast('Error: ' + e.message, 'error');
  }

  _proxyActionPending = false;
  // Refresh health card and proxy entries without full page reload
  const p = document.getElementById('page');
  if (p && !p._loading) loadProxy(p, null, activeProxyLifecycle);
}

async function removeProxy(domain) {
  if (!confirm('Remove proxy entry for '+domain+'?')) return;
  try { const res = await apiPost('/api/proxy/remove',{domain}); if(res.success){toast('Proxy removed','success');window.renderTable&&renderTable();}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

const proxyPage = { mount: loadProxy, unmount() { activeProxyLifecycle = null; _proxyActionPending = false; } };
export { loadProxy, proxyPage };
Object.assign(window, { loadProxy, setProxyButtonsEnabled, setProxyButtonText, proxyAction, removeProxy });
