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
    err.code = body.error;
    err.api_message = body.message;
    err.body = body;
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
        <a class="nav-link" data-page="mail" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 4h16c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H4c-1.1 0-2-.9-2-2V6c0-1.1.9-2 2-2z"/><polyline points="22,6 12,13 2,6"/></svg> Mail</a>
        <a class="nav-link" data-page="webmail" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M4 4h16c1.1 0 2 .9 2 2v12c0 1.1-.9 2-2 2H4c-1.1 0-2-.9-2-2V6c0-1.1.9-2 2-2z"/><polyline points="22,6 12,13 2,6"/></svg> Webmail</a>
        <a class="nav-link" data-page="proxy" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><line x1="5" y1="12" x2="19" y2="12"/><polyline points="12 5 19 12 12 19"/></svg> Proxy</a>
        <a class="nav-link" data-page="access" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2"/><circle cx="12" cy="7" r="4"/></svg> Access</a>
        <a class="nav-link" data-page="backups" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M21 16V8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16z"/><polyline points="3.27 6.96 12 12.01 20.73 6.96"/></svg> Backups</a>
        <a class="nav-link" data-page="migration" href="#"><svg width="16" height="16" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><path d="M5 4h14M5 4v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V4M5 4H3m16 0h2"/><path d="M9 10l3 3 3-3"/><path d="M12 7v6"/></svg> Migration</a>
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
  else if (page === 'domain-detail') loadDomainDetail(p, params);
  else if (page === 'mail') loadMail(p);
  else if (page === 'mail-domain') loadMailDomain(p, params);
  else if (page === 'mail-health') loadMailHealth(p);
  else if (page === 'webmail') loadWebmail(p);
  else if (page === 'databases') loadDatabases(p);
  else if (page === 'ssl') loadSsl(p);
  else if (page === 'proxy') loadProxy(p);
  else if (page === 'access') loadAccess(p);
  else if (page === 'backups') loadBackups(p);
  else if (page === 'migration') loadMigration(p);
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
        <div class="health-item" id="mail-health-dot"><div class="health-dot"></div><div><div class="health-name">Mail</div><div class="health-label">loading...</div></div></div>
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
    // Mail health dot
    api('/api/mail/health').then(h => {
      const dot = $('mail-health-dot');
      if (!dot) return;
      const status = h.data?.status || 'error';
      const d = dot.querySelector('.health-dot');
      if (d) d.className = 'health-dot ' + (status === 'ok' ? 'ok' : status === 'degraded' ? 'warn' : 'error');
      const l = dot.querySelector('.health-label');
      if (l) l.textContent = status === 'ok' ? 'Healthy' : status === 'degraded' ? 'Warning' : 'Error';
    }).catch(() => {
      const dot = $('mail-health-dot');
      if (dot) { const d = dot.querySelector('.health-dot'); if(d) d.className = 'health-dot'; const l = dot.querySelector('.health-label'); if(l) l.textContent = 'Inactive'; }
    });
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
      const badgeCls = {'Active':'badge-ok','Running':'badge-ok','Running':'badge-ok','N/A':'badge-info'};
      const rtM = {'Running':'badge-ok','Active':'badge-ok','Stopped':'badge-err','Unhealthy':'badge-warn','Starting':'badge-warn','Expiring':'badge-warn','Error':'badge-err','Expired':'badge-err','Disabled':'badge-info','Issuing':'badge-warn','Unknown':'badge-info','N/A':'badge-info'};
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

    var isSystem = site.system_role === 'admin-panel' || site.can_delete === false;

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
        <div class="card"><h3>Web Service</h3><div style="margin-top:8px;font-size:13px;"><div>Status: <span class="badge badge-ok">${esc(site.web_status||'Active')}</span></div></div></div>
        <div class="card"><h3>PHP</h3><div style="margin-top:8px;font-size:13px;"><div>Status: <span class="badge badge-info">N/A</span></div></div></div>
        <div class="card"><h3>Databases</h3><div style="margin-top:8px;font-size:13px;"><div>Not applicable</div></div></div>
        <div class="card"><h3>Backups</h3><div style="margin-top:8px;font-size:13px;"><div>Not applicable</div></div></div>
      </div>
      <div style="margin-top:12px;">
        <a href="#" onclick="navigate('domain-detail',0);return false" class="btn btn-sm">View Domain Configuration</a>
        <a href="#" onclick="navigate('ssl',0);return false" class="btn btn-sm" style="margin-left:4px;">View SSL</a>
      </div>` : `
      <div style="display:grid;grid-template-columns:2fr 1fr 1fr;gap:12px;margin-bottom:12px;">
        <div id="rt-card" class="card"></div>
        <div id="site-cols-left" style="display:grid;gap:12px;align-content:start;"></div>
        <div id="site-cols-right" style="display:grid;gap:12px;align-content:start;"></div>
      </div>
      <div style="display:grid;grid-template-columns:1fr 1fr;gap:12px;" id="site-cols-bottom"></div>
      <div style="margin-top:12px;" id="site-php-mail"></div>`}`;

    if (isSystem) return;

    const [domains, databases, ssl, proxy, backups] = await Promise.all([
      api('/api/domains'), api('/api/databases'), api('/api/ssl'), api('/api/proxy'), api('/api/backups')
    ]);
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
    loadPhpMailCard(site.id, site.domain);
    loadRuntimeCard(site.id, site.domain, site.web_server);
  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load site</div>'; }
}

/* ===== PHP MAIL CARD ===== */
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
  const m = {'primary':'badge-ok','alias':'badge-info','redirect':'badge-warn','wildcard':'badge-info','legacy':'badge-info','system':'badge-admin'};
  const label = type || 'legacy';
  return `<span class="badge ${m[label]||'badge-info'}">${esc(label)}</span>`;
}

function domainSslBadge(status) {
  const m={'Active':'badge-ok','Disabled':'badge-info','Expired':'badge-err','Expiring':'badge-warn','Error':'badge-err','Issuing':'badge-warn'};
  return `<span class="badge ${m[status]||'badge-info'}">${esc(status)}</span>`;
}

function domainUsableHttps(r) {
  // Only suggest HTTPS when the certificate is actually usable
  return r.ssl_status === 'Active' || r.ssl_status === 'Expiring';
}

async function loadDomains(p) {
  try {
    const data = await api('/api/domains');
    const domains = data.data || [];
    p.innerHTML = `<div class="page-header"><h1>Domains</h1><div class="page-actions" style="font-size:12px;color:var(--text3);font-weight:normal;">${domains.length} domain${domains.length===1?'':'s'}</div></div>`;
    p.innerHTML += tb('All Domains');

    window.renderTable = () => {
      const tbl = $('domains-table');
      if (!tbl) return;
      const lowerSearch = (searchTerm||'').toLowerCase();
      const rows = domains.filter(r => {
        if (!lowerSearch) return true;
        return (r.domain||'').toLowerCase().includes(lowerSearch)
            || (r.site_name||'').toLowerCase().includes(lowerSearch)
            || (r.site_domain||'').toLowerCase().includes(lowerSearch)
            || (r.target||'').toLowerCase().includes(lowerSearch)
            || (r.type||'').toLowerCase().includes(lowerSearch)
            || (r.ssl_status||'').toLowerCase().includes(lowerSearch);
      });
      tbl.innerHTML = buildTable([
        {label:'Domain', html: r => `<a href="#" onclick="navigate('domain-detail',${r.id});return false" style="color:var(--primary);text-decoration:none;font-weight:500;">${esc(r.domain)}</a> <span style="cursor:pointer;font-size:11px;color:var(--text3);" onclick="copyText('${esc(r.domain)}')" title="Copy domain">&#128203;</span>`},
        {label:'Type', html: r => domainTypeBadge(r.type)},
        {label:'Site', html: r => r.site_name ? `<div style="line-height:1.4;"><div>${esc(r.site_name)}</div><div style="font-size:11px;color:var(--text3);">${esc(r.site_domain||'')}</div></div>` : `<span class="badge badge-info">Unlinked</span>`},
        {label:'Target', html: r => {
          if (r.target) return `<span style="word-break:break-all;">${esc(r.target)}</span>`;
          if (r.site_domain) return `<span style="color:var(--text3);">${esc(r.site_domain)}</span>`;
          return '<span class="badge badge-info">—</span>';
        }},
        {label:'DNS', html: r => {
          const dnsData = DnsCache.get(r.domain, 'A,AAAA,MX');
          if (!dnsData) return '<span class="badge badge-info">...</span>';
          return window.dnsStatusBadge(dnsData.overall_status);
        }},
        {label:'Mail', html: r => r.mail_domain_id && r.mail_domain_id > 0 ? '<span class="badge badge-ok">Active</span>' : '<span class="badge badge-info">—</span>'},
        {label:'Runtime', html: r => {
          if (r.site_id === 0) return '<span class="badge badge-info">N/A</span>';
          if (!r.site_id && r.site_id !== 0) return '<span class="badge badge-info">N/A</span>';
          const rtData = RuntimeCache.get(r.site_id);
          if (!rtData) return '<span class="badge badge-info">...</span>';
          return window.runtimeStatusBadge(rtData.web);
        }},
        {label:'SSL', html: r => domainSslBadge(r.ssl_status)},
        {label:'Health', html: r => {
          var cachedHealth = window.HealthCache.get(r.domain);
          if (cachedHealth && cachedHealth !== 'loading') return window.healthGradeBadge(cachedHealth.score, cachedHealth.grade);
          const dnsData = DnsCache.get(r.domain, 'A,AAAA,MX');
          const hs = window.computeDomainHealthScore({domainRow: r, rootDns: dnsData});
          return window.healthGradeBadge(hs.score, hs.grade);
        }},
        {label:'Actions', html: r => {
          let acts = `<button class="btn-icon" onclick="navigate('domain-detail',${r.id})" title="View details">&#128065;</button>`;
          acts += `<button class="btn-icon" onclick="window.open('${domainUsableHttps(r) ? 'https' : 'http'}://${esc(r.domain)}','_blank')" title="Open in browser">&#8599;</button>`;
          acts += `<button class="btn-icon" onclick="copyText('${esc(r.domain)}')" title="Copy domain">&#128203;</button>`;
          if (r.can_delete !== false) {
            acts += `<button class="btn-icon" style="color:var(--red)" onclick="removeDomain('${esc(r.domain)}')" title="Remove domain">&#10005;</button>`;
          }
          return acts;
        }}
      ], rows, 'No domains');
    };
    p.innerHTML += `<div id="domains-table"></div>`;
    window.renderTable();

    // Progressive DNS loading: concurrency=3, one domain at a time
    const rows = domains.filter(r => {
      if (!searchTerm) return true;
      return (r.domain||'').toLowerCase().includes((searchTerm||'').toLowerCase());
    });

    const domainListTypes = 'A,AAAA,MX';
    await window.processBatch(rows, 3, async (r) => {
      if (DnsCache.get(r.domain, domainListTypes)) return;
      if (DnsCache.isLoading(r.domain, domainListTypes)) {
        await DnsCache.waitFor(r.domain, domainListTypes);
        return;
      }
      DnsCache.setLoading(r.domain, domainListTypes);
      try {
        const res = await api('/api/domains/' + encodeURIComponent(r.domain) + '/dns-check?types=' + domainListTypes);
        DnsCache.set(r.domain, domainListTypes, res.data || {});
      } catch(e) {
        DnsCache.set(r.domain, domainListTypes, null);
        return;
      }
      const idx = rows.indexOf(r);
      const row = document.querySelector(`#domains-table table tbody tr:nth-child(${idx+1})`);
      if (!row) return;
      const cells = row.querySelectorAll('td');
      if (cells.length < 9) return;
      const dnsData = DnsCache.get(r.domain);
      cells[4].innerHTML = window.dnsStatusBadge(dnsData ? dnsData.overall_status : null);
      // Health score uses HealthCache.load (full context) or shows '...'
      window.HealthCache.load(r.domain, r, null, null).then(function(healthResult) {
        if (!healthResult || healthResult.score == null) return;
        var hRow = document.querySelector(`#domains-table table tbody tr:nth-child(${idx+1})`);
        if (!hRow) return;
        var hCells = hRow.querySelectorAll('td');
        if (hCells.length < 9) return;
        hCells[8].innerHTML = window.healthGradeBadge(healthResult.score, healthResult.grade);
      });
    });

    // Progressive Runtime loading (separate pass, concurrency=3)
    const siteRows = rows.filter(r => r.site_id && r.site_id > 0);
    await window.processBatch(siteRows, 3, async (r) => {
      if (RuntimeCache.get(r.site_id)) return;
      try {
        const res = await api('/api/runtime/' + r.site_id);
        RuntimeCache.set(r.site_id, res.data || {});
      } catch(e) {
        return;
      }
      const idx = rows.indexOf(r);
      const row = document.querySelector(`#domains-table table tbody tr:nth-child(${idx+1})`);
      if (!row) return;
      const cells = row.querySelectorAll('td');
      if (cells.length < 9) return;
      const rtData = RuntimeCache.get(r.site_id);
      if (rtData) cells[6].innerHTML = window.runtimeStatusBadge(rtData.web);
    });

  } catch(e) { p.innerHTML = '<div class="empty-state">Failed to load domains</div>'; }
}

// ===== DOMAIN DETAIL =====
let _currentDomain = null;
let _domainIdForTab = null;

