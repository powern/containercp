// Always use /ui-api/ prefix (works through Reverse Proxy and directly)
const API_BASE = '/ui-api';

let sessionToken = localStorage.getItem('session_token');
let currentUser = null;
let currentPage = 'dashboard';
let searchTerm = '';

function navigateTo(page) { navigate(page); }

async function api(path, opts) {
  opts = opts || {};
  opts.headers = opts.headers || {};
  if (sessionToken) {
    opts.headers['X-Session-Token'] = sessionToken;
  }
  const res = await fetch(API_BASE + path, opts);
  if (res.status === 401) {
    const body = await res.json().catch(() => ({}));
    const err = new Error(body.error || 'Unauthorized');
    err.status = 401;
    err.login_required = body.login_required;
    throw err;
  }
  if (!res.ok) {
    const body = await res.json().catch(() => ({}));
    const err = new Error(body.error || 'HTTP ' + res.status);
    err.status = res.status;
    throw err;
  }
  return await res.json();
}

async function apiPost(path, body) {
  return api(path, {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify(body)});
}

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

function esc(s) { return String(s==null?'':s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;'); }
function $(id) { return document.getElementById(id); }
function qs(s) { return document.querySelector(s); }
function qsa(s) { return document.querySelectorAll(s); }

/* ===== LOGIN ===== */
function renderLogin(error) {
  document.getElementById('app').innerHTML = `
    <div style="min-height:100vh;display:flex;align-items:center;justify-content:center;background:var(--bg2);">
      <div style="background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:32px;width:380px;max-width:90vw;">
        <div style="text-align:center;margin-bottom:24px;">
          <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="var(--primary)" stroke-width="2" style="margin-bottom:8px;"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><polyline points="3.27 6.96 12 12.01 20.73 6.96"/><line x1="12" y1="22.08" x2="12" y2="12"/></svg>
          <h1 style="font-size:20px;font-weight:600;">ContainerCP</h1>
          <p style="font-size:13px;color:var(--text2);margin-top:4px;">Sign in to your control panel</p>
        </div>
        ${error ? `<div style="background:var(--red);color:#fff;padding:8px 12px;border-radius:6px;font-size:13px;margin-bottom:16px;">${esc(error)}</div>` : ''}
        <div style="display:grid;gap:14px;">
          <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Username</label><input id="login-username" value="admin" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
          <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Password</label><input id="login-password" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
          <button class="btn btn-primary" onclick="doLogin()" style="width:100%;padding:10px;margin-top:4px;">Sign In</button>
        </div>
      </div>
    </div>`;
  $('login-password').addEventListener('keydown', e => { if (e.key === 'Enter') doLogin(); });
}

async function doLogin() {
  const username = $('login-username').value.trim();
  const password = $('login-password').value;
  if (!username || !password) { renderLogin('Please enter username and password'); return; }
  try {
    const res = await apiPost('/auth/login', {username, password});
    if (res.success) {
      sessionToken = res.data.token;
      localStorage.setItem('session_token', sessionToken);
      currentUser = {username: res.data.username, must_change_password: res.data.must_change_password};
      if (res.data.must_change_password) {
        renderChangePassword();
      } else {
        initApp();
      }
    } else {
      renderLogin(res.error || 'Login failed');
    }
  } catch(e) {
    if (e.status === 401) {
      renderLogin('Invalid username or password');
    } else {
      renderLogin('Authentication service unavailable. Is the daemon running?');
    }
  }
}

/* ===== CHANGE PASSWORD ===== */
function renderChangePassword(error, success) {
  const app = document.getElementById('app');
  if (!app) return;
  app.innerHTML = `
    <div style="min-height:100vh;display:flex;align-items:center;justify-content:center;background:var(--bg2);">
      <div style="background:var(--surface);border:1px solid var(--border);border-radius:12px;padding:32px;width:380px;max-width:90vw;">
        <div style="text-align:center;margin-bottom:24px;">
          <svg width="40" height="40" viewBox="0 0 24 24" fill="none" stroke="var(--yellow)" stroke-width="2" style="margin-bottom:8px;"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg>
          <h1 style="font-size:20px;font-weight:600;">Change Password</h1>
          <p style="font-size:13px;color:var(--text2);margin-top:4px;">You must change your temporary password before continuing</p>
        </div>
        ${error ? `<div style="background:var(--red);color:#fff;padding:8px 12px;border-radius:6px;font-size:13px;margin-bottom:16px;">${esc(error)}</div>` : ''}
        ${success ? `<div style="background:var(--green);color:#fff;padding:8px 12px;border-radius:6px;font-size:13px;margin-bottom:16px;">${esc(success)}</div>` : ''}
        <div style="display:grid;gap:14px;">
          <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Current Password</label><input id="cp-old" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
          <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">New Password</label><input id="cp-new" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
          <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Confirm New Password</label><input id="cp-confirm" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;"></div>
          <button class="btn btn-primary" onclick="doChangePassword()" style="width:100%;padding:10px;margin-top:4px;">Change Password</button>
        </div>
      </div>
    </div>`;
}

async function doChangePassword() {
  const old = $('cp-old').value;
  const pwd = $('cp-new').value;
  const confirm = $('cp-confirm').value;
  if (!old || !pwd) { renderChangePassword('All fields are required'); return; }
  if (pwd.length < 6) { renderChangePassword('Password must be at least 6 characters'); return; }
  if (pwd !== confirm) { renderChangePassword('Passwords do not match'); return; }
  try {
    const res = await apiPost('/auth/change-password', {old_password: old, new_password: pwd});
    if (res.success) {
      toast('Password changed successfully', 'success');
      initApp();
    } else {
      renderChangePassword(res.error || 'Failed to change password');
    }
  } catch(e) {
    renderChangePassword('Connection error');
  }
}

/* ===== LOGOUT ===== */
async function doLogout() {
  try {
    await apiPost('/auth/logout');
  } catch(e) {}
  localStorage.removeItem('session_token');
  sessionToken = null;
  currentUser = null;
  renderLogin();
}

/* ===== INIT APP ===== */
function initApp() {
  document.getElementById('app').innerHTML = `
    <aside class="sidebar" id="sidebar">
      <div class="sidebar-header">
        <div class="logo">
          <svg width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><polyline points="3.27 6.96 12 12.01 20.73 6.96"/><line x1="12" y1="22.08" x2="12" y2="12"/></svg>
          <span>ContainerCP</span>
        </div>
      </div>
      <nav class="nav" id="nav">
        <a class="nav-link active" data-page="dashboard" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="3" width="7" height="7"/><rect x="14" y="3" width="7" height="7"/><rect x="14" y="14" width="7" height="7"/><rect x="3" y="14" width="7" height="7"/></svg> Dashboard</a>
        <a class="nav-link" data-page="sites" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/></svg> Sites</a>
        <a class="nav-link" data-page="domains" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="10"/><line x1="2" y1="12" x2="22" y2="12"/><path d="M12 2a15.3 15.3 0 0 1 4 10 15.3 15.3 0 0 1-4 10 15.3 15.3 0 0 1-4-10 15.3 15.3 0 0 1 4-10z"/></svg> Domains</a>
        <a class="nav-link" data-page="databases" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><ellipse cx="12" cy="5" rx="9" ry="3"/><path d="M21 12c0 1.66-4 3-9 3s-9-1.34-9-3"/><path d="M3 5v14c0 1.66 4 3 9 3s9-1.34 9-3V5"/></svg> Databases</a>
        <a class="nav-link" data-page="ssl" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="3" y="11" width="18" height="11" rx="2" ry="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></svg> SSL</a>
        <a class="nav-link" data-page="proxy" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="5" y1="12" x2="19" y2="12"/><polyline points="12 5 19 12 12 19"/></svg> Proxy</a>
        <a class="nav-link" data-page="access" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg> Access</a>
        <a class="nav-link" data-page="backups" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><polyline points="3.27 6.96 12 12.01 20.73 6.96"/></svg> Backups</a>
        <a class="nav-link" data-page="profiles" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/></svg> Profiles</a>
        <a class="nav-link" data-page="templates" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><polyline points="16 18 22 12 16 6"/><polyline points="8 6 2 12 8 18"/></svg> Templates</a>
        <a class="nav-link" data-page="nodes" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><rect x="2" y="2" width="8" height="8" rx="2"/><rect x="14" y="2" width="8" height="8" rx="2"/><rect x="2" y="14" width="8" height="8" rx="2"/><rect x="14" y="14" width="8" height="8" rx="2"/></svg> Nodes</a>
        <a class="nav-link" data-page="logs" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M14 2H6a2 2 0 0 0-2 2v16a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V8z"/><polyline points="14 2 14 8 20 8"/><line x1="16" y1="13" x2="8" y2="13"/><line x1="16" y1="17" x2="8" y2="17"/></svg> Logs</a>
        <a class="nav-link" data-page="settings" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1 0 2.83 2 2 0 0 1-2.83 0l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-2 2 2 2 0 0 1-2-2v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83 0 2 2 0 0 1 0-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1-2-2 2 2 0 0 1 2-2h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 0-2.83 2 2 0 0 1 2.83 0l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 2-2 2 2 0 0 1 2 2v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 0 2 2 0 0 1 0 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 2 2 2 2 0 0 1-2 2h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg> Settings</a>
      </nav>
    </aside>
    <div class="main">
      <header class="topbar">
        <button class="sidebar-toggle" id="sidebarToggle">&#9776;</button>
        <div class="topbar-search">
          <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="11" cy="11" r="8"/><line x1="21" y1="21" x2="16.65" y2="16.65"/></svg>
          <input type="text" id="globalSearch" placeholder="Search...">
        </div>
        <div class="topbar-right">
          <span style="font-size:12px;color:var(--text2);margin-right:8px;" id="userDisplay"></span>
          <button class="btn btn-sm" onclick="doLogout()" style="margin-right:8px;font-size:11px;">Logout</button>
          <span class="version-badge" id="versionBadge">...</span>
          <span class="status-dot" id="statusDot"></span>
          <span class="status-label" id="statusLabel">Connected</span>
          <button class="theme-btn" id="themeToggle" title="Toggle theme">
            <svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="5"/><line x1="12" y1="1" x2="12" y2="3"/><line x1="12" y1="21" x2="12" y2="23"/><line x1="4.22" y1="4.22" x2="5.64" y2="5.64"/><line x1="18.36" y1="18.36" x2="19.78" y2="19.78"/><line x1="1" y1="12" x2="3" y2="12"/><line x1="21" y1="12" x2="23" y2="12"/><line x1="4.22" y1="19.78" x2="5.64" y2="18.36"/><line x1="18.36" y1="5.64" x2="19.78" y2="4.22"/></svg>
          </button>
        </div>
      </header>
      <div class="page" id="page"></div>
    </div>`;

  if (currentUser) {
    $('userDisplay').textContent = currentUser.username;
  }

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

  updateStatus();
  loadVersion();
  setInterval(updateStatus, 30000);
  navigate('dashboard');
}

/* ===== AUTH CHECK ===== */
async function checkAuth() {
  if (!sessionToken) {
    renderLogin();
    return;
  }
  try {
    const res = await api('/auth/me');
    currentUser = {username: res.data.username, must_change_password: res.data.must_change_password};
    if (res.data.must_change_password) {
      renderChangePassword();
    } else {
      initApp();
    }
  } catch(e) {
    localStorage.removeItem('session_token');
    sessionToken = null;
    renderLogin(e.status === 401 ? null : 'Session expired. Please log in again.');
  }
}

document.addEventListener('DOMContentLoaded', checkAuth);

/* ===== NAVIGATION ===== */
function navigate(page, params) {
  currentPage = page;
  qsa('.nav-link').forEach(l => l.classList.toggle('active', l.dataset.page === (page === 'site-detail' ? 'sites' : page)));
  const p = $('page');
  if (!p) return;
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
        {label:'Web',html:r=>`<span data-rt-id="${r.id}" data-rt-service="web" class="badge badge-info">...</span>`},
        {label:'PHP',html:r=>`<span data-rt-id="${r.id}" data-rt-service="php" class="badge badge-info">...</span>`},
        {label:'HTTPS',html:r=>`<span data-rt-id="${r.id}" data-rt-service="https" class="badge badge-info">...</span>`},
        {label:'Owner',html:r=>esc(r.owner)},
        {label:'Backend',html:r=>r.web_server==='nginx'?'<span class="badge badge-info">Nginx</span>':'<span class="badge badge-ok">Apache2</span>'},
        {label:'Actions',html:r=>`<button class="btn-icon" onclick="navigate('site-detail',${r.id})" title="View">&#128065;</button><button class="btn-icon" style="color:var(--red)" title="Remove" onclick="removeSite('${esc(r.domain)}')">&#10005;</button>`}
      ], filtered, 'No sites');
      // Fetch runtime + HTTPS status for each site
      filtered.forEach(site => {
        api('/api/runtime/' + site.id).then(rt => {
          if (!rt.success) return;
          const m={'Running':'badge-ok','Active':'badge-ok','Stopped':'badge-err','Unhealthy':'badge-warn','Starting':'badge-warn','Expiring':'badge-warn','Error':'badge-err','Expired':'badge-err','Disabled':'badge-info','Issuing':'badge-warn','Unknown':'badge-info'};
          const update = (srv, val) => {
            const el = tbl.querySelector(`span[data-rt-id="${site.id}"][data-rt-service="${srv}"]`);
            if (el) { el.className = 'badge ' + (m[val]||'badge-info'); el.textContent = val; }
          };
          update('web', rt.data.web);
          update('php', rt.data.php);
          update('https', rt.data.https);
        }).catch(()=>{});
      });
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
      <div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Backend Web Server</label>
        <select id="wiz-backend" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
          <option value="apache-php-default">Apache2 (default)</option>
          <option value="nginx-php-default">Nginx</option>
        </select>
      </div>
      <div id="wiz-summary" style="font-size:12px;color:var(--text3);background:var(--bg2);padding:8px 12px;border-radius:6px;margin-top:4px;">Backend: Apache2 with PHP-FPM</div>
      <button class="btn btn-primary" onclick="startSiteWizard()">Create Site</button>
    </div>`, 420);
  document.getElementById('wiz-backend').addEventListener('change', function() {
    var summary = document.getElementById('wiz-summary');
    if (this.value === 'nginx-php-default') {
      summary.textContent = 'Backend: Nginx with PHP-FPM';
    } else {
      summary.textContent = 'Backend: Apache2 with PHP-FPM';
    }
  });
}

async function startSiteWizard() {
  const owner = $('wiz-owner').value.trim();
  const domain = $('wiz-domain').value.trim();
  const profile = $('wiz-backend') ? $('wiz-backend').value : '';
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
          setTimeout(() => { document.getElementById('progress-overlay')?.remove(); navigate('sites'); }, 1500);
        });
      } else {
        $('progress-bar').style.width = '100%';
        $('progress-step').textContent = 'Site created successfully';
        $('progress-status').textContent = 'Completed';
        setTimeout(() => { document.getElementById('progress-overlay')?.remove(); navigate('sites'); }, 1500);
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

/* ===== JOB PROGRESS POLLING ===== */
function pollJobProgress(jobId, onComplete) {
  const interval = setInterval(async () => {
    try {
      const res = await api('/api/jobs?id=' + jobId);
      if (res.success && res.data) {
        const job = res.data;
        const pbar = $('progress-bar');
        const pstep = $('progress-step');
        const pstatus = $('progress-status');
        if (pbar) pbar.style.width = job.progress + '%';
        if (pstep) pstep.textContent = job.message || (job.steps && job.steps[job.current_step]) || 'Running...';
        if (pstatus) pstatus.textContent = job.status;

        if (job.status === 'completed' || job.status === 'failed') {
          clearInterval(interval);
          if (job.status === 'failed') {
            if (pbar) pbar.style.background = 'var(--red)';
            if (pstep) pstep.textContent = 'Error: ' + (job.message || 'Deployment failed');
          } else {
            if (onComplete) onComplete();
          }
        }
      }
    } catch(e) {
      clearInterval(interval);
    }
  }, 500);
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
          <div class="details-field"><div class="details-label">Web Server</div><div class="details-value">${site.web_server==='nginx'?'Nginx':'Apache2'}</div></div>
          <div class="details-field"><div class="details-label">Node ID</div><div class="details-value">${site.node_id}</div></div>
        </div>
      </div>
      <div style="display:grid;grid-template-columns:2fr 1fr 1fr;gap:12px;margin-bottom:12px;">
        <div id="rt-card" class="card"></div>
        <div id="site-cols-left" style="display:grid;gap:12px;align-content:start;"></div>
        <div id="site-cols-right" style="display:grid;gap:12px;align-content:start;"></div>
      </div>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;" id="site-cols-bottom"></div>`;
    const [domains, databases, ssl, proxy, backups] = await Promise.all([
      api('/api/domains'), api('/api/databases'), api('/api/ssl'), api('/api/proxy'), api('/api/backups')
    ]);
    // Existing cards: Domains + SSL in left column, Databases in right column
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
    colRight.innerHTML = makeCard('Databases', (databases.data||[]).filter(d=>d.site_id==site.id).map(d=>d.name), '#8b5cf6');
    colBottom.innerHTML =
      makeCard('Proxy', (proxy.data||[]).filter(p=>p.site_id==site.id).map(p=>p.status), '#06b6d4') +
      makeCard('Backups', (backups.data||[]).filter(b=>b.site_id==site.id).map(b=>b.filename), '#f97316');
    // Load runtime card
    loadRuntimeCard(site.id, site.domain, site.web_server);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load site</div>'; }
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
    setTimeout(() => {
      const domainEl = document.querySelector('#rt-card');
      if (domainEl) refreshRuntimeCard(siteId, domain, '');
    }, 1500);
  }).catch(e => {
    toast('Error: ' + e.message, 'error');
  });
}

