const API_BASE = '/ui-api';
let sessionToken = localStorage.getItem('session_token');
let currentUser = null;

export function getSessionToken() { return sessionToken; }
export function setSessionToken(token) { sessionToken = token; if (token) localStorage.setItem('session_token', token); }
export function clearSessionToken() { sessionToken = null; localStorage.removeItem('session_token'); }
export function getCurrentUser() { return currentUser; }
export function setCurrentUser(user) { currentUser = user; }

export async function api(path, opts) {
  opts = opts || {};
  opts.headers = Object.assign({'Content-Type':'application/json'}, opts.headers||{});
  if (sessionToken) opts.headers['X-Session-Token'] = sessionToken;
  const res = await fetch(API_BASE + path.replace(/^\/api/, ''), opts);
  let data = {};
  try { data = await res.json(); } catch(e) {}
  if (!res.ok) {
    if (res.status === 401 && !path.includes('/auth/login')) {
      clearSessionToken();
      location.reload();
    }
    const msg = (data.error && data.error.message) || data.error || data.message || ('HTTP ' + res.status);
    const err = new Error(msg);
    err.status = res.status;
    err.code = data.error && data.error.code;
    err.api_message = data.error && data.error.message;
    err.body = data;
    throw err;
  }
  return data;
}

export async function apiPost(path, body, method) {
  return api(path, { method: method || 'POST', body: JSON.stringify(body || {}) });
}