async function loadDomainDetail(p, domainId) {
  _domainIdForTab = domainId;
  try {
    // Fetch domain data (from enriched list)
    const allDomains = await api('/api/domains');
    const domainRow = (allDomains.data || []).find(d => d.id == domainId);
    if (!domainRow) { p.innerHTML = '<div class="empty-state">Domain not found</div>'; return; }
    _currentDomain = domainRow;

    // Fetch mail domain data
    let mailDomain = null;
    try {
      const mdRes = await api('/api/mail/domains');
      mailDomain = (mdRes.data || []).find(m => m.domain === domainRow.domain || (domainRow.id > 0 && m.domain_id == domainId)) || null;
    } catch(e) {
      console.error('Failed to load mail domain', e);
    }

    // Fetch server hostname and its DNS (A/AAAA records) for expected IP comparison
    let serverHostname = '';
    let serverDns = null;
    try {
      const settingsRes = await api('/api/settings');
      if (settingsRes && settingsRes.data) {
        serverHostname = settingsRes.data.server_hostname || '';
        // Resolve server_hostname's A and AAAA records as the expected IPs
        if (serverHostname) {
          const srvDnsRes = await fetchDnsForFqdn(serverHostname, 'A,AAAA');
          if (srvDnsRes) serverDns = srvDnsRes;
        }
      }
    } catch(e) {
      console.error('Failed to load settings', e);
    }

    // SSL data from enriched GET /api/domains (ssl_status, ssl_enabled)
    // No extra SSL API call — use domainRow fields directly.
    // ssl_status values: Active, Disabled, Expired, Expiring, Error, Issuing

    let runtimeData = null;
    if (domainRow.site_id > 0) {
      try {
        const rtRes = await api('/api/runtime/' + domainRow.site_id);
        if (rtRes.success) runtimeData = rtRes.data;
      } catch(e) {
        console.error('Failed to load runtime data for site ' + domainRow.site_id, e);
      }
    }

    // Health score via HealthCache.load (async — updates header in-place)
    var hsInitial = window.HealthCache.get(domainRow.domain);
    var hsDisplay = hsInitial && hsInitial !== 'loading' ? hsInitial : null;
    var hsBadge = window.healthGradeBadge(hsDisplay ? hsDisplay.score : null, hsDisplay ? hsDisplay.grade : 'N/A');

    p.innerHTML = `
      <div class="page-header">
        <h1><a href="#" onclick="navigate('domains');return false" style="color:var(--text2);text-decoration:none;">&larr;</a> ${esc(domainRow.domain)}</h1>
        <div class="page-actions">
          <span class="health-badge" style="margin-right:8px;font-size:13px;">Health: ${hsBadge}</span>
          <button class="btn btn-sm" onclick="window.open('${domainUsableHttps(domainRow) ? 'https' : 'http'}://${esc(domainRow.domain)}','_blank')">Open</button>
          <button class="btn btn-sm" onclick="copyText('${esc(domainRow.domain)}')">Copy</button>
          ${domainRow.can_delete !== false ? `<button class="btn btn-sm btn-danger" onclick="removeDomain('${esc(domainRow.domain)}')">Remove</button>` : ''}
        </div>
      </div>
      <div class="tabs" id="domain-tabs">
        <div class="tab active" data-tab="overview" onclick="switchDomainTab('overview')">Overview</div>
        <div class="tab" data-tab="dns-records" onclick="switchDomainTab('dns-records')">DNS Records</div>
        <div class="tab" data-tab="mail" onclick="switchDomainTab('mail')">Mail</div>
        <div class="tab" data-tab="security" onclick="switchDomainTab('security')">Security</div>
        <div class="tab" data-tab="health" onclick="switchDomainTab('health')">Health</div>
      </div>
      <div id="domain-tab-content"></div>`;

    // Store data for tab access
    window._domainDetailData = {domainRow, mailDomain, runtimeData, serverHostname, serverDns};

    // Async health score load — updates header badge when ready
    window.HealthCache.load(domainRow.domain, domainRow, mailDomain, serverHostname).then(function(healthResult) {
      if (!healthResult || healthResult.score == null) return;
      var badgeSpan = p.querySelector('.page-actions .health-badge');
      if (!badgeSpan) return;
      badgeSpan.innerHTML = 'Health: ' + window.healthGradeBadge(healthResult.score, healthResult.grade);
    });

    // Load first tab
    loadDomainOverview();
  } catch(e) {
    p.innerHTML = '<div class="empty-state">Failed to load domain</div>';
  }
}

function switchDomainTab(tabId) {
  document.querySelectorAll('#domain-tabs .tab').forEach(t => t.classList.toggle('active', t.dataset.tab === tabId));
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  if (tabId === 'overview') loadDomainOverview();
  else if (tabId === 'dns-records') loadDomainDnsRecords();
  else if (tabId === 'mail') loadDomainMail();
  else if (tabId === 'security') loadDomainSecurity();
  else if (tabId === 'health') loadDomainHealth();
  else content.innerHTML = '<div class="empty-state">Coming soon</div>';
}

// Helper: fetch DNS check for a specific FQDN with typed cache
// Cache key includes both FQDN and type list (e.g., 'example.com|A,TXT')
// to prevent cache collisions between different type queries for the same domain.
async function fetchDnsForFqdn(fqdn, types) {
  const cached = DnsCache.get(fqdn, types);
  if (cached) return cached;
  if (DnsCache.isLoading(fqdn, types)) return DnsCache.waitFor(fqdn, types);
  DnsCache.setLoading(fqdn, types);
  try {
    const res = await api('/api/domains/' + encodeURIComponent(fqdn) + '/dns-check?types=' + types);
    if (res && res.success && res.data) {
      DnsCache.set(fqdn, types, res.data);
      return res.data;
    }
    DnsCache.set(fqdn, types, null);
    return null;
  } catch(e) {
    console.error('DNS check failed for ' + fqdn, e);
    DnsCache.set(fqdn, types, null);
    return null;
  }
}

// --- System domain action helper ---
async function runSystemAction(btnId, actionFn, confirmMsg, onSuccess) {
  var btn = document.getElementById(btnId);
  if (!btn || btn.disabled) return;
  if (confirmMsg && !confirm(confirmMsg)) return;
  btn.disabled = true;
  btn.textContent = 'Working...';
  try {
    var res = await actionFn();
    if (res && res.success) {
      toast(res.data && res.data.message ? res.data.message : 'Action completed', 'success');
      if (onSuccess) onSuccess();
    } else {
      toast('Error: ' + (res && res.error ? res.error : 'Unknown error'), 'error');
    }
  } catch(e) {
    toast('Error: ' + (e.message || 'Request failed'), 'error');
  }
  btn.disabled = false;
  btn.textContent = btnId === 'proxy-test-btn' ? 'Test Global Proxy Config' :
                    btnId === 'proxy-reload-btn' ? 'Reload Global Proxy' :
                    btnId === 'proxy-sync-btn' ? 'Sync All Proxy Configs' :
                    btnId === 'ssl-renew-btn' ? 'Renew Certificate' :
                    btnId === 'ssl-issue-btn' ? 'Issue New Certificate' : 'Action';
}

// Fetch SSL details and populate the SSL detail card
async function loadSslDetails(domain) {
  var infoEl = document.getElementById('ssl-detail-info');
  if (!infoEl) return;
  try {
    var res = await api('/api/ssl/' + encodeURIComponent(domain));
    if (res && res.success && res.data) {
      var d = res.data;
      infoEl.innerHTML = '';
      if (d.issuer) infoEl.innerHTML += '<div>Issuer: ' + esc(d.issuer) + '</div>';
      if (d.expires_at) infoEl.innerHTML += '<div>Expires: ' + esc(d.expires_at) + '</div>';
      if (d.domains && d.domains.length) infoEl.innerHTML += '<div>Domains: ' + esc(d.domains.join(', ')) + '</div>';
      if (d.status === 'active' && d.expires_at) {
        var days = Math.round((new Date(d.expires_at) - new Date()) / 86400000);
        if (days > 0) infoEl.innerHTML += '<div>Days remaining: ' + days + '</div>';
      }
    } else {
      infoEl.textContent = 'Certificate details not available';
    }
  } catch(e) {
    infoEl.textContent = 'Failed to load certificate details';
  }
}

