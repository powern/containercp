import {
  api, apiPost, buildTable, esc, escAttr, navigate, tb, toast
} from '../core/context.js';


/* ===== SSL ===== */

// Helper: perform SSL action on a domain using path-based REST API
async function sslAction(domain, action, body) {
  try {
    const res = await apiPost('/api/ssl/' + encodeURIComponent(domain) + '/' + action, body || {});
    if (res.success) {
      toast('SSL ' + action + ' completed', 'success');
    } else {
      const msg = (res.error && res.error.message) || res.error || 'Unknown error';
      toast('Error: ' + msg, 'error');
    }
  } catch(e) {
    toast('Network error: ' + e.message, 'error');
  }
  loadSsl($('page'));
}

async function issueSsl(domain) {
  toast('Issuing certificate for ' + domain + '...', 'info');
  await sslAction(domain, 'issue');
}
async function renewSsl(domain) {
  await sslAction(domain, 'renew');
}
async function toggleSsl(domain, enable) {
  await sslAction(domain, enable ? 'enable' : 'disable');
}
async function toggleRedirect(domain, enable) {
  await sslAction(domain, enable ? 'redirect/enable' : 'redirect/disable');
}

// Status badge helper
function sslStatusBadge(status) {
  const map = {
    'HTTP_ONLY': 'badge-info',
    'issuing': 'badge-warn',
    'active': 'badge-ok',
    'error': 'badge-err',
    'disabled': 'badge-err'
  };
  return `<span class="badge ${map[status] || 'badge-err'}">${esc(status)}</span>`;
}

// Action buttons based on SSL state
function sslActions(r) {
  const d = esc(r.domain);
  const arg = escAttr(JSON.stringify(String(r.domain || '')));
  let html = '';

  if (r.status === 'HTTP_ONLY' || r.status === 'error') {
    html += `<button class="btn-icon" onclick="issueSsl(${arg})" title="Issue Certificate">&#9679; Issue</button>`;
  }
  if (r.status === 'active') {
    if (r.https_enabled) {
      html += `<button class="btn-icon" onclick="toggleSsl(${arg},false)" title="Disable HTTPS">&#9646;&#9646; HTTPS</button>`;
    } else {
      html += `<button class="btn-icon" onclick="toggleSsl(${arg},true)" title="Enable HTTPS">&#9654; HTTPS</button>`;
    }
    html += `<button class="btn-icon" onclick="renewSsl(${arg})" title="Renew Certificate">&#8635; Renew</button>`;
    if (r.https_enabled) {
      if (r.redirect_enabled) {
        html += `<button class="btn-icon" onclick="toggleRedirect(${arg},false)" title="Disable Redirect">&#8592; No Redirect</button>`;
      } else {
        html += `<button class="btn-icon" onclick="toggleRedirect(${arg},true)" title="Enable Redirect">&#8594; Redirect</button>`;
      }
    }
  }
  if (r.status === 'disabled') {
    html += `<button class="btn-icon" onclick="toggleSsl(${arg},true)" title="Enable HTTPS">&#9654; HTTPS</button>`;
  }

  return html || '<span class="badge badge-info">No actions</span>';
}

async function loadSite(domain) {
  try {
    const res = await api('/api/sites');
    const site = (res.data || []).find(s => s.domain === domain);
    if (site) navigate('site-detail', site.id);
    else toast('Site not found for ' + domain, 'error');
  } catch(e) {
    toast('Failed to load site for ' + domain, 'error');
  }
}

// Format ISO date to short display
function fmtDate(iso) {
  if (!iso) return '—';
  try {
    const d = new Date(iso);
    return d.toLocaleDateString('en-US', {year:'numeric', month:'short', day:'numeric'});
  } catch(e) { return iso; }
}

async function loadSsl(p) {
  try {
    const data = await api('/api/ssl');
    p.innerHTML = `<div class="page-header"><h1>SSL Certificates</h1></div>`;
    p.innerHTML += tb('All Sites');
    window.renderTable = () => {
      const tbl = $('ssl-table');
      if (!tbl) return;
      tbl.innerHTML = buildTable([
        {label:'Domain', html:r=>`<a href="#" onclick="loadSite(${escAttr(JSON.stringify(String(r.domain || '')))});return false">${esc(r.domain)}</a>`},
        {label:'Status', html:r=>sslStatusBadge(r.status)},
        {label:'HTTPS', html:r=>r.https_enabled?'<span class="badge badge-ok">ON</span>':'<span class="badge badge-err">OFF</span>'},
        {label:'Provider', html:r=> r.status === 'HTTP_ONLY' ? '—' : esc(r.provider_id)+(r.environment?'<br><span class="badge badge-info">'+esc(r.environment)+'</span>':'')},
        {label:'Expires', html:r=>fmtDate(r.expires_at)},
        {label:'Auto Renew', html:r=> r.status === 'HTTP_ONLY' ? '<span class="badge badge-info">N/A</span>' : (r.auto_renew?'<span class="badge badge-ok">Yes</span>':'<span class="badge badge-info">No</span>')},
        {label:'Actions', html:r=>sslActions(r)}
      ], (data.data||[]));
    };
    p.innerHTML += `<div id="ssl-table"></div>`;
    window.renderTable();
  } catch(e) {
    p.innerHTML = '<div class="empty-state">Failed to load SSL</div>';
  }
}

export { loadSsl };
Object.assign(window, { sslAction, issueSsl, renewSsl, toggleSsl, toggleRedirect, sslStatusBadge, sslActions, loadSite, fmtDate, loadSsl });