/* ===== DOMAINS ===== */
function domainTypeBadge(type) {
  const m = {'primary':'badge-ok','alias':'badge-info','redirect':'badge-warn','wildcard':'badge-info'};
  const label = type || 'legacy';
  return `<span class="badge ${m[label]||'badge-err'}">${esc(label)}</span>`;
}

function domainSslBadge(status) {
  const m={'Active':'badge-ok','Disabled':'badge-info','Expired':'badge-err','Expiring':'badge-warn','Error':'badge-err','Issuing':'badge-warn'};
  return `<span class="badge ${m[status]||'badge-info'}">${esc(status)}</span>`;
}

async function loadDomains(p) {
  try {
    const data = await api('/api/domains');
    p.innerHTML = `<div class="page-header"><h1>Domains</h1></div>`;
    p.innerHTML += tb('All Domains');
    window.renderTable = () => {
      const tbl = $('domains-table');
      if (!tbl) return;
      const rows = (data.data||[]).filter(r=>!searchTerm||r.domain.includes(searchTerm)||(r.site_name||'').includes(searchTerm));
      tbl.innerHTML = buildTable([
        {label:'Domain',html:r=>`<a href="http://${esc(r.domain)}" target="_blank" style="color:var(--primary);text-decoration:none;" title="Open in browser">${esc(r.domain)}</a> <span style="cursor:pointer;font-size:11px;color:var(--text3);" onclick="copyText('${esc(r.domain)}')" title="Copy domain">&#128203;</span>`},
        {label:'Type',html:r=>domainTypeBadge(r.type)},
        {label:'Site',html:r=>r.site_name?`${esc(r.site_name)}<br><span style="font-size:11px;color:var(--text3);">${esc(r.site_domain||'')}</span>`:`<span class="badge badge-info">Unlinked</span>`},
        {label:'Target',html:r=>r.target?esc(r.target):r.site_domain?esc(r.site_domain):'<span class="badge badge-info">—</span>'},
        {label:'DNS',html:()=>'<span class="badge badge-info">Unknown</span>'},
        {label:'HTTP',html:()=>'<span class="badge badge-info">Unknown</span>'},
        {label:'SSL',html:r=>domainSslBadge(r.ssl_status)},
        {label:'Actions',html:r=>`<button class="btn-icon" onclick="window.open('http://${esc(r.domain)}','_blank')" title="Open">&#8599;</button><button class="btn-icon" onclick="copyText('${esc(r.domain)}')" title="Copy">&#128203;</button>${r.site_id?`<button class="btn-icon" onclick="navigate('site-detail',${r.site_id})" title="View site">&#128065;</button>`:''}<button class="btn-icon" style="color:var(--red)" onclick="removeDomain('${esc(r.domain)}')" title="Remove">&#10005;</button>`}
      ], rows, 'No domains');
    };
    p.innerHTML += `<div id="domains-table"></div>`;
    window.renderTable();
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load domains</div>'; }
}

function copyText(text) {
  navigator.clipboard.writeText(text).then(() => toast('Copied: '+text, 'success')).catch(() => toast('Copy failed', 'error'));
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
  let html = '';

  if (r.status === 'HTTP_ONLY' || r.status === 'error') {
    html += `<button class="btn-icon" onclick="issueSsl('${d}')" title="Issue Certificate">&#9679; Issue</button>`;
  }
  if (r.status === 'active') {
    if (r.https_enabled) {
      html += `<button class="btn-icon" onclick="toggleSsl('${d}',false)" title="Disable HTTPS">&#9646;&#9646; HTTPS</button>`;
    } else {
      html += `<button class="btn-icon" onclick="toggleSsl('${d}',true)" title="Enable HTTPS">&#9654; HTTPS</button>`;
    }
    html += `<button class="btn-icon" onclick="renewSsl('${d}')" title="Renew Certificate">&#8635; Renew</button>`;
    if (r.https_enabled) {
      if (r.redirect_enabled) {
        html += `<button class="btn-icon" onclick="toggleRedirect('${d}',false)" title="Disable Redirect">&#8592; No Redirect</button>`;
      } else {
        html += `<button class="btn-icon" onclick="toggleRedirect('${d}',true)" title="Enable Redirect">&#8594; Redirect</button>`;
      }
    }
  }
  if (r.status === 'disabled') {
    html += `<button class="btn-icon" onclick="toggleSsl('${d}',true)" title="Enable HTTPS">&#9654; HTTPS</button>`;
  }

  return html || '<span class="badge badge-info">No actions</span>';
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
        {label:'Domain', html:r=>`<a href="#" onclick="loadSite('${esc(r.domain)}');return false">${esc(r.domain)}</a>`},
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
      {label:'Actions',html:r=>r.status==='completed'?`<button class="btn-icon" title="Restore" onclick="restoreBackup(${r.id},'${esc(r.filename)}')">&#8635;</button><button class="btn-icon" style="color:var(--red)" onclick="removeBackup(${r.id})">&#10005;</button>`:''}
    ], data.data||[]);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load backups</div>'; }
}