// --- Overview tab ---
async function loadDomainOverview() {
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  const dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }
  const {domainRow, mailDomain, runtimeData, serverHostname, serverDns} = dd;
  const domain = domainRow.domain;

  content.innerHTML = '<div class="empty-state">Checking DNS...</div>';

  // Helper: format a record value for display (truncate if too long)
  function fmtVal(v) {
    if (!v || typeof v !== 'string') return '—';
    return v.length > 40 ? v.substr(0, 40) + '...' : v;
  }

  // Helper: get records from a dnsResult for a specific type
  function getRecs(dnsResult, typeName) {
    if (!dnsResult || !Array.isArray(dnsResult.per_type)) return [];
    const pt = dnsResult.per_type.find(x => x && x.type === typeName);
    if (!pt || !Array.isArray(pt.records)) return [];
    return pt.records;
  }

  // Fetch DNS data for each FQDN separately
  // Root domain: A, AAAA, MX, TXT (for SPF)
  const rootDns = await fetchDnsForFqdn(domain, 'A,AAAA,MX,TXT');

  // DKIM: query <selector>._domainkey.<domain> (separate FQDN)
  let dkimDns = null;
  if (mailDomain && mailDomain.dkim_public_key_dns) {
    const selector = mailDomain.dkim_selector || 'dkim';
    const dkimFqdn = selector + '._domainkey.' + domain;
    dkimDns = await fetchDnsForFqdn(dkimFqdn, 'TXT');
  }

  // DMARC: query _dmarc.<domain> (separate FQDN)
  let dmarcDns = null;
  if (mailDomain) {
    const dmarcFqdn = '_dmarc.' + domain;
    dmarcDns = await fetchDnsForFqdn(dmarcFqdn, 'TXT');
  }

  // Determine expected MX target based on MailDomain mode
  let expectedMx = window.getExpectedMxTarget(mailDomain, serverHostname);

  // Build DNS check summary table
  const expectedTypes = ['A', 'AAAA', 'MX'];
  const mailActive = mailDomain && mailDomain.mode !== 'disabled';
  if (mailActive) {
    expectedTypes.push('SPF');
    if (mailDomain.dkim_public_key_dns) expectedTypes.push('DKIM');
    expectedTypes.push('DMARC');
  }

  let dnsRows = '';
  for (const type of expectedTypes) {
    let configured = '';
    let published = '';
    let statusCls = 'badge-info';
    let statusLabel = '';
    let recs = [];
    let hasExpected = false;

    if (type === 'A') {
      recs = getRecs(rootDns, 'A');
      // Expected IPv4 from NetworkService (auto-detected, via API)
      configured = rootDns && typeof rootDns.expected_ipv4 === 'string' && rootDns.expected_ipv4.length > 0
        ? rootDns.expected_ipv4 : '';
      hasExpected = !!configured;
    } else if (type === 'AAAA') {
      recs = getRecs(rootDns, 'AAAA');
      // Expected IPv6 from NetworkService (auto-detected, via API)
      configured = rootDns && typeof rootDns.expected_ipv6 === 'string' && rootDns.expected_ipv6.length > 0
        ? rootDns.expected_ipv6 : '';
      hasExpected = !!configured;
    } else if (type === 'MX') {
      recs = getRecs(rootDns, 'MX');
      configured = expectedMx;
      hasExpected = !!configured;
    } else if (type === 'SPF') {
      recs = getRecs(rootDns, 'TXT').filter(r => typeof r.value === 'string' && r.value.startsWith('v=spf1'));
      configured = mailActive ? 'v=spf1 mx ~all' : '';
      hasExpected = !!configured;
    } else if (type === 'DKIM') {
      recs = getRecs(dkimDns, 'TXT');
      configured = mailDomain && mailDomain.dkim_public_key_dns ? mailDomain.dkim_public_key_dns : '';
      hasExpected = !!configured;
    } else if (type === 'DMARC') {
      recs = getRecs(dmarcDns, 'TXT');
      configured = mailActive ? 'v=DMARC1; p=none;' : '';
      hasExpected = !!configured;
    }

    if (recs.length > 0) {
      if (type === 'MX') {
        // MX: show hostname primarily, priority as secondary
        published = recs.map(r => fmtVal(r.value) + (r.priority ? ' (priority ' + r.priority + ')' : '')).join(', ');
      } else {
        published = recs.map(r => fmtVal(r.value)).join(', ');
      }
    }

    // Determine column label and status
    // For records where ContainerCP has a configured/stored value → "Configured"
    // For records where ContainerCP generates a recommendation → "Recommended"
    const colLabel = (type === 'SPF' || type === 'DMARC') ? 'Recommended' : 'Configured';
    const displayVal = configured ? fmtVal(configured) : '—';
    const displayPub = published || '—';

    if (hasExpected && published || hasExpected && !published || !hasExpected && published) {
      if (type === 'A') {
        const r = window.compareIpRecords(recs, configured);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'AAAA') {
        const r = window.compareIpRecords(recs, configured);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'MX') {
        const r = window.compareMxRecords(recs, expectedMx);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'SPF') {
        const r = window.compareSpfRecords(configured, published, rootDns && rootDns.spf_analysis);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'DKIM') {
        const pubVal = recs.length > 0 ? recs[0].value : '';
        const r = window.compareDkimRecords(configured, pubVal);
        statusLabel = r.status; statusCls = r.cls;
      } else if (type === 'DMARC') {
        const pubVal = recs.length > 0 ? recs[0].value : '';
        const r = window.compareDmarcRecords(configured, pubVal);
        statusLabel = r.status; statusCls = r.cls;
      } else {
        statusLabel = published ? 'Match' : 'N/A'; statusCls = published ? 'badge-ok' : 'badge-info';
      }
    }

    // Build column header: show "Recommended" for SPF/DMARC, "Configured" for others
    dnsRows += `<tr><td>${esc(type)}</td><td style="font-family:monospace;font-size:12px;">${esc(displayVal)}</td><td style="font-family:monospace;font-size:12px;">${esc(displayPub)}</td><td>${statusLabel ? '<span class="badge ' + statusCls + '">' + esc(statusLabel) + '</span>' : '<span class="badge badge-info">—</span>'}</td></tr>`;
  }

  // Build info cards
  const mailCard = mailDomain ? `
    <div class="card" style="cursor:pointer;" onclick="switchDomainTab('mail')">
      <h3>Mail</h3>
      <div style="margin-top:8px;font-size:13px;">
        <div>Domain: <strong>${esc(mailDomain.domain)}</strong></div>
        <div>Mode: <span class="badge badge-info">${esc(mailDomain.mode)}</span></div>
        <div>DKIM: ${mailDomain.dkim_public_key_dns ? '<span class="badge badge-ok">Generated</span>' : '<span class="badge badge-info">Not generated</span>'}</div>
      </div>
    </div>` : `
    <div class="card">
      <h3>Mail</h3>
      <div style="margin-top:8px;font-size:13px;color:var(--text3);">Not configured</div>
    </div>`;

  // SSL card: ssl_status as single source of truth. Both lines use badge components.
  const sslStatusDisplay = domainRow.ssl_status || 'Unknown';
  const sslBadgeCls = {'active':'badge-ok','http_only':'badge-info','disabled':'badge-info','expiring':'badge-warn','expired':'badge-err','error':'badge-err','issuing':'badge-warn'};
  const sslBadgeMap = {'active':'Active','http_only':'HTTP Only','disabled':'Disabled','expiring':'Expiring','expired':'Expired','error':'Error','issuing':'Issuing'};
  const sslKey = sslStatusDisplay.toLowerCase();
  const httpsBadge = (domainRow.ssl_enabled || sslStatusDisplay === 'Active' || sslStatusDisplay === 'Expiring') ? 'badge-ok' : 'badge-err';
  const httpsLabel = (domainRow.ssl_enabled || sslStatusDisplay === 'Active' || sslStatusDisplay === 'Expiring') ? 'Active' : 'Inactive';
  const sslCard = `
    <div class="card">
      <h3>SSL Certificate</h3>
      <div style="margin-top:8px;font-size:13px;">
        <div>Status: <span class="badge ${sslBadgeCls[sslKey] || 'badge-info'}">${esc(sslBadgeMap[sslKey] || sslStatusDisplay)}</span></div>
        <div>HTTPS: <span class="badge ${httpsBadge}">${httpsLabel}</span></div>
      </div>
    </div>`;

  const siteCard = domainRow.site_name ? `
    <div class="card" ${domainRow.site_id > 0 ? `onclick="navigate('site-detail',${domainRow.site_id})" style="cursor:pointer;"` : ''}>
      <h3>Site</h3>
      <div style="margin-top:8px;font-size:13px;">
        <div>Name: <strong>${esc(domainRow.site_name)}</strong></div>
        <div>Domain: ${esc(domainRow.site_domain || '')}</div>
        <div>Runtime: ${runtimeData ? window.runtimeStatusBadge(runtimeData.web) : '<span class="badge badge-info">N/A</span>'}</div>
      </div>
    </div>` : '';

  content.innerHTML = `
    <div style="margin-bottom:12px;">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
        <h3 style="font-size:14px;">DNS Check</h3>
        <button class="btn btn-sm" onclick="refreshDomainOverview()">Check Again</button>
      </div>
      <div class="table-wrap">
        <table>
          <thead><tr><th>Type</th><th>Configured</th><th>Published</th><th>Status</th></tr></thead>
          <tbody>${dnsRows}</tbody>
        </table>
      </div>
    </div>
    <div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(240px,1fr));gap:12px;">
      ${mailCard}
      ${sslCard}
      ${siteCard}
      ${domainRow.system_role === 'admin-panel' ? `
      <div class="card">
        <h3>System: Admin Panel</h3>
        <div style="margin-top:8px;font-size:13px;">
          <div>Site ID: <code>${domainRow.site_id}</code></div>
          <div>Role: <span class="badge badge-admin">${esc(domainRow.system_role)}</span></div>
          <div>FQDN: <strong>${esc(domainRow.domain)}</strong></div>
          <div>Proxy upstream: <code>${esc(domainRow.proxy_upstream || '—')}</code></div>
          <div>SSL: <span class="badge ${sslBadgeCls[sslKey] || 'badge-info'}">${esc(sslBadgeMap[sslKey] || sslStatusDisplay)}</span></div>
          <div>Runtime: <span class="badge badge-info">N/A</span></div>
        </div>
      </div>` : ''}
      ${domainRow.can_manage_proxy ? `
      <div class="card">
        <h3>Proxy Configuration</h3>
        <div style="margin-top:8px;font-size:13px;">
          <div>Upstream: <code>${esc(domainRow.proxy_upstream || '—')}</code></div>
          <div>Status: <span class="badge badge-info">${domainRow.proxy_upstream ? 'Available' : 'Not verified'}</span></div>
          <div style="margin-top:8px;font-size:11px;color:var(--text3);">
            These operations affect the central reverse proxy for ALL domains.
            An invalid configuration may interrupt access to ContainerCP and other sites.
          </div>
          <div style="margin-top:8px;">
            <button class="btn btn-sm" id="proxy-test-btn">Test Global Proxy Config</button>
            <button class="btn btn-sm" id="proxy-reload-btn" style="margin-left:4px;">Reload Global Proxy</button>
            <button class="btn btn-sm" id="proxy-sync-btn" style="margin-left:4px;">Sync All Proxy Configs</button>
          </div>
        </div>
      </div>` : ''}
      ${domainRow.can_manage_ssl ? `
      <div class="card" id="ssl-detail-card">
        <h3>SSL Certificate</h3>
        <div style="margin-top:8px;font-size:13px;">
          <div>Status: <span class="badge ${sslBadgeCls[sslKey] || 'badge-info'}">${esc(sslBadgeMap[sslKey] || sslStatusDisplay)}</span></div>
          <div>HTTPS: <span class="badge ${httpsBadge}">${httpsLabel}</span></div>
          <div style="margin-top:4px;font-size:11px;color:var(--text3);" id="ssl-detail-info">Loading details...</div>
          <div style="margin-top:8px;font-size:11px;color:var(--text3);">
            ACME rate limits apply. Temporary HTTPS interruption may occur during issuance.
          </div>
          <div style="margin-top:8px;">
            ${sslStatusDisplay === 'Active' || sslStatusDisplay === 'Expiring' ? `<button class="btn btn-sm" id="ssl-renew-btn">Renew Certificate</button>` : ''}
            ${sslStatusDisplay === 'Disabled' || sslStatusDisplay === 'Error' || sslStatusDisplay === '' || sslStatusDisplay === 'HTTP_ONLY' || sslStatusDisplay === 'http_only' ? `<button class="btn btn-sm" id="ssl-issue-btn">Issue New Certificate</button>` : ''}
            ${sslStatusDisplay === 'Issuing' ? `<span class="badge badge-warn">Issuance in progress</span>` : ''}
          </div>
        </div>
      </div>` : ''}
    </div>`;

  // Wire system-domain action buttons via single onclick handler (no accumulation)
  // Using onclick = assignment replaces previous handler, preventing duplicates
  var tabContent = document.getElementById('domain-tab-content');
  if (tabContent) {
    var handler = function(e) {
      var target = e.target;
      if (target.id === 'proxy-test-btn')
        runSystemAction('proxy-test-btn', function() { return apiPost('/api/proxy/test'); }, null);
      else if (target.id === 'proxy-reload-btn')
        runSystemAction('proxy-reload-btn', function() { return apiPost('/api/proxy/reload'); },
          'Reload central reverse proxy?\n\nThis will restart nginx for ALL domains. An invalid configuration may interrupt access to ContainerCP and other sites.');
      else if (target.id === 'proxy-sync-btn')
        runSystemAction('proxy-sync-btn', function() { return apiPost('/api/proxy/sync'); },
          'Synchronize all proxy configurations?\n\nThis regenerates HTTPS proxy configs for ALL domains.');
      else if (target.id === 'ssl-renew-btn')
        runSystemAction('ssl-renew-btn', function() { return apiPost('/api/ssl/' + encodeURIComponent(domainRow.domain) + '/renew'); },
          'Renew SSL certificate for ' + domainRow.domain + '?\n\nACME rate limits apply. Temporary HTTPS interruption may occur.',
          function() { loadSslDetails(domainRow.domain); refreshDomainOverview(); });
      else if (target.id === 'ssl-issue-btn')
        runSystemAction('ssl-issue-btn', function() { return apiPost('/api/ssl/' + encodeURIComponent(domainRow.domain) + '/issue', {provider_id:'letsencrypt'}); },
          'Issue new SSL certificate for ' + domainRow.domain + '?\n\nACME rate limits apply. Temporary HTTPS interruption may occur.',
          function() { loadSslDetails(domainRow.domain); refreshDomainOverview(); });
    };
    tabContent.onclick = handler;
  }

  // Load SSL details for the admin panel
  if (domainRow.can_manage_ssl && domainRow.system_role === 'admin-panel') {
    loadSslDetails(domainRow.domain);
  }
}

async function refreshDomainOverview() {
  if (!_currentDomain) return;
  DnsCache.clear(_currentDomain.domain);  // clears ALL type variants for this domain
  loadDomainOverview();
}

// Universal Full DNS Record formatter
function buildFullDnsRecord(fqdn, ttl, type, value, priority) {
  const name = fqdn.endsWith('.') ? fqdn : fqdn + '.';
  const ttlStr = (ttl && ttl > 0) ? ' ' + ttl : ' 3600';
  if (type === 'MX') {
    const prio = priority ? ' ' + priority : ' 10';
    return name + ttlStr + ' IN MX' + prio + ' ' + (value.endsWith('.') ? value : value + '.');
  }
  if (type === 'TXT' || type === 'SPF' || type === 'DKIM' || type === 'DMARC' || type === 'MTA-STS') {
    return name + ttlStr + ' IN TXT "' + value + '"';
  }
  if (type === 'CAA') {
    return name + ttlStr + ' IN CAA ' + value;
  }
  if (type === 'CNAME') {
    return name + ttlStr + ' IN CNAME ' + (value.endsWith('.') ? value : value + '.');
  }
  return name + ttlStr + ' IN ' + type + ' ' + value;
}

// Normalize hostname for comparison: lowercase, strip trailing dot
function normalizeHostname(h) {
  if (!h || typeof h !== 'string') return '';
  h = h.toLowerCase();
  if (h.endsWith('.')) h = h.slice(0, -1);
  return h;
}

// Attach data-copy event listener (single handler for all copy buttons)
function attachDataCopyListener(containerId) {
  const container = document.getElementById(containerId);
  if (!container) return;
  container.addEventListener('click', function(e) {
    const btn = e.target.closest('[data-copy]');
    if (!btn) return;
    const text = btn.getAttribute('data-copy');
    if (text) copyText(text, btn.getAttribute('data-copy-msg') || 'Copied');
  });
}

