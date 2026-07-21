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
    const set = Array.from(this.listeners.get(e.type) || []);
    for (const listener of set) listener(e);
  }

  appendChild(child) {
    this.children.push(child);
    if (child.id) this.document.elements.set(child.id, child);
  }

  remove() {
    if (this.id) this.document.elements.delete(this.id);
  }

  focus() { this.document.focused = this; }

  querySelector() { return null; }
}

class FakeDocument {
  constructor() {
    this.elements = new Map();
    this.body = new FakeElement(this, 'body');
    this.focused = null;
  }

  createElement(tagName) { return new FakeElement(this, tagName); }
  getElementById(id) { return this.elements.get(id) || null; }
  querySelector() { return null; }
  querySelectorAll() { return []; }
}

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

function listenerCount(el, type) {
  return (el.listeners.get(type) || new Set()).size;
}

function makeResponse(status, body) {
  return { ok: status >= 200 && status < 300, status, json: async () => body };
}

async function tick() {
  await Promise.resolve();
  await Promise.resolve();
  await new Promise(resolve => setImmediate(resolve));
}

(async () => {
  global.window = globalThis;
  global.document = new FakeDocument();
  global.localStorage = { getItem() { return null; }, setItem() {}, removeItem() {} };
  global.location = { reload() { throw new Error('unexpected reload'); } };
  global.setTimeout = (fn) => { fn(); return 1; };
  global.clearTimeout = () => {};
  global.setInterval = () => 1;
  global.clearInterval = () => {};

  const requests = [];
  let fetchImpl = async (url, opts) => {
    requests.push({ url, opts: opts || {} });
    return makeResponse(200, { success: true, data: {} });
  };
  global.fetch = (url, opts) => fetchImpl(url, opts);

  const mod = await import(pathToFileURL(path.join(root, 'web/pages/databases.js')).href + '?drop-modal-test=' + Date.now());
  const db = {
    database_id: 13,
    id: 13,
    site_id: 13,
    domain: 'test-gui-apache.local',
    database_name: 'test_gui_apache_local_db',
    database_user: 'test_gui_apache_local_user',
    can_drop: true,
    can_verify: true,
    runtime_status: 'Running',
    connection_status: 'verified',
    credential_state: 'available',
    ownership_state: 'managed',
  };

  function resetModal() {
    window.hideModal();
    requests.length = 0;
    window.dbDashboardState.selectedDetail = db;
    window.dbDashboardState.dropSubmitting = false;
    window.showDatabaseDropConfirm(13);
    return {
      input: document.getElementById('db-drop-confirm'),
      submit: document.getElementById('db-drop-submit'),
      cancel: document.getElementById('db-drop-cancel'),
      msg: document.getElementById('db-drop-confirm-msg'),
      overlay: document.getElementById('modal-overlay'),
    };
  }

  let controls = resetModal();
  assert(typeof window.confirmDatabaseDrop === 'function', 'drop handler is not exported for compatibility');
  assert(controls.input && controls.submit && controls.cancel, 'drop modal controls were not rendered');
  assert(window.dbDashboardState.selectedDetail === db, 'selected detail was cleared while modal is open');
  assert(listenerCount(controls.submit, 'click') === 1, 'drop submit listener is not bound exactly once');

  controls.input.value = 'wrong_value';
  controls.input.dispatchEvent({ type: 'input' });
  assert(controls.submit.disabled, 'wrong confirmation enabled submit');
  controls.submit.dispatchEvent({ type: 'click' });
  assert(requests.filter(r => r.opts.method === 'POST').length === 0, 'wrong confirmation called the API');

  controls = resetModal();
  controls.input.value = 'drop';
  controls.input.dispatchEvent({ type: 'input' });
  assert(controls.submit.disabled, 'generic confirmation enabled submit');
  controls.submit.dispatchEvent({ type: 'click' });
  assert(requests.filter(r => r.opts.method === 'POST').length === 0, 'generic confirmation called the API');

  controls = resetModal();
  controls.input.value = db.database_name;
  controls.input.dispatchEvent({ type: 'input' });
  assert(!controls.submit.disabled, 'exact database name did not enable submit');

  controls = resetModal();
  let resolvePost;
  fetchImpl = async (url, opts) => {
    requests.push({ url, opts: opts || {} });
    if ((opts || {}).method === 'POST') {
      return new Promise(resolve => { resolvePost = () => resolve(makeResponse(202, { success: true, data: { job_id: 77 } })); });
    }
    if (String(url).includes('/api/jobs?id=77')) return makeResponse(200, { success: true, data: { status: 'completed', progress: 100, message: 'Managed database dropped', steps: [] } });
    if (String(url).includes('/api/databases')) return makeResponse(200, { success: true, data: [] });
    return makeResponse(200, { success: true, data: {} });
  };
  controls.input.value = db.domain;
  controls.input.dispatchEvent({ type: 'input' });
  controls.submit.dispatchEvent({ type: 'click' });
  assert(controls.submit.disabled, 'submit button was not disabled while submitting');
  controls.submit.dispatchEvent({ type: 'click' });
  assert(requests.filter(r => r.opts.method === 'POST').length === 1, 'duplicate submit produced more than one POST');
  resolvePost();
  await tick();
  assert(requests.some(r => String(r.url).includes('/ui-api/api/jobs?id=77')), 'HTTP 202 did not start job polling');
  assert(requests.some(r => String(r.url) === '/ui-api/api/databases'), 'successful drop did not refresh inventory');

  controls = resetModal();
  fetchImpl = async (url, opts) => {
    requests.push({ url, opts: opts || {} });
    return makeResponse(500, { success: false, error: { code: 'test_error', message: 'backend refused drop' } });
  };
  controls.input.value = db.database_name;
  controls.input.dispatchEvent({ type: 'input' });
  controls.submit.dispatchEvent({ type: 'click' });
  await tick();
  assert(controls.msg.textContent.includes('backend refused drop'), 'API error was not displayed inside modal');
  assert(!controls.submit.disabled, 'submit button was not restored after failed request');

  const rendered = document.getElementById('modal-overlay').innerHTML;
  for (const forbidden of ['DB_PASSWORD', 'MYSQL_ROOT_PASSWORD', 'CONTAINERCP_DB_SERVICE_PASSWORD', 'generated_password']) {
    assert(!rendered.includes(forbidden), `modal rendered forbidden secret token ${forbidden}`);
  }

  controls = resetModal();
  const inputBeforeClose = controls.input;
  const submitBeforeClose = controls.submit;
  window.hideModal();
  assert(listenerCount(inputBeforeClose, 'input') === 0, 'closing modal did not remove input listener');
  assert(listenerCount(submitBeforeClose, 'click') === 0, 'closing modal did not remove submit listener');

  controls = resetModal();
  const staleSubmit = controls.submit;
  mod.databasesPage.unmount();
  staleSubmit.dispatchEvent({ type: 'click' });
  assert(requests.filter(r => r.opts.method === 'POST').length === 0, 'navigation cleanup left stale drop handler active');

  console.log('database drop modal regression tests passed');
})().catch(err => {
  console.error(err && err.stack ? err.stack : String(err));
  process.exit(1);
});