async function showBackupModal() {
  let sitesHtml = '<option value="" disabled selected>Loading sites...</option>';
  try {
    const sites = await api('/api/sites');
    const list = (sites.data||[]);
    if (list.length === 0) {
      sitesHtml = '<option value="" disabled selected>No sites available</option>';
    } else {
      sitesHtml = list.map(s => '<option value="'+esc(s.domain)+'">'+esc(s.domain)+'</option>').join('');
    }
  } catch(e) {
    sitesHtml = '<option value="" disabled selected>Failed to load sites</option>';
  }
  showModal('Create Backup', '<div><label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Site</label><select id="bk-domain" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">'+sitesHtml+'</select><div style="margin-top:8px;font-size:11px;color:var(--text3);">Or type a domain manually:</div><input id="bk-domain-manual" placeholder="example.com" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;margin-top:4px;"><button class="btn btn-primary" onclick="createBackup()" style="margin-top:12px;">Create Backup</button>', 420);
}

let creatingBackup = false;

async function createBackup() {
  if (creatingBackup) return;
  creatingBackup = true;
  const sel = $('bk-domain');
  const manual = $('bk-domain-manual');
  const domain = (sel && sel.value) ? sel.value : (manual ? manual.value.trim() : '');
  if (!domain) {
    toast('Select a site or enter a domain', 'error');
    creatingBackup = false;
    return;
  }
  hideModal();
  try {
    const res = await apiPost('/api/backups/create',{domain});
    if (res.success) {
      toast('Backup created: '+res.data.filename, 'success');
      loadBackups($('page'));
    } else {
      toast('Error: '+(res.error||'Unknown'), 'error');
    }
  } catch(e) {
    toast('Network error', 'error');
  } finally {
    creatingBackup = false;
  }
}

