#!/usr/bin/env node

const fs = require('fs');
const path = require('path');
const { pathToFileURL } = require('url');

const root = path.resolve(__dirname, '..');

function read(rel) {
  return fs.readFileSync(path.join(root, rel), 'utf8');
}

function fail(message) {
  console.error(`frontend baseline check failed: ${message}`);
  process.exitCode = 1;
}

function requireIncludes(file, text, label) {
  if (!file.includes(text)) fail(label);
}

function requireMatch(file, regex, label) {
  if (!regex.test(file)) fail(label);
}

const indexHtml = read('web/index.html');
const appJs = read('web/app.js');
const shellJs = read('web/core/shell.js');
const contextJs = read('web/core/context.js');
const cacheJs = read('web/js/cache.js');
const utilsJs = read('web/js/utils.js');

requireIncludes(indexHtml, '<script type="module" src="/app.js"></script>', 'missing native module entry point');
if (indexHtml.includes('<script src="/js/cache.js"></script>')) fail('legacy cache.js script tag still present');
if (indexHtml.includes('<script src="/js/utils.js"></script>')) fail('legacy utils.js script tag still present');
if (indexHtml.includes('<script src="/app.js"></script>')) fail('legacy classic app.js script tag still present');

const mainRoutes = [
  'dashboard', 'sites', 'domains', 'databases', 'ssl', 'mail', 'webmail',
  'proxy', 'access', 'backups', 'migration', 'profiles', 'templates',
  'nodes', 'logs', 'settings',
];

for (const route of mainRoutes) {
  requireIncludes(shellJs, `data-page="${route}"`, `missing main menu data-page ${route}`);
  requireIncludes(appJs, `registerRoute('${route}'`, `missing route registration for ${route}`);
}

const detailRoutes = ['site-detail', 'domain-detail', 'mail-domain', 'mail-health'];
for (const route of detailRoutes) {
  requireIncludes(appJs, `registerRoute('${route}'`, `missing detail route registration for ${route}`);
}

const appGlobals = [
  'api, apiPost',
  'esc, escAttr, jsString, dbJsArg',
  'toast',
  'showModal, hideModal',
  'copyText',
  'navigate, navigateTo',
  'pollRotationJob',
  'renderWordPressRotationDiagnostics',
  'renderRotationJobTimeline',
];

for (const needle of appGlobals) {
  requireIncludes(contextJs, needle, `missing core compatibility/global ${needle}`);
}

const requiredPageModules = [
  'dashboard', 'sites', 'domains', 'databases', 'ssl', 'mail', 'webmail',
  'proxy', 'access', 'backups', 'migration', 'profiles', 'templates',
  'nodes', 'logs', 'settings',
];

for (const page of requiredPageModules) {
  const rel = `web/pages/${page}.js`;
  if (!fs.existsSync(path.join(root, rel))) fail(`missing page module ${rel}`);
}

const cacheGlobals = ['window.DnsCache', 'window.RuntimeCache', 'window.HealthCache'];
for (const needle of cacheGlobals) {
  requireIncludes(cacheJs, needle, `missing cache global ${needle}`);
}

const utilityGlobals = [
  'window.processBatch',
  'window.dnsStatusBadge',
  'window.runtimeStatusBadge',
  'window.healthGradeBadge',
  'window.computeDomainHealthScore',
  'window.attachDataCopyListener',
];

for (const needle of utilityGlobals) {
  requireIncludes(utilsJs, needle, `missing utility global ${needle}`);
}

function collectJs(dir) {
  const out = [];
  for (const entry of fs.readdirSync(path.join(root, dir), { withFileTypes: true })) {
    const rel = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...collectJs(rel));
    else if (entry.name.endsWith('.js')) out.push(rel);
  }
  return out;
}

const combinedJs = collectJs('web').map(read).join('\n');
const forbiddenSecretSurfaces = [
  'database_password',
  'mysql_password',
  'root_password',
  'generated_password',
  'MYSQL_ROOT_PASSWORD',
  'MYSQL_PASSWORD',
];

for (const forbidden of forbiddenSecretSurfaces) {
  if (combinedJs.includes(forbidden)) {
    fail(`forbidden frontend secret surface token found: ${forbidden}`);
  }
}

if (!process.exitCode) {
  checkApiProxyUrls().then(() => {
    if (!process.exitCode) console.log('frontend baseline check passed');
  }).catch((err) => {
    fail(`API proxy URL check failed: ${err && err.message ? err.message : err}`);
  });
}

async function checkApiProxyUrls() {
  const urls = [];
  global.localStorage = {
    getItem() { return null; },
    setItem() {},
    removeItem() {},
  };
  global.fetch = async (url) => {
    urls.push(url);
    return { ok: true, json: async () => ({ success: true }) };
  };

  const apiModule = await import(pathToFileURL(path.join(root, 'web/core/api.js')).href);
  await apiModule.api('/api/sites');
  await apiModule.api('/api/databases');
  await apiModule.api('/auth/me');

  const expected = ['/ui-api/api/sites', '/ui-api/api/databases', '/ui-api/auth/me'];
  for (let i = 0; i < expected.length; i += 1) {
    if (urls[i] !== expected[i]) {
      fail(`expected api URL ${expected[i]}, got ${urls[i] || '<none>'}`);
    }
  }
}
