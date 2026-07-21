import { api, apiPost, clearSessionToken, getCurrentUser, getSessionToken, setCurrentUser, setSessionToken } from './api.js';
import { $, qsa } from './dom.js';
import { toast } from './notifications.js';
import { esc } from './utils.js';
import { leaveActiveRoute, navigate } from './router.js';

let statusInterval = null;

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
      setSessionToken(res.data.token);
      setCurrentUser({username: res.data.username, must_change_password: res.data.must_change_password});
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
      setCurrentUser(Object.assign({}, getCurrentUser() || {}, { must_change_password: false }));
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
  clearSessionToken();
  setCurrentUser(null);
  leaveActiveRoute();
  if (statusInterval) { clearInterval(statusInterval); statusInterval = null; }
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

  if (getCurrentUser()) {
    $('userDisplay').textContent = (getCurrentUser() || {}).username || '';
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
  if (statusInterval) clearInterval(statusInterval);
  statusInterval = setInterval(updateStatus, 30000);
  navigate('dashboard');
}

/* ===== AUTH CHECK ===== */
async function checkAuth() {
  if (!getSessionToken()) {
    renderLogin();
    return;
  }
  try {
    const res = await api('/auth/me');
    setCurrentUser({username: res.data.username, must_change_password: res.data.must_change_password});
    if (res.data.must_change_password) {
      renderChangePassword();
    } else {
      initApp();
    }
  } catch(e) {
    clearSessionToken();
    renderLogin(e.status === 401 ? null : 'Session expired. Please log in again.');
  }
}



/* ===== NAVIGATION ===== */
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
    window.searchTerm = e.target.value;
    if (window.renderTable) window.renderTable();
  }
});
document.addEventListener('DOMContentLoaded', checkAuth);
Object.assign(window, { renderLogin, doLogin, renderChangePassword, doChangePassword, doLogout, initApp, checkAuth, toggleTheme, loadVersion, updateStatus });