let restoringBackup = false;

async function restoreBackup(id, filename) {
  if (restoringBackup) return;
  if (!confirm('Restore backup ' + filename + '?')) return;
  restoringBackup = true;
  try {
    const res = await apiPost('/api/backups/restore', {id});
    if (res.success) {
      toast('Backup restored successfully', 'success');
      loadBackups($('page'));
    } else {
      toast('Error: ' + (res.error || 'Restore failed'), 'error');
    }
  } catch(e) {
    if (e.status) {
      toast('Error: ' + e.message, 'error');
    } else {
      toast('Network error', 'error');
    }
  } finally {
    restoringBackup = false;
  }
}

async function removeBackup(id) {
  if (!confirm('Remove backup?')) return;
  try {
    const res = await apiPost('/api/backups/remove', {id});
    if (res.success) {
      toast('Backup removed', 'success');
      loadBackups($('page'));
    } else {
      toast('Error: ' + (res.error || 'Remove failed'), 'error');
    }
  } catch(e) {
    if (e.status) {
      toast('Error: ' + e.message, 'error');
    } else {
      toast('Network error', 'error');
    }
  }
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
  let settings = {version:'...', server_hostname:''};
  try {
    const res = await api('/api/settings');
    if (res.success) settings = res.data;
  } catch(e) {}
  const host = esc(settings.server_hostname || '');

  p.innerHTML = `<div class="page-header"><h1>Settings</h1></div>
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

/* ===== THEME ===== */
function toggleTheme() {
  const html = document.documentElement;
  const isDark = html.getAttribute('data-theme') !== 'light';
  html.setAttribute('data-theme', isDark ? 'light' : 'dark');
  const val = $('themeValue');
  if (val) val.textContent = isDark ? 'Light' : 'Dark';
}

/* ===== VERSION ===== */
async function loadVersion() {
  try {
    const res = await api('/api/version');
    if (res.success && res.data && res.data.version) {
      const v = $('versionBadge');
      if (v) v.textContent = 'v' + res.data.version;
    }
  } catch(e) {
    const v = $('versionBadge');
    if (v) v.textContent = 'version unknown';
  }
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