// --- DNS Records tab ---
async function loadDomainDnsRecords() {
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  const dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }
  const {domainRow, mailDomain, serverHostname} = dd;
  const domain = domainRow.domain;

  content.innerHTML = '<div class="empty-state">Checking DNS...</div>';

  function getRecs(dnsResult, typeName) {
    if (!dnsResult || !Array.isArray(dnsResult.per_type)) return [];
    const pt = dnsResult.per_type.find(x => x && x.type === typeName);
    if (!pt || !Array.isArray(pt.records)) return [];
    return pt.records;
  }

  const rootDns = await fetchDnsForFqdn(domain, 'A,AAAA,MX,TXT,NS,CNAME,CAA');
  let dkimDns = null, dmarcDns = null, mtaStsDns = null;

  if (mailDomain && mailDomain.dkim_public_key_dns) {
    const sel = mailDomain.dkim_selector || 'dkim';
    dkimDns = await fetchDnsForFqdn(sel + '._domainkey.' + domain, 'TXT');
  }
  if (mailDomain) {
    dmarcDns = await fetchDnsForFqdn('_dmarc.' + domain, 'TXT');
    mtaStsDns = await fetchDnsForFqdn('_mta-sts.' + domain, 'TXT');
  }

  let expectedMx = '';
  expectedMx = window.getExpectedMxTarget(mailDomain, serverHostname);

  const now = Date.now();
  const ts = new Date(now).toLocaleTimeString();

  function fmtVal(v, max) {
    if (!v || typeof v !== 'string') return '—';
    if (v.length > (max || 40)) return esc(v.substr(0, max || 40)) + '...';
    return esc(v);
  }

  function statusBadge(label, cls) {
    if (!label) return '<span class="badge badge-info">—</span>';
    return `<span class="badge ${cls || 'badge-info'}">${esc(label)}</span>`;
  }

  // Data-copy buttons: safe pattern with attribute-based copying
  function copyBtn(text, shortLabel, fullLabel) {
    const safe = text.replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
    return `<button class="btn-icon" data-copy="${safe}" title="${esc(fullLabel || shortLabel)}">${esc(shortLabel)}</button>`;
  }

  function copyRowButtons(record) {
    const {host, type, value, ttl, priority, domainName} = record;
    const name = host === '@' ? domainName : host;
    const fqdn = name.endsWith('.') ? name : name + '.';
    const fullRecord = buildFullDnsRecord(name, ttl, type, value, priority);
    return `<span style="white-space:nowrap;">
      ${copyBtn(name, 'H', 'Copy Host')}
      ${copyBtn(value, 'V', 'Copy Value')}
      ${copyBtn(fqdn, 'F', 'Copy FQDN')}
      ${copyBtn(fullRecord, 'R', 'Copy Full Record')}
    </span>`;
  }

  let rows = '';

  // 1. A record
  {
    const recs = getRecs(rootDns, 'A');
    const expected = rootDns && rootDns.expected_ipv4 || '';
    const publishedIps = recs.map(r => r.value).filter(Boolean);
    const published = publishedIps.join(', ');
    const aR = window.compareIpRecords(recs, expected);
    const ttl = recs.length > 0 ? recs[0].ttl : 0;
    const valToCopy = publishedIps[0] || expected;
    rows += `<tr>
      <td>${statusBadge(aR.status, aR.cls)}</td>
      <td>A</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${fmtVal(expected)}</td>
      <td style="font-family:monospace;">${fmtVal(published)}</td>
      <td>${ttl || '—'}</td>
      <td>${valToCopy ? copyRowButtons({host:'@', type:'A', value:valToCopy, ttl, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 2. AAAA
  {
    const recs = getRecs(rootDns, 'AAAA');
    const expected = rootDns && typeof rootDns.expected_ipv6 === 'string' ? rootDns.expected_ipv6 : '';
    const aaaaR = window.compareIpRecords(recs, expected);
    const publishedIps = recs.map(r => r.value).filter(Boolean);
    const published = publishedIps.join(', ');
    const ttl = recs.length > 0 ? recs[0].ttl : 0;
    const valToCopy = publishedIps[0] || expected;
    rows += `<tr>
      <td>${statusBadge(aaaaR.status, aaaaR.cls)}</td>
      <td>AAAA</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${fmtVal(expected)}</td>
      <td style="font-family:monospace;">${fmtVal(published)}</td>
      <td>${ttl || '—'}</td>
      <td>${valToCopy ? copyRowButtons({host:'@', type:'AAAA', value:valToCopy, ttl, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 3. MX
  {
    const recs = getRecs(rootDns, 'MX');
    const configured = expectedMx || '';
    const r = window.compareMxRecords(recs, configured);
    const publishedStr = window.formatMxPublished(recs);
    rows += `<tr>
      <td>${statusBadge(r.status, r.cls)}</td>
      <td>MX</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${fmtVal(configured)}</td>
      <td style="font-family:monospace;">${fmtVal(publishedStr)}</td>
      <td>—</td>
      <td>${recs.length > 0 ? copyRowButtons({host:'@', type:'MX', value:recs[0].value || configured, ttl:recs[0].ttl, priority:recs[0].priority, domainName:domain}) : configured ? copyRowButtons({host:'@', type:'MX', value:configured, ttl:3600, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 4. SPF
  {
    const recs = getRecs(rootDns, 'TXT').filter(r => typeof r.value === 'string' && r.value.startsWith('v=spf1'));
    const recommended = mailDomain ? 'v=spf1 mx ~all' : '';
    const publishedVal = recs.length > 0 ? recs[0].value : '';
    const r = window.compareSpfRecords(recommended, publishedVal, rootDns && rootDns.spf_analysis);
    const val = publishedVal || recommended;
    rows += `<tr>
      <td>${statusBadge(r.status, r.cls)}</td>
      <td>SPF</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${recommended ? esc(recommended) : '—'}</td>
      <td style="font-family:monospace;">${fmtVal(publishedVal)}</td>
      <td>${recs.length > 0 ? recs[0].ttl : '—'}</td>
      <td>${val ? copyRowButtons({host:'@', type:'TXT', value:val, ttl:recs.length > 0 ? recs[0].ttl : 3600, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 5. DKIM
  if (dkimDns) {
    const recs = getRecs(dkimDns, 'TXT');
    const selector = mailDomain ? mailDomain.dkim_selector || 'dkim' : 'dkim';
    const host = selector + '._domainkey.' + domain;
    const pubKey = mailDomain ? mailDomain.dkim_public_key_dns || '' : '';
    const publishedVal = recs.length > 0 ? recs[0].value : '';
    const dkimR = window.compareDkimRecords(pubKey, publishedVal);
    const ttl = recs.length > 0 ? recs[0].ttl : 0;
    const val = publishedVal || pubKey;
    rows += `<tr>
      <td>${statusBadge(dkimR.status, dkimR.cls)}</td>
      <td>DKIM</td>
      <td style="font-family:monospace;">${esc(host)}</td>
      <td style="font-family:monospace;">${pubKey ? fmtVal(pubKey, 60) : '—'}</td>
      <td style="font-family:monospace;">${publishedVal ? fmtVal(publishedVal, 60) : '—'}</td>
      <td>${ttl || '—'}</td>
      <td>${val ? copyRowButtons({host, type:'TXT', value:val, ttl: ttl || 3600, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 6. DMARC
  if (dmarcDns) {
    const recs = getRecs(dmarcDns, 'TXT');
    const recommended = 'v=DMARC1; p=none;';
    const host = '_dmarc.' + domain;
    const publishedVal = recs.length > 0 ? recs[0].value : '';
    const dmarcR = window.compareDmarcRecords(recommended, publishedVal);
    const dmarcVal = publishedVal || recommended;
    rows += `<tr>
      <td>${statusBadge(dmarcR.status, dmarcR.cls)}</td>
      <td>DMARC</td>
      <td style="font-family:monospace;">${esc(host)}</td>
      <td style="font-family:monospace;">${esc(recommended)}</td>
      <td style="font-family:monospace;">${fmtVal(publishedVal)}</td>
      <td>${recs.length > 0 ? recs[0].ttl : '—'}</td>
      <td>${dmarcVal ? copyRowButtons({host, type:'TXT', value:dmarcVal, ttl: recs.length > 0 ? recs[0].ttl : 3600, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 7. CAA
  {
    const recs = getRecs(rootDns, 'CAA');
    const recommended = '0 issue "letsencrypt.org"';
    const hasPublished = recs.length > 0;
    let status, cls;
    if (hasPublished) {
      const hasRequired = recs.some(r => {
        const v = (r.value || '').replace(/\s+/g, '');
        const rn = recommended.replace(/\s+/g, '');
        return v === rn;
      });
      status = hasRequired ? 'Match' : 'Mismatch';
      cls = hasRequired ? 'badge-ok' : 'badge-warn';
    } else { status = 'Not Published'; cls = 'badge-err'; }
    const publishedStr = recs.map(r => r.value).join(', ');
    rows += `<tr>
      <td>${statusBadge(status, cls)}</td>
      <td>CAA</td>
      <td style="font-family:monospace;">@</td>
      <td style="font-family:monospace;">${esc(recommended)}</td>
      <td style="font-family:monospace;">${fmtVal(publishedStr)}</td>
      <td>—</td>
      <td>${copyRowButtons({host:'@', type:'CAA', value:recommended, ttl:3600, domainName:domain})}</td>
    </tr>`;
  }

  // 8. NS (informational)
  {
    const recs = getRecs(rootDns, 'NS');
    const hasPublished = recs.length > 0;
    const publishedStr = recs.map(r => r.value).join(', ');
    const val = recs[0] ? recs[0].value : '';
    rows += `<tr>
      <td>${statusBadge(hasPublished ? 'Found' : 'N/A', hasPublished ? 'badge-ok' : 'badge-info')}</td>
      <td>NS</td>
      <td style="font-family:monospace;">@</td>
      <td>—</td>
      <td style="font-family:monospace;">${fmtVal(publishedStr)}</td>
      <td>${recs.length > 0 ? recs[0].ttl : '—'}</td>
      <td>${val ? copyRowButtons({host:'@', type:'NS', value:val, ttl:recs[0].ttl, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  // 9. MTA-STS (informational if no expected value)
  if (mtaStsDns) {
    const recs = getRecs(mtaStsDns, 'TXT');
    const hasPublished = recs.length > 0;
    const val = hasPublished ? recs[0].value : '';
    const host = '_mta-sts.' + domain;
    rows += `<tr>
      <td>${statusBadge(hasPublished ? 'Found' : 'N/A', hasPublished ? 'badge-ok' : 'badge-info')}</td>
      <td>MTA-STS</td>
      <td style="font-family:monospace;">${esc(host)}</td>
      <td>—</td>
      <td style="font-family:monospace;">${fmtVal(val)}</td>
      <td>${recs.length > 0 ? recs[0].ttl : '—'}</td>
      <td>${val ? copyRowButtons({host, type:'TXT', value:val, ttl:recs[0].ttl, domainName:domain}) : '—'}</td>
    </tr>`;
  }

  content.innerHTML = `
    <div id="dns-records-content">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
        <h3 style="font-size:14px;">DNS Records</h3>
        <button class="btn btn-sm" onclick="refreshDnsRecordsTab()">Check Again</button>
      </div>
      <div class="table-wrap">
        <table>
          <thead><tr><th>Status</th><th>Type</th><th>Name</th><th>Configured</th><th>Published</th><th>TTL</th><th>Actions</th></tr></thead>
          <tbody>${rows}</tbody>
        </table>
      </div>
      <div style="margin-top:8px;font-size:11px;color:var(--text3);text-align:right;">Last checked: ${ts}</div>
    </div>`;

  // Attach single data-copy event listener
  attachDataCopyListener('dns-records-content');
}

async function refreshDnsRecordsTab() {
  // Clear cache for all domain-related FQDNs
  const dd = window._domainDetailData;
  if (!dd) return;
  const domain = dd.domainRow.domain;
  DnsCache.clear(domain);
  DnsCache.clear('_dmarc.' + domain);
  DnsCache.clear('_mta-sts.' + domain);
  if (dd.mailDomain && dd.mailDomain.dkim_public_key_dns) {
    const sel = dd.mailDomain.dkim_selector || 'dkim';
    DnsCache.clear(sel + '._domainkey.' + domain);
  }
  loadDomainDnsRecords();
}
// --- Mail tab ---
async function loadDomainMail() {
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  const dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }
  const {domainRow, mailDomain, serverHostname} = dd;
  const domain = domainRow.domain;

  content.innerHTML = '<div class="empty-state">Loading...</div>';

  // Scenario B — No MailDomain
  if (!mailDomain) {
    content.innerHTML = `
      <div class="card">
        <h3>Mail</h3>
        <div style="margin-top:8px;font-size:13px;color:var(--text3);">
          <p>Mail service is not configured for this domain.</p>
          <p>MX, SPF, DKIM, DMARC are not shown as errors because mail is not in use.</p>
        </div>
        <div style="margin-top:12px;">
          <button class="btn btn-sm btn-primary" onclick="navigate('mail')">Enable Mail for this Domain</button>
        </div>
      </div>`;
    return;
  }

  // Scenario A — MailDomain exists

  // Fetch mailboxes/aliases count
  let mailboxCount = 0, aliasCount = 0;
  try {
    const mb = await api('/api/mail/domains/' + mailDomain.id + '/mailboxes');
    if (mb && mb.data) mailboxCount = mb.data.length;
  } catch(e) { console.error('Failed to load mailboxes', e); }
  try {
    const al = await api('/api/mail/domains/' + mailDomain.id + '/aliases');
    if (al && al.data) aliasCount = al.data.length;
  } catch(e) { console.error('Failed to load aliases', e); }

  // Fetch DNS data for Required and Recommended records
  // Query A+TXT together to get both TXT records and expected_ipv4 from NetworkService
  const rootDns = await fetchDnsForFqdn(domain, 'A,TXT');
  const rootMx = await fetchDnsForFqdn(domain, 'MX');
  const rootCaa = await fetchDnsForFqdn(domain, 'CAA');
  const expectedIpv4 = rootDns && typeof rootDns.expected_ipv4 === 'string' ? rootDns.expected_ipv4 : '';

  let dkimDns = null, dmarcDns = null, mtaStsDns = null, tlsRptDns = null, autoDiscoverDns = null;
  if (mailDomain.dkim_public_key_dns) {
    const sel = mailDomain.dkim_selector || 'dkim';
    dkimDns = await fetchDnsForFqdn(sel + '._domainkey.' + domain, 'TXT');
  }
  dmarcDns = await fetchDnsForFqdn('_dmarc.' + domain, 'TXT');
  mtaStsDns = await fetchDnsForFqdn('_mta-sts.' + domain, 'TXT');
  tlsRptDns = await fetchDnsForFqdn('_smtp._tls.' + domain, 'TXT');
  if (serverHostname) {
    autoDiscoverDns = await fetchDnsForFqdn('autodiscover.' + domain, 'CNAME,A');
  }

  // Determine expected MX
  let expectedMx = '';
  expectedMx = window.getExpectedMxTarget(mailDomain, serverHostname);

  // === Required Records ===
  const mxPubRecs = window.getDnsRecs(rootMx, 'MX');
  const mxPublished = window.formatMxPublished(mxPubRecs);
  const mxNorm = window.normalizeHostname(expectedMx);
  const mxMatch = mxPubRecs.some(r => window.normalizeHostname(r.value) === mxNorm);
  const mxStatus = window.computeRecordStatus(expectedMx, mxPublished, () => mxMatch);

  const spfRecs = window.getDnsRecs(rootDns, 'TXT').filter(r => typeof r.value === 'string' && r.value.startsWith('v=spf1'));
  const spfRecommended = 'v=spf1 mx ~all';
  const spfPublished = spfRecs.length > 0 ? spfRecs[0].value : '';
  const spfStatus = window.compareSpfRecords(spfRecommended, spfPublished, rootDns && rootDns.spf_analysis);

  const dkimRecs = dkimDns ? window.getDnsRecs(dkimDns, 'TXT') : [];
  const dkimKey = mailDomain.dkim_public_key_dns || '';
  const dkimPublished = dkimRecs.length > 0 ? dkimRecs[0].value : '';
  const dkimStatus = window.computeRecordStatus(dkimKey, dkimPublished, (a,b) => window.normalizeDnsValue(a) === window.normalizeDnsValue(b));

  const dmarcRecs = dmarcDns ? window.getDnsRecs(dmarcDns, 'TXT') : [];
  const dmarcRecommended = 'v=DMARC1; p=none;';
  const dmarcPublished = dmarcRecs.length > 0 ? dmarcRecs[0].value : '';
  const dmarcStatus = window.computeRecordStatus(dmarcRecommended, dmarcPublished, (a,b) => window.normalizeDmarcValue(a) === window.normalizeDmarcValue(b));

  // === Recommended Records ===
  // Autodiscover: standard is CNAME autodiscover.<domain> → <server_hostname>
  // or A record pointing to the server's public IP. Query both.
  const autoCnameRecs = autoDiscoverDns ? window.getDnsRecs(autoDiscoverDns, 'CNAME') : [];
  const autoARecs = autoDiscoverDns ? window.getDnsRecs(autoDiscoverDns, 'A') : [];
  let autoPublished = '', autoStatus, autoMatchFn;
  const autoRecommended = serverHostname || '';
  if (autoCnameRecs.length > 0) {
    autoPublished = 'CNAME ' + autoCnameRecs[0].value;
    const normPub = window.normalizeHostname(autoCnameRecs[0].value);
    const normExp = window.normalizeHostname(serverHostname);
    autoMatchFn = () => normPub === normExp;
  } else if (autoARecs.length > 0) {
    autoPublished = autoARecs.map(r => r.value).join(', ');
    autoMatchFn = () => expectedIpv4 && autoARecs.some(r => r.value === expectedIpv4);
  }
  autoStatus = window.computeRecordStatus(autoRecommended, autoPublished, autoMatchFn);
  if (!serverHostname) autoStatus = {status: 'N/A', cls: 'badge-info'};
  const autoHost = 'autodiscover.' + domain;

  // MTA-STS
  const mtaRecs = mtaStsDns ? window.getDnsRecs(mtaStsDns, 'TXT') : [];
  const mtaRecommended = 'v=STSv1; id=1';
  const mtaPublished = mtaRecs.length > 0 ? mtaRecs[0].value : '';
  const mtaStatus = window.computeRecordStatus(mtaRecommended, mtaPublished, (a,b) => window.normalizeDnsValue(a) === window.normalizeDnsValue(b));

  // TLS-RPT
  const tlsRecs = tlsRptDns ? window.getDnsRecs(tlsRptDns, 'TXT') : [];
  const tlsRecommended = 'v=TLSRPTv1; rua=mailto:tlsrpt@' + domain;
  const tlsPublished = tlsRecs.length > 0 ? tlsRecs[0].value : '';
  const tlsStatus = window.computeRecordStatus(tlsRecommended, tlsPublished, (a,b) => window.normalizeDnsValue(a) === window.normalizeDnsValue(b));

  // CAA
  const caaRecs = rootCaa ? window.getDnsRecs(rootCaa, 'CAA') : [];
  const caaRecommended = '0 issue "letsencrypt.org"';
  const caaPublished = caaRecs.map(r => r.value).join(', ');
  const caaHasRequired = caaRecs.some(r => window.normalizeDnsValue(r.value) === window.normalizeDnsValue(caaRecommended));
  const caaStatus = caaRecs.length > 0
    ? (caaHasRequired ? {status:'Match', cls:'badge-ok'} : {status:'Mismatch', cls:'badge-warn'})
    : {status:'Not Published', cls:'badge-err'};

  // PHP Mail status (site_id > 0 only)
  let phpMailHtml = '';
  if (domainRow.site_id && domainRow.site_id > 0) {
    try {
      const ms = await api('/api/sites/' + domainRow.site_id + '/mail-status');
      if (ms && ms.data) {
        const s = ms.data;
        const phpOk = s.enabled && s.credential_exists && s.msmtprc && s.network;
        phpMailHtml = `
          <div class="card" style="margin-top:12px;">
            <h3>PHP Mail</h3>
            <div style="margin-top:8px;font-size:13px;">
              <div>Status: ${window.statusBadge(phpOk ? 'Enabled' : s.enabled ? 'Degraded' : 'Disabled', phpOk ? 'badge-ok' : s.enabled ? 'badge-warn' : 'badge-info')}</div>
              <div style="display:grid;grid-template-columns:1fr 1fr;gap:4px;margin-top:6px;font-size:12px;color:var(--text3);">
                <div>Mail Domain: ${s.mail_domain ? '✅' : '❌'}</div>
                <div>Credentials: ${s.credential_exists ? '✅' : '❌'}</div>
                <div>msmtprc: ${s.msmtprc ? '✅' : '❌'}</div>
                <div>Network: ${s.network ? '✅' : '❌'}</div>
              </div>
            </div>
          </div>`;
      }
    } catch(e) { console.error('Failed to load PHP Mail status', e); }
  }

  const dkimSelector = mailDomain.dkim_selector || 'dkim';
  const dkimHost = dkimSelector + '._domainkey.' + domain;
  const ts = new Date().toLocaleTimeString();

  content.innerHTML = `
    <div id="mail-tab-content">
      <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:8px;">
        <h3 style="font-size:14px;">Mail Configuration</h3>
        <button class="btn btn-sm" onclick="refreshMailTab()">Check Again</button>
      </div>

      <div class="card" style="margin-bottom:12px;">
        <h3>Mail Domain: ${esc(mailDomain.domain || domain)}</h3>
        <div style="margin-top:8px;font-size:13px;">
          <div>Mode: ${window.statusBadge(mailDomain.mode, 'badge-info')}</div>
          <div>Status: ${mailDomain.enabled ? window.statusBadge('Active', 'badge-ok') : window.statusBadge('Disabled', 'badge-err')}</div>
          <div>Mailboxes: <strong>${mailboxCount}</strong> &nbsp;|&nbsp; Aliases: <strong>${aliasCount}</strong></div>
        </div>
      </div>

      <h3 style="font-size:13px;margin-bottom:8px;">Required Records</h3>
      <div class="table-wrap" style="margin-bottom:12px;">
        <table>
          <thead><tr><th>Type</th><th>Configured</th><th>Published</th><th>Status</th><th>Actions</th></tr></thead>
          <tbody>
            <tr><td>MX</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(expectedMx)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(mxPublished)}</td><td>${window.statusBadge(mxStatus.status, mxStatus.cls)}</td><td>${expectedMx ? window.copyRowButtons({host:'@', type:'MX', value:expectedMx, ttl:3600, domainName:domain}) : '—'}</td></tr>
            <tr><td>SPF</td><td style="font-family:monospace;font-size:12px;">${esc(spfRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(spfPublished)}</td><td>${window.statusBadge(spfStatus.status, spfStatus.cls)}</td><td>${window.copyRowButtons({host:'@', type:'SPF', value:spfRecommended, ttl:3600, domainName:domain})}</td></tr>
            <tr><td>DKIM</td><td style="font-family:monospace;font-size:12px;">${dkimKey ? window.fmtVal(dkimKey, 60) : '—'}</td><td style="font-family:monospace;font-size:12px;">${dkimPublished ? window.fmtVal(dkimPublished, 60) : '—'}</td><td>${window.statusBadge(dkimStatus.status, dkimStatus.cls)}</td><td>${dkimKey ? window.copyRowButtons({host:dkimHost, type:'TXT', value:dkimKey, ttl:3600, domainName:domain}) : '—'}</td></tr>
            <tr><td>DMARC</td><td style="font-family:monospace;font-size:12px;">${esc(dmarcRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(dmarcPublished)}</td><td>${window.statusBadge(dmarcStatus.status, dmarcStatus.cls)}</td><td>${window.copyRowButtons({host:'_dmarc.' + domain, type:'TXT', value:dmarcRecommended, ttl:3600, domainName:domain})}</td></tr>
          </tbody>
        </table>
      </div>

      <h3 style="font-size:13px;margin-bottom:8px;">Recommended Records</h3>
      <div class="table-wrap" style="margin-bottom:12px;">
        <table>
          <thead><tr><th>Type</th><th>Recommended</th><th>Published</th><th>Status</th><th>Actions</th></tr></thead>
          <tbody>
            <tr><td>Autodiscover</td><td style="font-family:monospace;font-size:12px;">${esc(autoRecommended ? 'CNAME → ' + autoRecommended : 'N/A')}</td><td style="font-family:monospace;font-size:12px;">${esc(autoPublished) || '—'}</td><td>${window.statusBadge(autoStatus.status, autoStatus.cls)}</td><td>${autoRecommended ? window.copyRowButtons({host:autoHost, type:'CNAME', value:autoRecommended + '.', ttl:3600, domainName:domain}) : '—'}</td></tr>
            <tr><td>MTA-STS</td><td style="font-family:monospace;font-size:12px;">${esc(mtaRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(mtaPublished)}</td><td>${window.statusBadge(mtaStatus.status, mtaStatus.cls)}</td><td>${window.copyRowButtons({host:'_mta-sts.' + domain, type:'TXT', value:mtaRecommended, ttl:3600, domainName:domain})}<br><span style="font-size:10px;color:var(--text3);">Also requires HTTPS policy at https://mta-sts.${esc(domain)}/.well-known/mta-sts.txt</span></td></tr>
            <tr><td>TLS-RPT</td><td style="font-family:monospace;font-size:12px;">${esc(tlsRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(tlsPublished)}</td><td>${window.statusBadge(tlsStatus.status, tlsStatus.cls)}</td><td>${window.copyRowButtons({host:'_smtp._tls.' + domain, type:'TXT', value:tlsRecommended, ttl:3600, domainName:domain})}<br><span style="font-size:10px;color:var(--text3);">Requires mailbox tlsrpt@${esc(domain)}</span></td></tr>
            <tr><td>CAA</td><td style="font-family:monospace;font-size:12px;">${esc(caaRecommended)}</td><td style="font-family:monospace;font-size:12px;">${window.fmtVal(caaPublished)}</td><td>${window.statusBadge(caaStatus.status, caaStatus.cls)}</td><td>${window.copyRowButtons({host:'@', type:'CAA', value:caaRecommended, ttl:3600, domainName:domain})}</td></tr>
          </tbody>
        </table>
      </div>

      <div style="text-align:right;font-size:11px;color:var(--text3);margin-bottom:8px;">Last checked: ${ts}</div>

      ${phpMailHtml}
    </div>`;

  window.attachDataCopyListener('mail-tab-content');
}

async function refreshMailTab() {
  const dd = window._domainDetailData;
  if (!dd) return;
  const domain = dd.domainRow.domain;
  const md = dd.mailDomain;
  DnsCache.clear(domain);
  if (md) {
    DnsCache.clear('_dmarc.' + domain);
    DnsCache.clear('_mta-sts.' + domain);
    DnsCache.clear('_smtp._tls.' + domain);
    DnsCache.clear('autodiscover.' + domain);
    if (md.dkim_public_key_dns) {
      const sel = md.dkim_selector || 'dkim';
      DnsCache.clear(sel + '._domainkey.' + domain);
    }
  }
  loadDomainMail();
}
// ===== SECURITY TAB =====
let _dmarcSelection = '';

// Track open evidence panels (accordion: only one at a time)
// ===== SECURITY TAB =====
let _openEvidencePanel = null;

// Shared accordion helpers
function closeEvidencePanel() {
  if (_openEvidencePanel) {
    const panel = document.getElementById(_openEvidencePanel);
    if (panel) panel.remove();
    _openEvidencePanel = null;
  }
}

function toggleEvidencePanel(panelId, anchorEl, html) {
  // Same panel clicked → close it
  if (_openEvidencePanel === panelId) {
    closeEvidencePanel();
    return;
  }
  // Different panel open → close first
  closeEvidencePanel();
  // Insert new panel after anchor
  const container = document.createElement('div');
  container.id = panelId;
  container.innerHTML = html;
  anchorEl.after(container);
  _openEvidencePanel = panelId;
  // Wire Dismiss button
  const dismissBtn = container.querySelector('[data-evidence-dismiss]');
  if (dismissBtn) dismissBtn.addEventListener('click', closeEvidencePanel);
}

function evidenceHtml(type, configured, published, dnsDetails, copyValue, steps) {
  const info = getEvidenceReason(type, configured, published, dnsDetails || '');
  const stepsHtml = steps && steps.length
    ? '<ol>' + steps.map(s => '<li>' + esc(s) + '</li>').join('') + '</ol>'
    : '<p>' + esc(info.fix) + '</p>';
  return '<div class="evidence-panel" style="background:var(--bg3);border:1px solid var(--border);border-radius:6px;padding:12px;margin:8px 0;">'
    + '<div style="margin-bottom:8px;"><strong style="font-size:13px;">' + esc(type) + '</strong></div>'
    + '<div style="display:grid;gap:6px;font-size:12px;">'
    + '<div><strong>Expected (ContainerCP):</strong><br><code style="word-break:break-all;">' + esc(configured) + '</code></div>'
    + '<div><strong>Published (public DNS):</strong><br><code style="word-break:break-all;">' + esc(published) + '</code></div>'
    + '<div><strong>Reason:</strong> ' + esc(info.reason) + '</div>'
    + '<div><strong>How to fix:</strong>' + stepsHtml + '</div>'
    + (dnsDetails ? '<div><strong>DNS Response Details:</strong><br><code style="font-size:11px;word-break:break-all;">' + esc(dnsDetails) + '</code></div>' : '')
    + '</div>'
    + '<div style="margin-top:8px;display:flex;gap:6px;">'
    + '<button class="btn btn-sm btn-primary" data-copy="' + copyValue.replace(/"/g, '&quot;').replace(/'/g, '&#39;') + '">Copy Correct Record</button>'
    + '<button class="btn btn-sm" data-evidence-dismiss="1">Dismiss</button>'
    + '</div></div>';
}

function getEvidenceReason(type, dnsDetails) {
  const reasons = {
    'DMARC_POLICY_MISMATCH': { reason: 'The DMARC policy (p=) field differs between the recommended and published values.' },
    'MTA_STS_NOT_FOUND': { reason: 'No MTA-STS TXT record found. Mail delivery without TLS may be insecure.' },
    'CAA_MISSING': { reason: 'No CAA record found. Any CA can issue certificates for your domain.' },
    'TLS_RPT_NOT_FOUND': { reason: 'No TLS-RPT record found. Delivery failure reports will not be sent.' },
  };
  return reasons[type] || { reason: 'Unexpected DNS configuration. Review the expected and published values.' };
}

function getEvidenceSteps(type, domain) {
  const shared = ['Copy the correct record using the button below.', 'Log in to your DNS provider\'s control panel.', 'Navigate to the DNS zone for ' + domain + '.'];
  const specifics = {
    'DMARC_POLICY_MISMATCH': ['Update the TXT record at _dmarc.' + domain + ' with the recommended value.', 'Wait for DNS propagation (up to 48 hours).', 'Click Check Again to verify.'],
    'MTA_STS_NOT_FOUND': ['Add a new TXT record with Host: _mta-sts and the value below.', 'Optionally create the HTTPS policy file at https://mta-sts.' + domain + '/.well-known/mta-sts.txt', 'Click Check Again to verify.'],
    'CAA_MISSING': ['Add a new CAA record with the value: 0 issue "letsencrypt.org"', 'Click Check Again to verify.'],
    'TLS_RPT_NOT_FOUND': ['Add a new TXT record with Host: _smtp._tls and the value below.', 'Ensure tlsreports@' + domain + ' exists as a mailbox or alias.', 'Click Check Again to verify.'],
  };
  const extra = specifics[type] || ['Add or update the DNS record with the correct value.', 'Click Check Again to verify.'];
  return [...shared, ...extra];
}

// Recommendation definitions (single source of truth)
function getRecDefs(domain, serverHostname, dmarcCurrent, dmarcPublished, stsPublished, caaPublished, tlsPublished, autoPublished) {
  function p(v) { return v || '(not published)'; }
  const all = {
    'dmarc': {
      type: 'DMARC_POLICY_MISMATCH',
      configured: dmarcCurrent || '',
      published: p(dmarcPublished),
      copyValue: dmarcCurrent || '',
    },
    'mta-sts': {
      type: 'MTA_STS_NOT_FOUND',
      configured: 'v=STSv1; id=1',
      published: p(stsPublished),
      copyValue: 'v=STSv1; id=1',
    },
    'caa': {
      type: 'CAA_MISSING',
      configured: '0 issue "letsencrypt.org"',
      published: p(caaPublished),
      copyValue: '0 issue "letsencrypt.org"',
    },
    'tls-rpt': {
      type: 'TLS_RPT_NOT_FOUND',
      configured: 'v=TLSRPTv1; rua=mailto:tlsreports@' + domain,
      published: p(tlsPublished),
      copyValue: 'v=TLSRPTv1; rua=mailto:tlsreports@' + domain,
    },
    'autodiscover': {
      type: 'AUTODISCOVER_NOT_FOUND',
      configured: serverHostname || 'N/A',
      published: p(autoPublished),
      copyValue: 'autodiscover.' + domain + '. 3600 IN CNAME ' + (serverHostname || 'N/A') + '.',
    },
  };
  return all;
}

function loadDomainSecurity() {
  const content = document.getElementById('domain-tab-content');
  if (!content) return;
  const dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }
  const {domainRow, mailDomain, serverHostname} = dd;
  const domain = domainRow.domain;
  closeEvidencePanel();

  (async () => {
    // Fetch live DNS for DMARC + all recommendations
    // Each fetch has its own try/catch so one failure doesn't block the tab
    let dmarcPublished = '', stsPublished = '', caaPublished = '', tlsPublished = '', autoPublished = '';
    try {
      const dmarcDns = await fetchDnsForFqdn('_dmarc.' + domain, 'TXT');
      if (dmarcDns) { const r = window.getDnsRecs(dmarcDns, 'TXT'); if (r.length) dmarcPublished = r[0].value; }
    } catch(e) { console.error('Failed to fetch _dmarc.' + domain, e); }
    try {
      const d = await fetchDnsForFqdn('_mta-sts.' + domain, 'TXT');
      if (d) { const r = window.getDnsRecs(d, 'TXT'); if (r.length) stsPublished = r[0].value; }
    } catch(e) { console.error('Failed to fetch _mta-sts.' + domain, e); }
    try {
      const d = await fetchDnsForFqdn(domain, 'CAA');
      if (d) { const r = window.getDnsRecs(d, 'CAA'); if (r.length) caaPublished = r.map(x => x.value).join(', '); }
    } catch(e) { console.error('Failed to fetch CAA for ' + domain, e); }
    try {
      const d = await fetchDnsForFqdn('_smtp._tls.' + domain, 'TXT');
      if (d) { const r = window.getDnsRecs(d, 'TXT'); if (r.length) tlsPublished = r[0].value; }
    } catch(e) { console.error('Failed to fetch _smtp._tls.' + domain, e); }
    try {
      const d = await fetchDnsForFqdn('autodiscover.' + domain, 'CNAME,A');
      if (d) {
        const c = window.getDnsRecs(d, 'CNAME');
        if (c.length) autoPublished = 'CNAME ' + c[0].value;
        else { const a = window.getDnsRecs(d, 'A'); if (a.length) autoPublished = 'A ' + a[0].value; }
      }
    } catch(e) { console.error('Failed to fetch autodiscover.' + domain, e); }

    const dmarcCurrent = _dmarcSelection || 'v=DMARC1; p=none;';
    const recDefs = getRecDefs(domain, serverHostname, dmarcCurrent, dmarcPublished, stsPublished, caaPublished, tlsPublished, autoPublished);
    const dmarcHost = '_dmarc.' + domain;
    const dmarcFqdn = dmarcHost + '.';
    const dmarcFull = dmarcFqdn + ' 3600 IN TXT "' + dmarcCurrent + '"';
    const hasRua = dmarcCurrent.includes('rua=');
    const copyWithRua = hasRua ? dmarcCurrent : dmarcCurrent.replace(/;?\s*$/, '; rua=mailto:dmarc@' + domain);

    // Build entire HTML in one string — single innerHTML assignment
    var html = '';

    // Header
    html += '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:12px;">'
      + '<h3 style="font-size:14px;">DMARC Policy</h3>'
      + '<button class="btn btn-sm" data-security-check-again="1">Check Again</button></div>'
      + '<div style="margin-bottom:12px;font-size:12px;color:var(--text3);">'
      + (dmarcPublished ? 'Current in DNS: <code>' + esc(dmarcPublished) + '</code>' : 'No DMARC record found in DNS') + '</div>';

    // DMARC policy cards
    html += '<div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px;margin-bottom:16px;">'
      + '<div class="card" style="cursor:pointer;' + (_dmarcSelection && _dmarcSelection.includes('p=none') ? 'border-color:var(--primary);' : '') + '" data-dmarc-policy="v=DMARC1; p=none;">'
      + '<h3>Monitor</h3><div style="margin-top:8px;font-size:12px;font-family:monospace;">v=DMARC1; p=none;</div>'
      + '<div style="margin-top:4px;font-size:11px;color:var(--text3);">No action taken on failing messages</div></div>'
      + '<div class="card" style="cursor:pointer;' + (_dmarcSelection && _dmarcSelection.includes('p=quarantine') ? 'border-color:var(--primary);' : '') + '" data-dmarc-policy="v=DMARC1; p=quarantine; rua=mailto:dmarc@' + domain + '">'
      + '<h3>Quarantine</h3><div style="margin-top:8px;font-size:12px;font-family:monospace;">v=DMARC1; p=quarantine;</div>'
      + '<div style="margin-top:4px;font-size:11px;color:var(--text3);">Tag suspicious emails as spam</div></div>'
      + '<div class="card" style="cursor:pointer;' + (_dmarcSelection && _dmarcSelection.includes('p=reject') ? 'border-color:var(--primary);' : '') + '" data-dmarc-policy="v=DMARC1; p=reject; rua=mailto:dmarc@' + domain + '">'
      + '<h3>Reject</h3><div style="margin-top:8px;font-size:12px;font-family:monospace;">v=DMARC1; p=reject;</div>'
      + '<div style="margin-top:4px;font-size:11px;color:var(--text3);">Block failing emails entirely</div></div></div>';

    // DMARC Preview + Comparison
    if (_dmarcSelection) {
      var cmp = '';
      if (dmarcPublished) {
        var r = window.compareDmarcRecords(_dmarcSelection, dmarcPublished);
        var dmarcDef = recDefs['dmarc'];
        cmp = '<div class="card" style="margin-top:8px;" data-security-record="dmarc"'
          + ' data-evidence-configured="' + escAttr(dmarcDef ? dmarcDef.configured : '') + '"'
          + ' data-evidence-published="' + escAttr(dmarcDef ? dmarcDef.published : '') + '"'
          + ' data-evidence-copy="' + escAttr(dmarcDef ? dmarcDef.copyValue : '') + '">'
          + '<div style="font-size:12px;">Comparison</div>'
          + '<div style="margin-top:6px;display:grid;grid-template-columns:1fr 1fr;gap:8px;font-size:12px;">'
          + '<div><strong>Recommended:</strong><br><code style="font-size:11px;word-break:break-all;">' + esc(_dmarcSelection) + '</code></div>'
          + '<div><strong>Published:</strong><br><code style="font-size:11px;word-break:break-all;">' + esc(dmarcPublished) + '</code></div></div>'
          + '<div style="margin-top:6px;">' + window.statusBadge(r.status, r.cls)
          + (r.status === 'Mismatch' ? '<button class="btn btn-sm" data-security-why="1" data-evidence-type="DMARC_POLICY_MISMATCH" data-security-record-key="dmarc">Why?</button>' : '') + '</div></div>';
      }
      html += '<div class="card"><h3>Your DMARC Record Preview</h3>'
        + '<div style="margin-top:8px;font-size:12px;font-family:monospace;word-break:break-all;background:var(--bg3);padding:8px;border-radius:4px;">' + esc(dmarcFull) + '</div>'
        + '<div style="margin-top:8px;display:flex;gap:6px;flex-wrap:wrap;">'
        + '<button class="btn btn-sm btn-primary" data-copy="' + escAttr(_dmarcSelection) + '">Copy Record</button>'
        + '<button class="btn btn-sm" data-copy="' + escAttr(copyWithRua) + '">Copy with RUA</button>'
        + '<button class="btn btn-sm" data-copy="' + escAttr(dmarcFull) + '">Copy Full Record</button></div>'
        + '<div style="margin-top:8px;font-size:11px;color:var(--yellow);">⚠️ Start with p=none to monitor, then escalate to quarantine after 1-2 weeks.</div>'
        + cmp + '</div>';
    }

    // Additional recommendations
    function secCard(key, title, host, type, val, btns) {
      var def = recDefs[key];
      if (!def) return '';
      return '<div class="card" data-security-record="' + key + '"'
        + ' data-evidence-configured="' + escAttr(def.configured) + '"'
        + ' data-evidence-published="' + escAttr(def.published) + '"'
        + ' data-evidence-copy="' + escAttr(def.copyValue) + '">'
        + '<h3>' + title + '</h3>'
        + '<div style="margin-top:8px;font-size:12px;"><div>' + esc(val) + '</div>'
        + '<div style="margin-top:6px;font-family:monospace;font-size:11px;">Host: ' + esc(host) + '<br>Type: ' + esc(type) + '<br>Value: ' + esc(def.configured) + '</div></div>'
        + '<div style="margin-top:8px;display:flex;gap:6px;flex-wrap:wrap;">' + btns + '</div></div>';
    }
    function recWhy(key) {
      var def = recDefs[key];
      if (!def) return '';
      return '<button class="btn btn-sm" data-security-why="1" data-evidence-type="' + escAttr(def.type) + '" data-security-record-key="' + escAttr(key) + '">Why?</button>';
    }
    function copyBtnHtml(v) { return '<button class="btn btn-sm" data-copy="' + escAttr(v) + '">Copy Record</button>'; }

    html += '<h3 style="font-size:14px;margin:16px 0 8px;">Additional Recommendations</h3>'
      + '<div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(300px,1fr));gap:12px;">'
      + secCard('mta-sts', 'MTA-STS', '_mta-sts', 'TXT', 'Ensures TLS is used for mail delivery (RFC 8461).', copyBtnHtml('v=STSv1; id=1') + recWhy('mta-sts'))
      + secCard('caa', 'CAA', '@', 'CAA', 'Certification Authority Authorization lets you specify which CAs can issue certificates.', copyBtnHtml('0 issue "letsencrypt.org"') + recWhy('caa'))
      + secCard('tls-rpt', 'TLS-RPT', '_smtp._tls', 'TXT', 'TLS-RPT sends delivery failure reports to your email.', copyBtnHtml(recDefs['tls-rpt'].configured) + recWhy('tls-rpt'))
      + secCard('autodiscover', 'Autodiscover', 'autodiscover', 'CNAME', 'Autodiscover configures email clients automatically.', copyBtnHtml(recDefs['autodiscover'].copyValue) + (serverHostname ? recWhy('autodiscover') : ''))
      + '</div>';

    // Single innerHTML assignment — all elements inside #security-tab-content
    content.innerHTML = '<div id="security-tab-content">' + html + '</div>';

    // Unified event delegation
    attachSecurityDelegation();
  })().catch(function(err) {
    console.error('Security tab load failed', err);
    var el = document.getElementById('domain-tab-content');
    if (el) el.innerHTML = '<div class="empty-state">Failed to load Security tab</div>';
  });
}

// Single event delegation for Security tab
function attachSecurityDelegation() {
  var sec = document.getElementById('security-tab-content');
  if (!sec) return;
  sec.addEventListener('click', function(e) {
    // Copy buttons
    var copyBtn = e.target.closest('[data-copy]');
    if (copyBtn) {
      copyText(copyBtn.getAttribute('data-copy'), 'Copied');
      return;
    }
    // Dismiss evidence
    if (e.target.closest('[data-evidence-dismiss]')) {
      closeEvidencePanel();
      return;
    }
    // DMARC policy selection
    var policyCard = e.target.closest('[data-dmarc-policy]');
    if (policyCard) {
      _dmarcSelection = policyCard.getAttribute('data-dmarc-policy');
      closeEvidencePanel();
      loadDomainSecurity();
      return;
    }
    // Check Again
    if (e.target.closest('[data-security-check-again]')) {
      closeEvidencePanel();
      var dd = window._domainDetailData;
      if (dd) {
        var d = dd.domainRow.domain;
        DnsCache.clear(d);
        DnsCache.clear('_dmarc.' + d);
        DnsCache.clear('_mta-sts.' + d);
        DnsCache.clear('_smtp._tls.' + d);
        DnsCache.clear('autodiscover.' + d);
      }
      loadDomainSecurity();
      return;
    }
    // Why? evidence
    var whyBtn = e.target.closest('[data-security-why]');
    if (!whyBtn) return;
    var key = whyBtn.getAttribute('data-security-record-key');
    var type = whyBtn.getAttribute('data-evidence-type');
    var dd = window._domainDetailData;
    if (!dd) return;
    var card = sec.querySelector('[data-security-record="' + key + '"]');
    if (!card) return;
    var configured = card.getAttribute('data-evidence-configured') || '';
    var published = card.getAttribute('data-evidence-published') || '(not published)';
    var copyValue = card.getAttribute('data-evidence-copy') || '';
    var steps = getEvidenceSteps(type, dd.domainRow.domain);
    var html = evidenceHtml(type, configured, published, '', copyValue, steps);
    toggleEvidencePanel('ev-' + key, card, html);
  });
}

window.selectDmarcPolicy = function(value) {
  _dmarcSelection = value;
  closeEvidencePanel();
  loadDomainSecurity();
};

function escAttr(s) {
  return String(s == null ? '' : s).replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

function getEvidenceSteps(type, domain) {
  const shared = ['Copy the correct record using the button below.', 'Log in to your DNS provider\'s control panel.', 'Navigate to the DNS zone for ' + domain + '.'];
  const map = {
    'DMARC_POLICY_MISMATCH': ['Update the TXT record at _dmarc.' + domain + ' with the recommended value.', 'Wait for DNS propagation (up to 48 hours).', 'Click Check Again to verify.'],
    'MTA_STS_NOT_FOUND': ['Add a new TXT record with Host: _mta-sts and the value below.', 'Optionally create the HTTPS policy file at https://mta-sts.' + domain + '/.well-known/mta-sts.txt', 'Click Check Again to verify.'],
    'CAA_MISSING': ['Add a new CAA record with the value: 0 issue "letsencrypt.org"', 'Click Check Again to verify.'],
    'TLS_RPT_NOT_FOUND': ['Add a new TXT record with Host: _smtp._tls and the value below.', 'Ensure tlsreports@' + domain + ' exists as a mailbox or alias.', 'Click Check Again to verify.'],
  };
  return [...shared, ...(map[type] || ['Add or update the DNS record with the correct value.', 'Click Check Again to verify.'])];
}
function loadDomainHealth() {
  var content = document.getElementById('domain-tab-content');
  if (!content) return;
  var dd = window._domainDetailData;
  if (!dd) { content.innerHTML = '<div class="empty-state">No data</div>'; return; }

  content.innerHTML = '<div class="empty-state">Computing health score...</div>';

  var domain = dd.domainRow.domain;

  void(dd);
  var domain = dd.domainRow.domain;
  window.HealthCache.load(domain, dd.domainRow, dd.mailDomain, dd.serverHostname, {force: true}).then(function(result) {

    if (!result || result.score == null) {
      content.innerHTML = '<div class="empty-state">No checks applicable for this domain.</div>';
      return;
    }

    var ts = result.computed_at ? new Date(result.computed_at).toLocaleTimeString() : new Date().toLocaleTimeString();

    if (result.score == null) {
      content.innerHTML = '<div class="empty-state">No checks applicable for this domain.</div>';
      return;
    }

    // Build breakdown rows with Configured + Published columns
    var rows = '';
    for (var i = 0; i < result.breakdown.length; i++) {
      var c = result.breakdown[i];
      var clsLabel = c.cls === 'req' ? 'Required' : c.cls === 'rec' ? 'Recommended' : 'Informational';
      var clsBadge = c.cls === 'req' ? 'badge-err' : c.cls === 'rec' ? 'badge-warn' : 'badge-info';
      var earned = c.earned !== null && c.earned !== undefined ? c.earned : '—';
      var scoreStr = c.weight > 0 ? earned + '/' + c.weight : '—';
      rows += '<tr>'
        + '<td><span class="badge ' + clsBadge + '">' + clsLabel + '</span></td>'
        + '<td>' + esc(c.label) + '</td>'
        + '<td>' + window.statusBadge(c.status || 'N/A', c.status === 'Match' || c.status === 'Active' || c.status === 'Running' ? 'badge-ok' : c.status === 'N/A' ? 'badge-info' : c.status === 'Unexpected' || c.status === 'Expiring' || c.status === 'Starting' ? 'badge-warn' : 'badge-err') + '</td>'
        + '<td>' + c.weight + '</td>'
        + '<td style="font-family:monospace;font-size:12px;">' + esc(typeof c.configured === 'string' ? c.configured.substring(0, 30) : '') + '</td>'
        + '<td style="font-family:monospace;font-size:12px;">' + esc(typeof c.published === 'string' ? c.published.substring(0, 30) : '') + '</td>'
        + '<td>' + earned + '</td>'
        + '<td>' + scoreStr + '</td>'
        + '</tr>';
    }

    var gradeColor = result.grade === 'Excellent' ? 'badge-ok' : result.grade === 'Good' ? 'badge-info' : result.grade === 'Fair' ? 'badge-warn' : 'badge-err';

    content.innerHTML = '<div id="health-tab-content">'
      + '<div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:16px;">'
      + '<h3 style="font-size:14px;">Health Score</h3>'
      + '<button class="btn btn-sm" data-health-check-again="1">Check Again</button></div>'

      + '<div class="card" style="text-align:center;margin-bottom:16px;">'
      + '<div style="font-size:48px;font-weight:700;">' + result.score + '</div>'
      + '<div style="font-size:14px;">/ 100</div>'
      + '<div style="margin-top:8px;"><span class="badge ' + gradeColor + '" style="font-size:14px;padding:4px 16px;">' + esc(result.grade) + '</span></div>'
      + '<div style="margin-top:8px;font-size:12px;color:var(--text3);">' + result.earnedWeight + ' of ' + result.applicableWeight + ' weighted points</div>'
      + '<div style="margin-top:4px;font-size:11px;color:var(--text3);">Last checked: ' + ts + '</div></div>'

      + '<h3 style="font-size:13px;margin-bottom:8px;">Check Details</h3>'
      + '<div class="table-wrap">'
      + '<table><thead><tr><th>Class</th><th>Check</th><th>Status</th><th>Wt</th><th>Configured</th><th>Published</th><th>Got</th><th>Score</th></tr></thead>'
      + '<tbody>' + rows + '</tbody></table></div></div>';

    attachHealthDelegation();
  }).catch(function(err) {
    console.error('Health tab failed', err);
    content.innerHTML = '<div class="empty-state">Failed to load Health tab</div>';
  });
}

function attachHealthDelegation() {
  var root = document.getElementById('health-tab-content');
  if (!root) return;
  root.addEventListener('click', function(e) {
    if (e.target.closest('[data-health-check-again]')) {
      var dd = window._domainDetailData;
      if (dd) {
        var d = dd.domainRow.domain;
        // Clear DNS cache for all health-related FQDNs
        DnsCache.clear(d);
        DnsCache.clear('_dmarc.' + d);
        DnsCache.clear('_mta-sts.' + d);
        DnsCache.clear('_smtp._tls.' + d);
        DnsCache.clear('autodiscover.' + d);
        if (dd.mailDomain && dd.mailDomain.dkim_public_key_dns) {
          var sel = dd.mailDomain.dkim_selector || 'dkim';
          DnsCache.clear(sel + '._domainkey.' + d);
        }
        // Invalidate HealthCache so next load is fresh
        window.HealthCache.invalidate(d);
      }
      loadDomainHealth();
    }
  });
}

function copyText(text, msg) {
  navigator.clipboard.writeText(text).then(() => toast(msg || 'Copied', 'success')).catch(() => toast('Copy failed', 'error'));
}

async function removeDomain(domain) {
  if (!confirm('Remove domain '+domain+'?')) return;
  try { const res = await apiPost('/api/domains/remove',{domain}); if(res.success){toast('Domain removed','success');loadDomains($('page'));}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
}

/* ===== WEBMAIL ===== */
async function loadWebmail(p) {
  p.innerHTML = `<div class="page-header"><h1>Webmail</h1></div>
    <div class="card">
      <div class="card-header"><h3>SnappyMail Webmail</h3></div>
      <div style="padding:16px;text-align:center;">
        <p style="margin-bottom:16px;color:var(--text2);">Access your mail via SnappyMail webmail client.</p>
        <a href="/webmail/" target="_blank" class="btn btn-primary">Open Webmail</a>
        <p style="margin-top:12px;font-size:12px;color:var(--text2);">Login with your full email address and mailbox password.</p>
      </div>
    </div>`;
}

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
        const filtered = domains.filter(r => !searchTerm || r.domain.includes(searchTerm));
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
      {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeMailbox(${r.id})">&#10005;</button>`}
    ], mailboxes, 'No mailboxes');

    // Aliases section
    html += `<div class="page-header" style="margin-top:16px;"><h3>Aliases</h3><div class="page-actions"><button class="btn btn-primary btn-sm" onclick="showCreateAlias(${id})">+ Add Alias</button></div></div>`;
    html += buildTable([
      {label:'Source',html:r=>esc(r.source)+'@'+esc(domain.domain)},
      {label:'Destination',html:r=>esc(r.destination)},
      {label:'Enabled',html:r=>r.enabled ? '<span class="badge badge-ok">Yes</span>' : '<span class="badge badge-err">No</span>'},
      {label:'Actions',html:r=>`<button class="btn-icon" style="color:var(--red)" onclick="removeAlias(${r.id})">&#10005;</button>`}
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

async function removeMailbox(id) {
  if (!confirm('Remove mailbox?')) return;
  try {
    const res = await apiPost('/api/mail/mailboxes/' + id, {}, 'DELETE');
    if (res.success) { toast('Mailbox removed', 'success'); navigate('mail-domain', currentParams); }
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

async function removeAlias(id) {
  if (!confirm('Remove alias?')) return;
  try {
    const res = await apiPost('/api/mail/aliases/' + id, {}, 'DELETE');
    if (res.success) { toast('Alias removed', 'success'); navigate('mail-domain', currentParams); }
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
  if (p._loading) return;
  p._loading = true;

  try {
    const [proxyData, healthData] = await Promise.all([
      api('/api/proxy'),
      api('/api/proxy/health')
    ]);

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
    window.renderTable = () => {
      const tbl = $('proxy-table');
      if (!tbl) return;
      const rows = (proxyData.data||[]).filter(r=>!searchTerm||r.domain.includes(searchTerm));
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
  if (p && !p._loading) loadProxy(p);
}

async function removeProxy(domain) {
  if (!confirm('Remove proxy entry for '+domain+'?')) return;
  try { const res = await apiPost('/api/proxy/remove',{domain}); if(res.success){toast('Proxy removed','success');window.renderTable&&renderTable();}else toast('Error: '+res.error,'error'); } catch(e){toast('Network error','error');}
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
/* ===== MIGRATION (myVestaCP Import) ===== */
async function loadMigration(p) {
  try {
    let html = `<div class="page-header"><h1>Migration</h1><div class="page-actions"></div></div>`;
    html += `<div class="card"><div class="card-header"><h3>Import from myVestaCP</h3></div>
      <div style="padding:16px;">
      <p style="margin-bottom:16px;color:var(--text2);">Analyze a myVestaCP backup archive before importing into ContainerCP.</p>`;

    // Load available backups
    html += `<div class="form-group"><label>Backup file</label>
      <select id="migrate-backup" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text);">
      <option value="">Loading backups...</option></select></div>`;

    html += `<div class="form-group"><label>Domain</label>
      <input type="text" id="migrate-domain" placeholder="example.com" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text);"></div>`;

    html += `<div class="form-group"><label>Owner</label>
      <input type="text" id="migrate-owner" value="admin" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text);"></div>`;

    html += `<details style="margin-bottom:12px;"><summary style="cursor:pointer;font-size:13px;color:var(--text2);">Advanced options</summary>
      <div class="form-group" style="margin-top:8px;"><label>Database (override)</label>
        <input type="text" id="migrate-database" placeholder="Auto-detect" style="width:100%;padding:8px;border:1px solid var(--border);border-radius:6px;background:var(--input-bg);color:var(--text);"></div>
      <div style="display:flex;gap:16px;margin-top:8px;">
        <label><input type="checkbox" id="migrate-skip-db"> Skip database import</label>
        <label><input type="checkbox" id="migrate-keep-staging"> Keep staging files</label>
      </div>
    </details>`;

    html += `<button class="btn btn-primary" id="migrate-analyze-btn" onclick="analyzeBackup()">Analyze backup</button>
      <div id="migrate-result" style="margin-top:16px;"></div>
      </div></div>`;

    p.innerHTML = html;

    // Load backup list
    try {
      const backups = await api('/api/migration/vesta/backups');
      const sel = document.getElementById('migrate-backup');
      if (backups.data && backups.data.length > 0) {
        sel.innerHTML = '<option value="">Select a backup...</option>';
        backups.data.forEach(b => {
          sel.innerHTML += `<option value="${esc(b.name)}">${esc(b.name)} (${(b.size/1024/1024).toFixed(1)} MB)</option>`;
        });
      } else {
        sel.innerHTML = '<option value="">No backups found</option>';
      }
    } catch(e) {
      document.getElementById('migrate-backup').innerHTML = '<option value="">Failed to load backups</option>';
    }
  } catch(e) {
    p.innerHTML = '<div class="empty-state">Failed to load migration page</div>';
  }
}

async function analyzeBackup() {
  const btn = document.getElementById('migrate-analyze-btn');
  const resultDiv = document.getElementById('migrate-result');
  btn.disabled = true;
  btn.textContent = 'Analyzing...';
  resultDiv.innerHTML = '';

  try {
    const backup = document.getElementById('migrate-backup').value;
    const domain = document.getElementById('migrate-domain').value.trim();
    const owner = document.getElementById('migrate-owner').value.trim();
    const database = document.getElementById('migrate-database').value.trim();
    const skipDb = document.getElementById('migrate-skip-db').checked;
    const keepStaging = document.getElementById('migrate-keep-staging').checked;

    if (!backup || !domain || !owner) {
      resultDiv.innerHTML = '<div class="alert alert-error">Backup, domain and owner are required.</div>';
      btn.disabled = false; btn.textContent = 'Analyze backup';
      return;
    }

    const body = { backup, domain, owner };
    if (database) body.database = database;
    if (skipDb) body.skip_db = true;
    if (keepStaging) body.keep_staging = true;

    const res = await apiPost('/api/migration/vesta/inspect', body);
    const d = res.data;

    let html = '<div class="card" style="margin-top:12px;"><div class="card-header"><h3>Analysis Result</h3></div><div style="padding:12px;font-size:13px;">';

    if (d.errors && d.errors.length > 0) {
      html += '<div style="color:var(--red);margin-bottom:8px;"><strong>Errors:</strong><ul>';
      d.errors.forEach(e => { html += '<li>' + esc(e) + '</li>'; });
      html += '</ul></div>';
    }

    html += '<table style="width:100%;border-collapse:collapse;">';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Domain found</td><td>' + (d.domain_found ? '<span class="badge badge-ok">Yes</span>' : '<span class="badge badge-info">No</span>') + '</td></tr>';

    if (d.domain_found) {
      html += '<tr><td style="padding:4px 8px;color:var(--text2);">Web archive</td><td>' + esc(d.web_archive_path) + '</td></tr>';
      html += '<tr><td style="padding:4px 8px;color:var(--text2);">Web root</td><td>' + esc(d.web_root_type) + '</td></tr>';
      html += '<tr><td style="padding:4px 8px;color:var(--text2);">WordPress config</td><td>' + (d.wp_config_found ? '<span class="badge badge-ok">Found</span>' : '<span class="badge badge-info">Not found</span>') + '</td></tr>';

      if (d.wp_config_found) {
        if (d.wp_db_ambiguous) {
          html += '<tr><td style="padding:4px 8px;color:var(--text2);">DB_NAME</td><td><span class="badge badge-warning">Ambiguous</span> Use database override</td></tr>';
        } else if (d.wp_config_parsed) {
          html += '<tr><td style="padding:4px 8px;color:var(--text2);">DB_NAME</td><td>' + esc(d.wp_db_name) + '</td></tr>';
          html += '<tr><td style="padding:4px 8px;color:var(--text2);">DB_USER</td><td>' + esc(d.wp_db_user) + '</td></tr>';
        }
      }

      html += '<tr><td style="padding:4px 8px;color:var(--text2);">SQL dump</td><td>' + (d.db_dump_found ? '<span class="badge badge-ok">Found (' + esc(d.db_type) + ')</span>' : '<span class="badge badge-info">Not found</span>') + '</td></tr>';

      if (d.all_databases && d.all_databases.length > 0) {
        html += '<tr><td style="padding:4px 8px;color:var(--text2);">Available DBs</td><td>' + d.all_databases.map(esc).join(', ') + '</td></tr>';
      }
    }

    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Site exists</td><td>' + (d.site_exists ? '<span class="badge badge-error">Yes</span>' : '<span class="badge badge-ok">No</span>') + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Disk available</td><td>' + (d.available_disk_mb > 0 ? d.available_disk_mb + ' MB' : 'Unknown') + '</td></tr>';
    html += '</table>';

    if (d.warnings && d.warnings.length > 0) {
      html += '<div style="margin-top:8px;"><strong>Warnings:</strong><ul>';
      d.warnings.forEach(w => { html += '<li style="color:var(--orange);">' + esc(w) + '</li>'; });
      html += '</ul></div>';
    }

    html += '</div></div>';

    // Decide which buttons to show based on state machine
    const hasErrors = d.errors && d.errors.length > 0;

    if (!hasErrors) {
      if (!d.site_exists) {
        // Stage 0: Create site
        html += `<div class="card" style="margin-top:12px;border-color:var(--blue);">
          <div class="card-header"><h3>Migration Stage 0 — Backup Analyzed</h3></div>
          <div style="padding:12px;font-size:13px;">
            <p style="margin-bottom:12px;">Backup analyzed. Site not yet created.</p>
            <button class="btn btn-primary" onclick="importMigrationSite()" id="migrate-import-btn">Create site</button>
            <div id="migrate-import-result" style="margin-top:12px;"></div>
          </div></div>`;

      } else if (d.migration_completed) {
        // Stage 3: Completed
        html += `<div class="card" style="margin-top:12px;border-color:var(--green);">
          <div class="card-header"><h3 style="color:var(--green);">Migration Completed</h3></div>
          <div style="padding:12px;font-size:13px;">
            <p>All migration stages completed.</p>
          </div></div>`;

      } else if (d.site_exists && d.migration_marker_found) {
        // Migration in progress — show current stage
        const stageLabels = {1: 'Site created', 2: 'Files imported'};
        const stageDesc = stageLabels[d.migration_stage] || ('Stage ' + d.migration_stage);
        html += `<div class="card" style="margin-top:12px;border-color:var(--blue);">
          <div class="card-header"><h3 style="color:var(--blue);">Migration: ${esc(stageDesc)}</h3></div>
          <div style="padding:12px;font-size:13px;">
            <table style="width:100%;border-collapse:collapse;">
              <tr><td style="padding:4px 8px;color:var(--text2);">Site ID</td><td>${d.migration_site_id || '?'}</td></tr>
              <tr><td style="padding:4px 8px;color:var(--text2);">Files</td><td>${d.files_status === 'imported' ? '<span class="badge badge-ok">Imported</span>' : '<span class="badge badge-info">' + esc(d.files_status || 'Pending') + '</span>'}</td></tr>
              <tr><td style="padding:4px 8px;color:var(--text2);">SQL</td><td>${d.sql_status === 'imported' ? '<span class="badge badge-ok">Imported</span>' : '<span class="badge badge-info">' + esc(d.sql_status || 'Pending') + '</span>'}</td></tr>
            </table>`;

        // Import files button (stage 1 → 2)
        if (d.can_import_files) {
          html += `<button class="btn btn-primary" style="margin-top:12px;" onclick="importMigrationFiles()">Import files</button>
            <div id="migrate-files-result" style="margin-top:12px;"></div>`;
        }

        // Import SQL button (stage 2 → 3)
        if (d.can_import_sql) {
          html += `<button class="btn btn-primary" style="margin-top:12px;" onclick="importMigrationSql()">Import SQL</button>
            <div id="migrate-sql-result" style="margin-top:12px;"></div>`;
        }

        html += `</div></div>`;

      } else if (d.site_exists && d.migration_marker_found && d.marker_error) {
        html += `<div class="alert alert-warning" style="margin-top:12px;">Marker error: ${esc(d.marker_error)}</div>`;
      } else if (d.site_exists) {
        html += `<div class="alert alert-info" style="margin-top:12px;">Existing site is not an active myVesta migration.</div>`;
      }
    }

    resultDiv.innerHTML = html;
  } catch(e) {
    resultDiv.innerHTML = '<div class="alert alert-error">Analysis failed: ' + esc(e.message || 'Unknown error') + '</div>';
  }

  btn.disabled = false;
  btn.textContent = 'Analyze backup';
}

async function importMigrationSite() {
  const btn = document.getElementById('migrate-import-btn');
  const resultDiv = document.getElementById('migrate-import-result');
  if (!btn || !resultDiv) return;

  btn.disabled = true;
  btn.textContent = 'Importing...';
  resultDiv.innerHTML = '';

  try {
    const backup = document.getElementById('migrate-backup').value;
    const domain = document.getElementById('migrate-domain').value.trim();
    const owner = document.getElementById('migrate-owner').value.trim();
    const database = document.getElementById('migrate-database').value.trim();
    const skipDb = document.getElementById('migrate-skip-db').checked;
    const keepStaging = document.getElementById('migrate-keep-staging').checked;

    const body = { backup, domain, owner };
    if (database) body.database = database;
    if (skipDb) body.skip_db = true;
    if (keepStaging) body.keep_staging = true;

    const res = await apiPost('/api/migration/vesta/create-site', body);
    const d = res.data;

    let html = '<div class="card" style="margin-top:12px;border-color:var(--green);"><div class="card-header"><h3 style="color:var(--green);">Stage 1 Completed</h3></div><div style="padding:12px;font-size:13px;">';
    html += '<p style="margin-bottom:12px;">' + esc(d.message) + '</p>';
    html += '<table style="width:100%;border-collapse:collapse;">';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Site ID</td><td><strong>' + d.site_id + '</strong></td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Domain</td><td>' + esc(d.domain) + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Database</td><td>' + esc(d.database_name) + ' / ' + esc(d.database_user) + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Document root</td><td>' + esc(d.document_root) + '</td></tr>';
    html += '</table>';

    html += '<div style="margin-top:12px;"><strong>Status:</strong><ul style="margin-top:4px;">';
    if (d.status) {
      for (const [key, val] of Object.entries(d.status)) {
        const icon = val === 'created' ? '✅' : '⏳';
        html += '<li>' + icon + ' ' + esc(key) + ': ' + esc(val) + '</li>';
      }
    }
    html += '</ul></div>';

    html += '<p style="margin-top:12px;font-size:12px;color:var(--text2);">Run Analyze backup again to continue with Stage 2.</p>';
    html += '</div></div>';
    resultDiv.innerHTML = html;
  } catch(e) {
    resultDiv.innerHTML = '<div class="alert alert-error">Import failed: ' + esc(e.message || 'Unknown error') + '</div>';
  }

  btn.disabled = false;
  btn.textContent = 'Import site';
}

async function importMigrationFiles() {
  const resultDiv = document.getElementById('migrate-files-result');
  if (!resultDiv) return;
  resultDiv.innerHTML = 'Importing files...';

  try {
    const backup = document.getElementById('migrate-backup').value;
    const domain = document.getElementById('migrate-domain').value.trim();
    const owner = document.getElementById('migrate-owner').value.trim();
    const keepStaging = document.getElementById('migrate-keep-staging').checked;

    const body = { backup, domain, owner };
    if (keepStaging) body.keep_staging = true;

    const res = await apiPost('/api/migration/vesta/import-files', body);
    const d = res.data;

    let html = '<div class="card" style="margin-top:12px;border-color:var(--green);"><div class="card-header"><h3 style="color:var(--green);">Stage 2 Completed</h3></div><div style="padding:12px;font-size:13px;">';
    html += '<p style="margin-bottom:12px;">' + esc(d.message) + '</p>';
    html += '<table style="width:100%;border-collapse:collapse;">';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Web root</td><td>' + esc(d.web_root) + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Destination</td><td>' + esc(d.destination) + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Files</td><td>' + (d.files_count || 'unknown') + '</td></tr>';
    html += '<tr><td style="padding:4px 8px;color:var(--text2);">Size</td><td>' + (d.bytes_copied ? Math.round(d.bytes_copied/1024) + ' KB' : 'unknown') + '</td></tr>';
    html += '</table>';

    html += '<div style="margin-top:12px;"><strong>Status:</strong><ul style="margin-top:4px;">';
    if (d.status) {
      for (const [key, val] of Object.entries(d.status)) {
        const icon = val === 'imported' ? '✅' : '⏳';
        html += '<li>' + icon + ' ' + esc(key) + ': ' + esc(val) + '</li>';
      }
    }
    html += '</ul></div>';

    if (d.warnings && d.warnings.length > 0) {
      html += '<div style="margin-top:8px;"><strong>Warnings:</strong><ul>';
      d.warnings.forEach(w => { html += '<li style="color:var(--orange);">' + esc(w) + '</li>'; });
      html += '</ul></div>';
    }

    html += '</div></div>';
    resultDiv.innerHTML = html;
  } catch(e) {
    resultDiv.innerHTML = '<div class="alert alert-error">File import failed: ' + esc(e.message || 'Unknown error') + '</div>';
  }
}

async function importMigrationSql() {
  const resultDiv = document.getElementById('migrate-sql-result');
  if (!resultDiv) return;
  resultDiv.innerHTML = 'Starting SQL import...';

  try {
    const backup = document.getElementById('migrate-backup').value;
    const domain = document.getElementById('migrate-domain').value.trim();
    const owner = document.getElementById('migrate-owner').value.trim();
    const keepStaging = document.getElementById('migrate-keep-staging').checked;

    const body = { backup, domain, owner };
    if (keepStaging) body.keep_staging = true;

    const res = await apiPost('/api/migration/vesta/import-sql', body);
    const d = res.data;
    const jobId = d.job_id;

    // Poll job status
    resultDiv.innerHTML = '<div class="card" style="margin-top:12px;"><div class="card-header"><h3>SQL Import Running</h3></div><div style="padding:12px;font-size:13px;"><p>Job #' + jobId + ' started. Waiting for completion...</p><div id="migrate-sql-progress"></div></div></div>';

    const poll = setInterval(async () => {
      try {
        const statusRes = await api('/api/jobs?id=' + jobId);
        // API returns { success, data: { id, type, status, progress, message } }
        // or { success, data: [...] } for list view
        const jobData = statusRes && statusRes.data;
        if (!jobData || !jobData.status) {
          // Don't clear polling yet — maybe job is still being created
          return;
        }
        const progressDiv = document.getElementById('migrate-sql-progress');
        if (progressDiv) {
          const statusDisplay = esc(jobData.status || 'unknown');
          const progressDisplay = typeof jobData.progress === 'number' ? jobData.progress : 0;
          const msgDisplay = esc(jobData.message || '');
          progressDiv.innerHTML = '<div style="margin-bottom:8px;"><strong>Status:</strong> ' + statusDisplay + '</div>'
            + '<div style="margin-bottom:8px;"><strong>Progress:</strong> ' + progressDisplay + '%</div>'
            + (msgDisplay ? '<div><strong>Message:</strong> ' + msgDisplay + '</div>' : '');
        }

        if (jobData.status === 'completed') {
          clearInterval(poll);
          resultDiv.innerHTML = '<div class="card" style="margin-top:12px;border-color:var(--green);"><div class="card-header"><h3 style="color:var(--green);">Stage 3 Completed</h3></div><div style="padding:12px;font-size:13px;"><p>SQL import completed successfully.</p></div></div>';
        } else if (jobData.status === 'failed') {
          clearInterval(poll);
          resultDiv.innerHTML = '<div class="alert alert-error">SQL import failed: ' + esc(jobData.message || 'Unknown error') + '</div>';
        }
      } catch(e) {
        clearInterval(poll);
        resultDiv.innerHTML = '<div class="alert alert-error">Failed to poll job status: ' + esc(e.message || 'Unknown') + '</div>';
      }
    }, 2000);
  } catch(e) {
    resultDiv.innerHTML = '<div class="alert alert-error">SQL import failed: ' + esc(e.message || 'Unknown error') + '</div>';
  }
}

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
    </div>
    <div class="card" style="margin-top:16px">
      <h3>Change Password</h3>
      <div style="padding:12px">
        <div style="margin-bottom:8px">
          <label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Current Password</label>
          <input id="cp-old-pass" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
        </div>
        <div style="margin-bottom:8px">
          <label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">New Password</label>
          <input id="cp-new-pass" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
        </div>
        <div style="margin-bottom:8px">
          <label style="font-size:12px;color:var(--text2);display:block;margin-bottom:4px;">Confirm New Password</label>
          <input id="cp-confirm-pass" type="password" style="width:100%;padding:8px 12px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;">
        </div>
        <button class="btn btn-primary btn-sm" onclick="changeAdminPassword()">Change Password</button>
        <div id="cp-status" style="margin-top:8px;font-size:12px;color:var(--text3);"></div>
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

async function changeAdminPassword() {
  const oldP = $('cp-old-pass').value;
  const newP = $('cp-new-pass').value;
  const confirmP = $('cp-confirm-pass').value;
  const status = $('cp-status');
  if (!oldP || !newP || !confirmP) { status.textContent = 'Fill in all fields'; return; }
  if (newP !== confirmP) { status.textContent = 'New passwords do not match'; return; }
  if (newP.length < 4) { status.textContent = 'Password too short (min 4 chars)'; return; }
  status.textContent = 'Changing password...';
  try {
    const res = await apiPost('/auth/change-password', {old_password: oldP, new_password: newP});
    if (res.success) {
      status.textContent = '';
      $('cp-old-pass').value = '';
      $('cp-new-pass').value = '';
      $('cp-confirm-pass').value = '';
      toast('Password changed successfully', 'success');
    } else {
      status.textContent = res.error || 'Failed to change password';
    }
  } catch(e) { status.textContent = 'Network error'; }
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
