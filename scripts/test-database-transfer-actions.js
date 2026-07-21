#!/usr/bin/env node

const path = require('path');
const { pathToFileURL } = require('url');

const root = path.resolve(__dirname, '..');

class FakeElement {
  constructor(document, tagName) {
    this.document = document;
    this.tagName = tagName;
    this.children = [];
    this.listeners = new Map();
    this.style = {};
    this.value = '';
    this.disabled = false;
    this.textContent = '';
    this.className = '';
    this.id = '';
    this.files = [];
    this._innerHTML = '';
  }
  set innerHTML(value) {
    this._innerHTML = String(value || '');
    const tagRe = /<([a-zA-Z0-9]+)([^>]*)>/g;
    let match;
    while ((match = tagRe.exec(this._innerHTML)) !== null) {
      const attrs = match[2] || '';
      const idMatch = attrs.match(/\sid="([^"]+)"/);
      if (!idMatch) continue;
      const el = new FakeElement(this.document, match[1].toLowerCase());
      el.id = idMatch[1];
      el.disabled = /\sdisabled(?:\s|>|$)/.test(attrs);
      this.document.elements.set(el.id, el);
    }
  }
  get innerHTML() { return this._innerHTML; }
  addEventListener(type, listener) {
    if (!this.listeners.has(type)) this.listeners.set(type, new Set());
    this.listeners.get(type).add(listener);
  }
  removeEventListener(type, listener) {
    const set = this.listeners.get(type);
    if (set) set.delete(listener);
  }
  dispatchEvent(event) {
    const e = Object.assign({ target: this, type: '' }, event || {});
    for (const listener of Array.from(this.listeners.get(e.type) || [])) listener(e);
  }
  appendChild(child) { this.children.push(child); if (child.id) this.document.elements.set(child.id, child); }
  remove() { if (this.id) this.document.elements.delete(this.id); }
  click() { this.dispatchEvent({ type: 'click' }); }
  focus() { this.document.focused = this; }
  querySelector() { return null; }
}

class FakeDocument {
  constructor() { this.elements = new Map(); this.body = new FakeElement(this, 'body'); this.focused = null; }
  createElement(tagName) { return new FakeElement(this, tagName); }
  getElementById(id) { return this.elements.get(id) || null; }
  querySelector() { return null; }
  querySelectorAll() { return []; }
}

function assert(condition, message) { if (!condition) throw new Error(message); }
function listenerCount(el, type) { return (el.listeners.get(type) || new Set()).size; }
function makeResponse(status, body) { return { ok: status >= 200 && status < 300, status, json: async () => body, blob: async () => ({}) }; }
async function tick() { await Promise.resolve(); await Promise.resolve(); await new Promise(resolve => setImmediate(resolve)); }

(async () => {
  global.window = globalThis;
  global.document = new FakeDocument();
  global.localStorage = { getItem() { return 'session-token'; }, setItem() {}, removeItem() {} };
  global.location = { reload() { throw new Error('unexpected reload'); } };
  global.URL = { createObjectURL() { return 'blob:fake'; }, revokeObjectURL() {} };
  global.setTimeout = (fn) => { fn(); return 1; };
  global.clearTimeout = () => {};
  global.setInterval = () => 1;
  global.clearInterval = () => {};

  const requests = [];
  global.fetch = async (url, opts) => {
    requests.push({ url, opts: opts || {} });
    if (String(url).includes('/import-upload')) return makeResponse(201, { success:true, data:{ artifact_id:'0123456789abcdef0123456789abcdef' } });
    if (String(url).includes('/import')) return makeResponse(202, { success:true, data:{ job_id:91, artifact_id:'0123456789abcdef0123456789abcdef' } });
    if (String(url).includes('/api/jobs?id=91')) return makeResponse(200, { success:true, data:{ status:'completed', progress:100, message:'Database import completed', steps:[] } });
    if (String(url).includes('/api/databases/13')) return makeResponse(200, { success:true, data:db });
    if (String(url).includes('/api/databases')) return makeResponse(200, { success:true, data:[db] });
    return makeResponse(200, { success:true, data:{} });
  };

  await import(pathToFileURL(path.join(root, 'web/pages/databases.js')).href + '?transfer-test=' + Date.now());
  const db = {
    database_id: 13,
    id: 13,
    site_id: 13,
    domain: 'test-gui-apache.local',
    database_name: 'test_gui_apache_local_db',
    database_user: 'test_gui_apache_local_user',
    can_drop: true,
    can_verify: true,
    can_export: true,
    can_import: true,
    max_import_size: 5242880,
    supported_import_formats: '.sql',
    runtime_status: 'Running',
    connection_status: 'verified',
    credential_state: 'available',
    ownership_state: 'managed',
  };

  window.dbDashboardState.selectedDetail = db;
  const html = window.renderDatabaseTransferActions(db);
  assert(html.includes('db-export-btn'), 'export button did not render');
  assert(html.includes('db-import-btn'), 'import button did not render');
  assert(!html.includes('DB_PASSWORD') && !html.includes('/srv/containercp'), 'transfer action rendered a secret/path token');

  window.showDatabaseImportModal(13);
  const file = document.getElementById('db-import-file');
  const input = document.getElementById('db-import-confirm');
  const submit = document.getElementById('db-import-submit');
  assert(file && input && submit, 'import modal controls missing');
  assert(listenerCount(submit, 'click') === 1, 'import submit listener not bound once');
  input.value = 'wrong';
  file.files = [{ name:'dump.sql', size:128 }];
  input.dispatchEvent({ type:'input' });
  file.dispatchEvent({ type:'change' });
  assert(submit.disabled, 'wrong import confirmation enabled submit');
  submit.dispatchEvent({ type:'click' });
  assert(!requests.some(r => String(r.url).includes('/import-upload')), 'wrong confirmation uploaded file');

  input.value = db.database_name;
  input.dispatchEvent({ type:'input' });
  assert(!submit.disabled, 'exact import confirmation did not enable submit');
  submit.dispatchEvent({ type:'click' });
  submit.dispatchEvent({ type:'click' });
  await tick();
  assert(requests.filter(r => String(r.url).includes('/import-upload')).length === 1, 'duplicate import produced multiple uploads');
  assert(requests.some(r => String(r.url).includes('/api/databases/13/import')), 'import endpoint was not called after upload');
  assert(requests.some(r => String(r.url).includes('/api/jobs?id=91')), 'import job polling did not start');

  window.showDatabaseImportModal(13);
  const staleSubmit = document.getElementById('db-import-submit');
  window.hideModal();
  assert(listenerCount(staleSubmit, 'click') === 0, 'closing import modal did not remove submit listener');

  console.log('database transfer action regression tests passed');
})().catch(err => {
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(1);
});
