import {
  api, apiPost, card, copyText, esc, hideModal, pollRotationJob, renderRotationJobTimeline, renderWordPressRotationDiagnostics, showModal, toast
} from '../core/context.js';


/* ===== DATABASES ===== */
const dbDashboardState = {
  items: [],
  search: '',
  filters: {health:'all', runtime:'all', connection:'all', credentials:'all', ownership:'all'},
  sortBy: 'health',
  sortDir: 'asc',
  selectedId: null,
  selectedDetail: null,
  rotationStatus: null,
  rotationLoading: false,
  loadError: ''
};
window.dbDashboardState = dbDashboardState;
let activeDatabasesLifecycle = null;

function normalizeDbValue(value, fallback) {
  const v = value == null ? '' : String(value).trim();
  return v || (fallback || 'Unknown');
}

function dbConnectionState(db) {
  const raw = normalizeDbValue(db.connection_status, 'not_checked').toLowerCase();
  if (raw === 'verified' || raw === 'connected' || raw === 'available') return 'connected';
  if (raw === 'connection_failed' || raw === 'failed') return 'failed';
  return 'unknown';
}

function dbCredentialState(db) {
  const raw = normalizeDbValue(db.credential_state, 'unknown').toLowerCase();
  if (raw === 'available' || raw === 'valid') return 'available';
  if (raw === 'missing' || raw === 'credentials_unavailable') return 'missing';
  if (raw === 'invalid' || raw === 'unsupported' || raw === 'ambiguous') return 'invalid';
  return 'unknown';
}

function dbRuntimeState(db) {
  const raw = normalizeDbValue(db.runtime_status, 'Unknown');
  if (raw === 'Running') return 'running';
  if (raw === 'Stopped') return 'stopped';
  return 'unknown';
}

function dbOwnershipState(db) {
  const raw = normalizeDbValue(db.ownership_state, 'imported').toLowerCase();
  return raw === 'managed' ? 'managed' : 'imported';
}

function computeDatabaseHealthState(db) {
  const runtime = dbRuntimeState(db);
  const connection = dbConnectionState(db);
  const credentials = dbCredentialState(db);
  const imported = normalizeDbValue(db.imported_state, 'unknown').toLowerCase();

  if (runtime === 'stopped' || connection === 'failed' || credentials === 'invalid' || imported === 'site_missing') {
    return 'critical';
  }
  if (runtime === 'running' && connection === 'connected' && credentials === 'available') {
    return 'healthy';
  }
  if (runtime === 'unknown' && connection === 'unknown' && credentials === 'unknown') {
    return 'unknown';
  }
  return 'warning';
}
window.computeDatabaseHealthState = computeDatabaseHealthState;

function dbHealthLabel(state) {
  return ({healthy:'Healthy', warning:'Warning', critical:'Critical', unknown:'Unknown'})[state] || 'Unknown';
}

function dbBadge(label, cls) {
  return `<span class="badge ${cls || 'badge-info'}">${esc(label)}</span>`;
}

function dbHealthBadge(db) {
  const state = computeDatabaseHealthState(db);
  const cls = {healthy:'badge-ok', warning:'badge-warn', critical:'badge-err', unknown:'badge-info'}[state] || 'badge-info';
  return dbBadge(dbHealthLabel(state), cls);
}

function dbRuntimeBadge(db) {
  const state = dbRuntimeState(db);
  const label = state === 'running' ? 'Running' : (state === 'stopped' ? 'Stopped' : 'Unknown');
  return dbBadge(label, state === 'running' ? 'badge-ok' : (state === 'stopped' ? 'badge-err' : 'badge-info'));
}

function dbConnectionBadge(db) {
  const state = dbConnectionState(db);
  const label = state === 'connected' ? 'Connected' : (state === 'failed' ? 'Failed' : 'Unknown');
  return dbBadge(label, state === 'connected' ? 'badge-ok' : (state === 'failed' ? 'badge-err' : 'badge-info'));
}

function dbCredentialBadge(db) {
  const state = dbCredentialState(db);
  const label = {available:'Available', missing:'Missing', invalid:'Invalid', unknown:'Unknown'}[state] || 'Unknown';
  const cls = state === 'available' ? 'badge-ok' : (state === 'invalid' ? 'badge-err' : (state === 'missing' ? 'badge-warn' : 'badge-info'));
  return dbBadge(label, cls);
}

function dbOwnershipBadge(db) {
  const state = dbOwnershipState(db);
  return dbBadge(state === 'managed' ? 'Managed' : 'Imported', state === 'managed' ? 'badge-ok' : 'badge-warn');
}

function dbJsArg(value) {
  return JSON.stringify(String(value == null ? '' : value));
}

function dbDateValue(value) {
  if (!value) return 0;
  const ts = Date.parse(value);
  return Number.isFinite(ts) ? ts : 0;
}

function dbSortRank(db, key) {
  if (key === 'health') return ({critical:0, warning:1, unknown:2, healthy:3})[computeDatabaseHealthState(db)] ?? 2;
  if (key === 'runtime') return ({stopped:0, unknown:1, running:2})[dbRuntimeState(db)] ?? 1;
  if (key === 'connection') return ({failed:0, unknown:1, connected:2})[dbConnectionState(db)] ?? 1;
  if (key === 'ownership') return ({imported:0, managed:1})[dbOwnershipState(db)] ?? 0;
  if (key === 'created_at') return dbDateValue(db.created_at);
  if (key === 'updated_at') return dbDateValue(db.updated_at);
  if (key === 'database_name') return normalizeDbValue(db.database_name || db.name, '').toLowerCase();
  return normalizeDbValue(db.domain, '').toLowerCase();
}

function getFilteredDatabases() {
  const search = (dbDashboardState.search || '').toLowerCase();
  const filters = dbDashboardState.filters;
  const rows = dbDashboardState.items.filter(db => {
    const matchesSearch = !search
      || normalizeDbValue(db.domain, '').toLowerCase().includes(search)
      || normalizeDbValue(db.database_name || db.name, '').toLowerCase().includes(search)
      || normalizeDbValue(db.database_user || db.user, '').toLowerCase().includes(search)
      || normalizeDbValue(db.engine, '').toLowerCase().includes(search);
    if (!matchesSearch) return false;
    if (filters.health !== 'all' && computeDatabaseHealthState(db) !== filters.health) return false;
    if (filters.runtime !== 'all' && dbRuntimeState(db) !== filters.runtime) return false;
    if (filters.connection !== 'all' && dbConnectionState(db) !== filters.connection) return false;
    if (filters.credentials !== 'all' && dbCredentialState(db) !== filters.credentials) return false;
    if (filters.ownership !== 'all' && dbOwnershipState(db) !== filters.ownership) return false;
    return true;
  });
  rows.sort((a, b) => {
    const av = dbSortRank(a, dbDashboardState.sortBy);
    const bv = dbSortRank(b, dbDashboardState.sortBy);
    let cmp = av < bv ? -1 : (av > bv ? 1 : 0);
    if (cmp === 0) cmp = normalizeDbValue(a.domain, '').localeCompare(normalizeDbValue(b.domain, ''));
    return dbDashboardState.sortDir === 'desc' ? -cmp : cmp;
  });
  return rows;
}

function databaseSummaryCards(databases) {
  const counts = {total:databases.length, healthy:0, warning:0, critical:0, imported:0};
  databases.forEach(db => {
    const health = computeDatabaseHealthState(db);
    if (health === 'healthy') counts.healthy++;
    else if (health === 'warning') counts.warning++;
    else if (health === 'critical') counts.critical++;
    if (dbOwnershipState(db) === 'imported') counts.imported++;
  });
  const card = (label, value, cls, help) => `<div class="db-summary-card ${cls}"><div class="db-summary-label">${esc(label)}</div><div class="db-summary-value">${value}</div><div class="db-summary-help">${esc(help)}</div></div>`;
  return `<div class="db-summary-grid">`
    + card('Total Databases', counts.total, 'neutral', 'Managed application database records')
    + card('Healthy', counts.healthy, 'healthy', 'Runtime, connection, and credentials OK')
    + card('Warning', counts.warning, 'warning', 'Needs administrator attention')
    + card('Critical', counts.critical, 'critical', 'Stopped, failed, invalid, or missing relation')
    + card('Imported', counts.imported, 'imported', 'Represented without full lifecycle ownership')
    + `</div>`;
}

function dbSelect(id, label, options, value, onChange) {
  return `<label class="db-filter"><span>${esc(label)}</span><select id="${esc(id)}" onchange="${onChange}">`
    + options.map(o => `<option value="${esc(o.value)}" ${o.value === value ? 'selected' : ''}>${esc(o.label)}</option>`).join('')
    + `</select></label>`;
}

function databaseControlsHtml() {
  const opts = (items) => [{value:'all', label:'All'}].concat(items);
  return `<div class="db-controls card">
    <div class="db-search"><label for="db-search-input">Search</label><input id="db-search-input" value="${esc(dbDashboardState.search)}" placeholder="Domain, database, user, engine" oninput="dbDashboardState.search=this.value;renderDatabaseInventory();"></div>
    ${dbSelect('db-filter-health','Health',opts([{value:'healthy',label:'Healthy'},{value:'warning',label:'Warning'},{value:'critical',label:'Critical'},{value:'unknown',label:'Unknown'}]),dbDashboardState.filters.health,"dbDashboardState.filters.health=this.value;renderDatabaseInventory();")}
    ${dbSelect('db-filter-runtime','Runtime',opts([{value:'running',label:'Running'},{value:'stopped',label:'Stopped'},{value:'unknown',label:'Unknown'}]),dbDashboardState.filters.runtime,"dbDashboardState.filters.runtime=this.value;renderDatabaseInventory();")}
    ${dbSelect('db-filter-connection','Connection',opts([{value:'connected',label:'Connected'},{value:'failed',label:'Failed'},{value:'unknown',label:'Unknown'}]),dbDashboardState.filters.connection,"dbDashboardState.filters.connection=this.value;renderDatabaseInventory();")}
    ${dbSelect('db-filter-credentials','Credentials',opts([{value:'available',label:'Available'},{value:'missing',label:'Missing'},{value:'invalid',label:'Invalid'},{value:'unknown',label:'Unknown'}]),dbDashboardState.filters.credentials,"dbDashboardState.filters.credentials=this.value;renderDatabaseInventory();")}
    ${dbSelect('db-filter-ownership','Ownership',opts([{value:'managed',label:'Managed'},{value:'imported',label:'Imported'}]),dbDashboardState.filters.ownership,"dbDashboardState.filters.ownership=this.value;renderDatabaseInventory();")}
    ${dbSelect('db-sort-by','Sort by',[{value:'health',label:'Health severity'},{value:'domain',label:'Domain'},{value:'database_name',label:'Database name'},{value:'runtime',label:'Runtime'},{value:'connection',label:'Connection'},{value:'ownership',label:'Ownership'},{value:'created_at',label:'Created date'},{value:'updated_at',label:'Updated date'}],dbDashboardState.sortBy,"dbDashboardState.sortBy=this.value;renderDatabaseInventory();")}
    <button class="btn btn-sm" onclick="toggleDatabaseSortDirection()" aria-label="Toggle sort direction">${dbDashboardState.sortDir === 'asc' ? 'Ascending' : 'Descending'}</button>
    <button class="btn btn-sm" onclick="resetDatabaseFilters()">Reset filters</button>
  </div>`;
}

function toggleDatabaseSortDirection() {
  dbDashboardState.sortDir = dbDashboardState.sortDir === 'asc' ? 'desc' : 'asc';
  renderDatabaseInventory();
}

function resetDatabaseFilters() {
  dbDashboardState.search = '';
  dbDashboardState.filters = {health:'all', runtime:'all', connection:'all', credentials:'all', ownership:'all'};
  dbDashboardState.sortBy = 'health';
  dbDashboardState.sortDir = 'asc';
  renderDatabaseInventory();
}

async function loadDatabases(p, params, lifecycle) {
  activeDatabasesLifecycle = lifecycle || activeDatabasesLifecycle;
  if (lifecycle && lifecycle.addEventListener) {
    lifecycle.addEventListener(document, 'keydown', e => {
      if (e.key === 'Escape' && $('db-detail-backdrop') && $('db-detail-backdrop').style.display !== 'none') closeDatabaseDrawer();
    });
    lifecycle.onCleanup(destroyDatabaseDrawer);
  }
  p.innerHTML = `<div class="page-header db-page-header"><div><h1>Databases</h1><p>Monitor database health, runtime, connectivity, credentials, and ownership.</p></div><div class="page-actions"><button class="btn btn-sm" onclick="refreshDatabases()" aria-label="Refresh database inventory">Refresh</button></div></div><div id="db-dashboard-body" aria-live="polite"><div class="empty-state">Loading database inventory...</div></div>`;
  await refreshDatabases();
}

async function refreshDatabases() {
  const body = $('db-dashboard-body');
  if (body) body.innerHTML = '<div class="empty-state">Loading database inventory...</div>';
  try {
    const data = await api('/api/databases');
    if (activeDatabasesLifecycle && !activeDatabasesLifecycle.isActive()) return;
    dbDashboardState.items = data.data || [];
    dbDashboardState.loadError = '';
    renderDatabaseInventory();
  } catch(e) {
    dbDashboardState.items = [];
    dbDashboardState.loadError = 'Database inventory could not be loaded.';
    renderDatabaseInventory();
  }
}

function renderDatabaseInventory() {
  const body = $('db-dashboard-body');
  if (!body) return;
  if (dbDashboardState.loadError) {
    body.innerHTML = `<div class="empty-state">${esc(dbDashboardState.loadError)}<br><button class="btn btn-sm" style="margin-top:12px;" onclick="refreshDatabases()">Retry</button></div>`;
    return;
  }
  const rows = getFilteredDatabases();
  const emptyMessage = dbDashboardState.items.length === 0
    ? 'No managed databases were found.'
    : 'No databases match the current search and filters.';
  body.innerHTML = databaseSummaryCards(dbDashboardState.items)
    + databaseControlsHtml()
    + `<div class="db-inventory card"><div class="db-inventory-title"><strong>Database Inventory</strong><span>${rows.length} shown</span></div>`
    + (rows.length ? renderDatabaseTable(rows) + renderDatabaseCards(rows) : `<div class="empty-state">${emptyMessage}</div>`)
    + `</div>`;
}

function renderDatabaseTable(rows) {
  return `<div class="db-table-wrap"><table class="db-table"><thead><tr><th>Health</th><th>Domain</th><th>Database</th><th>User</th><th>Engine / Version</th><th>Runtime</th><th>Connection</th><th>Credentials</th><th>Ownership</th><th>Actions</th></tr></thead><tbody>`
    + rows.map(db => `<tr onclick="openDatabaseDetail(${Number(db.database_id || db.id || 0)})" tabindex="0" onkeydown="if(event.key==='Enter')openDatabaseDetail(${Number(db.database_id || db.id || 0)})">
      <td>${dbHealthBadge(db)}</td>
      <td><strong>${esc(db.domain || 'Unknown')}</strong></td>
      <td><code>${esc(db.database_name || db.name || 'Unknown')}</code></td>
      <td><code>${esc(db.database_user || db.user || 'Unknown')}</code></td>
      <td>${esc(db.engine || 'mariadb')}<div style="font-size:11px;color:var(--text3);">${esc(db.engine_version || db.version || 'Unknown')}</div></td>
      <td>${dbRuntimeBadge(db)}</td>
      <td>${dbConnectionBadge(db)}</td>
      <td>${dbCredentialBadge(db)}</td>
      <td>${dbOwnershipBadge(db)}</td>
      <td><button class="btn btn-sm" onclick="openDatabaseDetail(${Number(db.database_id || db.id || 0)});event.stopPropagation();">Details</button></td>
    </tr>`).join('')
    + `</tbody></table></div>`;
}

function renderDatabaseCards(rows) {
  return `<div class="db-mobile-list">` + rows.map(db => `<button class="db-mobile-card" onclick="openDatabaseDetail(${Number(db.database_id || db.id || 0)})">
    <div class="db-mobile-main"><div><strong>${esc(db.domain || 'Unknown')}</strong><div><code>${esc(db.database_name || db.name || 'Unknown')}</code></div></div>${dbHealthBadge(db)}</div>
    <div class="db-mobile-statuses">${dbRuntimeBadge(db)}${dbConnectionBadge(db)}${dbCredentialBadge(db)}${dbOwnershipBadge(db)}</div>
  </button>`).join('') + `</div>`;
}

async function openDatabaseDetail(databaseId) {
  if (!databaseId) return;
  dbDashboardState.selectedId = Number(databaseId);
  showDatabaseDrawer(`<div class="empty-state">Loading database detail...</div>`);
  try {
    const res = await api('/api/databases/' + databaseId);
    if (activeDatabasesLifecycle && !activeDatabasesLifecycle.isActive()) return;
    const db = res.data || {};
    dbDashboardState.selectedDetail = db;
    dbDashboardState.rotationStatus = null;
    dbDashboardState.rotationLoading = true;
    renderDatabaseDetail(db);
    await loadDatabaseRotationStatus(db);
  } catch(e) {
    const msg = e.status === 404 ? 'The selected database no longer exists.' : 'Database detail could not be loaded.';
    showDatabaseDrawer(`<div class="db-drawer-header"><div><h2>Database Detail</h2></div><button class="btn-icon" onclick="closeDatabaseDrawer()" aria-label="Close database detail">&times;</button></div><div class="empty-state">${esc(msg)}<br><button class="btn btn-sm" style="margin-top:12px;" onclick="refreshDatabases();closeDatabaseDrawer();">Retry inventory</button></div>`);
  }
}

function showDatabaseDrawer(content) {
  let backdrop = $('db-detail-backdrop');
  if (!backdrop) {
    backdrop = document.createElement('div');
    backdrop.id = 'db-detail-backdrop';
    backdrop.className = 'db-drawer-backdrop';
    backdrop.addEventListener('click', e => { if (e.target === backdrop) closeDatabaseDrawer(); });
    document.body.appendChild(backdrop);
  }
  backdrop.innerHTML = `<aside class="db-detail-drawer" role="dialog" aria-modal="true" aria-label="Database detail" tabindex="-1">${content}</aside>`;
  backdrop.style.display = 'flex';
  const ctx = activeDatabasesLifecycle;
  const later = ctx && ctx.setTimeout ? ctx.setTimeout.bind(ctx) : setTimeout;
  later(() => {
    const drawer = backdrop.querySelector('.db-detail-drawer');
    if (drawer) drawer.focus();
  }, 0);
}

function closeDatabaseDrawer() {
  const backdrop = $('db-detail-backdrop');
  if (backdrop) backdrop.style.display = 'none';
  dbDashboardState.selectedId = null;
  dbDashboardState.selectedDetail = null;
  dbDashboardState.rotationStatus = null;
}

function destroyDatabaseDrawer() {
  const backdrop = $('db-detail-backdrop');
  if (backdrop) backdrop.remove();
  dbDashboardState.selectedId = null;
  dbDashboardState.selectedDetail = null;
  dbDashboardState.rotationStatus = null;
}

function renderDatabaseDetail(db) {
  const title = db.database_name || db.name || 'Database';
  const content = `<div class="db-drawer-header"><div><h2>${esc(title)}</h2><p>${esc(db.domain || 'Unknown domain')}</p></div><button class="btn-icon" onclick="closeDatabaseDrawer()" aria-label="Close database detail">&times;</button></div>
    <div class="db-detail-content">
      ${databaseDetailSection('Overview', [
        ['Overall health', dbHealthBadge(db)], ['Domain', esc(db.domain || 'Unavailable')], ['Database name', '<code>' + esc(db.database_name || db.name || 'Unavailable') + '</code>'], ['Database user', '<code>' + esc(db.database_user || db.user || 'Unavailable') + '</code>'], ['Engine', esc(db.engine || 'mariadb')], ['Engine version', esc(db.engine_version || db.version || 'Unavailable')], ['Ownership', dbOwnershipBadge(db)], ['Imported state', esc(db.imported_state || 'unknown')]
      ])}
      ${databaseHealthSection(db)}
      ${databaseDetailSection('Relationships', [
        ['Database ID', copyableValue(db.database_id || db.id || 0)], ['Site ID', copyableValue(db.site_id || 0)], ['Domain', copyableValue(db.domain || '')], ['Managed / Imported', dbOwnershipBadge(db)]
      ])}
      ${databaseDetailSection('Metadata', [
        ['Created', esc(db.created_at || 'Unavailable')], ['Updated', esc(db.updated_at || 'Unavailable')], ['Engine version', esc(db.engine_version || db.version || 'Unavailable')], ['Connection verification state', esc(db.connection_status || 'not_checked')], ['Imported state', esc(db.imported_state || 'unknown')]
      ])}
      <section class="db-detail-section"><h3>Actions</h3><div id="db-rotation-action">${renderDatabaseRotationAction(db)}</div></section>
    </div>`;
  showDatabaseDrawer(content);
}

function databaseDetailSection(title, fields) {
  return `<section class="db-detail-section"><h3>${esc(title)}</h3><div class="details-grid">`
    + fields.map(f => `<div class="details-field"><div class="details-label">${esc(f[0])}</div><div class="details-value">${f[1]}</div></div>`).join('')
    + `</div></section>`;
}

function databaseHealthSection(db) {
  const explanations = {
    runtime: dbRuntimeState(db) === 'running' ? 'The MariaDB service is currently running.' : (dbRuntimeState(db) === 'stopped' ? 'The MariaDB service is stopped or unavailable.' : 'ContainerCP could not determine the MariaDB runtime state.'),
    connection: dbConnectionState(db) === 'connected' ? 'ContainerCP successfully verified access to the application database.' : (dbConnectionState(db) === 'failed' ? 'ContainerCP could not verify access to the application database.' : 'Connection verification has not run or could not be checked safely.'),
    credentials: dbCredentialState(db) === 'available' ? 'Supported application credentials are available through the approved configuration boundary.' : (dbCredentialState(db) === 'invalid' ? 'Credential metadata is invalid or unsupported for safe use.' : (dbCredentialState(db) === 'missing' ? 'Credentials are not safely available for this database.' : 'Credential state is unknown.'))
  };
  return `<section class="db-detail-section"><h3>Health</h3><div class="db-health-detail-row"><div>${dbRuntimeBadge(db)}<p>${esc(explanations.runtime)}</p></div><div>${dbConnectionBadge(db)}<p>${esc(explanations.connection)}</p></div><div>${dbCredentialBadge(db)}<p>${esc(explanations.credentials)}</p></div></div></section>`;
}

function copyableValue(value) {
  const text = String(value == null || value === '' ? 'Unavailable' : value);
  if (text === 'Unavailable') return esc(text);
  return `<code>${esc(text)}</code> <button class="btn-icon" onclick="copyText(${dbJsArg(text)}, 'Copied')" aria-label="Copy ${esc(text)}">&#128203;</button>`;
}

async function loadDatabaseRotationStatus(db) {
  dbDashboardState.rotationLoading = true;
  updateDatabaseRotationAction(db);
  try {
    if (!db.site_id || db.imported_state === 'site_missing') throw new Error('missing site');
    const res = await api('/api/wordpress/database-credentials/status?site_id=' + Number(db.site_id));
    if (activeDatabasesLifecycle && !activeDatabasesLifecycle.isActive()) return;
    dbDashboardState.rotationStatus = res.data || {};
  } catch(e) {
    dbDashboardState.rotationStatus = {available:false, database_target_available:false, database_target_message:'Rotation capability could not be loaded for this database.'};
  }
  dbDashboardState.rotationLoading = false;
  updateDatabaseRotationAction(db);
}

function updateDatabaseRotationAction(db) {
  const el = $('db-rotation-action');
  if (el) el.innerHTML = renderDatabaseRotationAction(db);
}

function databaseRotationCapability(db) {
  const status = dbDashboardState.rotationStatus || {};
  if (dbDashboardState.rotationLoading) return {enabled:false, reason:'Checking rotation capability through the existing WordPress credential boundary.'};
  if (!db.site_id || normalizeDbValue(db.imported_state, '').toLowerCase() === 'site_missing') return {enabled:false, reason:'Rotation is unavailable because the database is not linked to an inspectable Site.'};
  if (dbRuntimeState(db) !== 'running') return {enabled:false, reason:'Rotation is unavailable while the MariaDB runtime state is not Running.'};
  if (dbConnectionState(db) === 'failed') return {enabled:false, reason:'Rotation is unavailable because the current connection verification failed.'};
  if (dbCredentialState(db) === 'invalid') return {enabled:false, reason:'Rotation is unavailable because credentials are invalid or unsupported.'};
  if (!status.available) return {enabled:false, reason:status.message || status.database_target_message || 'Rotation is unavailable for this application configuration.'};
  if (!status.database_target_available) return {enabled:false, reason:status.database_target_message || 'No exact backend database target was resolved.'};
  if (Number(status.database_id) !== Number(db.database_id || db.id)) return {enabled:false, reason:'The backend-resolved rotation target does not match this database.'};
  return {enabled:true, reason:'Rotation is supported by the existing backend workflow.'};
}

function renderDatabaseRotationAction(db) {
  const cap = databaseRotationCapability(db);
  const status = dbDashboardState.rotationStatus || {};
  return `<div class="db-action-box">
    <div><strong>Rotate Password</strong><p>Uses the existing WordPress credential rotation backend, job progress, runtime verification, and compensation flow.</p></div>
    <div style="display:flex;gap:8px;flex-wrap:wrap;align-items:center;">
      <span class="badge ${cap.enabled ? 'badge-ok' : 'badge-info'}">${cap.enabled ? 'Supported' : 'Unavailable'}</span>
      ${status.database_target_status ? `<span class="badge ${status.database_target_available ? 'badge-ok' : 'badge-warn'}">target: ${esc(status.database_target_status)}</span>` : ''}
      <button class="btn btn-sm btn-warning" onclick="showDatabaseRotationConfirm(${Number(db.database_id || db.id || 0)})" ${cap.enabled ? '' : 'disabled'}>Rotate Password</button>
    </div>
    <div id="db-rotation-msg" class="db-action-message">${esc(cap.reason)}</div>
  </div>`;
}

function showDatabaseRotationConfirm(databaseId) {
  const db = dbDashboardState.selectedDetail;
  if (!db || Number(db.database_id || db.id) !== Number(databaseId)) return;
  const cap = databaseRotationCapability(db);
  if (!cap.enabled) return;
  const domain = db.domain || '';
  showModal('Rotate Database Password', `<div class="db-confirm-body">
    <p><strong>${esc(domain)}</strong> / <code>${esc(db.database_name || db.name || '')}</code></p>
    <ul>
      <li>The MariaDB application password will be changed.</li>
      <li>The WordPress configuration will be updated through WordPressConfigService.</li>
      <li>Runtime and connection verification will run before success is reported.</li>
      <li>Compensation or rollback may run if a stage fails.</li>
      <li>No password will be displayed or stored in the browser.</li>
    </ul>
    <p style="color:var(--text2);font-size:12px;">Rotation is only available when the backend reports this target is supported. Type the domain to confirm.</p>
    <input id="db-rotate-confirm" autocomplete="off" placeholder="${esc(domain)}" style="width:100%;padding:8px 10px;border:1px solid var(--border);border-radius:6px;background:var(--bg3);color:var(--text);font-size:13px;outline:none;margin-top:8px;">
    <div id="db-rotate-confirm-msg" style="font-size:12px;color:var(--text3);margin-top:6px;">No password value will be shown.</div>
    <div style="display:flex;justify-content:flex-end;gap:8px;margin-top:14px;"><button class="btn btn-sm" onclick="hideModal()">Cancel</button><button class="btn btn-sm btn-warning" onclick="confirmDatabasePasswordRotation(${Number(databaseId)})">Rotate Password</button></div>
  </div>`, 560);
  const ctx = activeDatabasesLifecycle;
  const later = ctx && ctx.setTimeout ? ctx.setTimeout.bind(ctx) : setTimeout;
  later(() => { const input = $('db-rotate-confirm'); if (input) input.focus(); }, 0);
}

async function confirmDatabasePasswordRotation(databaseId) {
  const db = dbDashboardState.selectedDetail;
  const input = $('db-rotate-confirm');
  const msg = $('db-rotate-confirm-msg');
  if (!db || !input) return;
  const confirmation = input.value.trim();
  if (confirmation !== db.domain) { if (msg) msg.textContent = 'Confirmation must match ' + db.domain; return; }
  if (msg) msg.textContent = 'Queueing credential rotation...';
  try {
    const res = await apiPost('/api/wordpress/database-credentials/rotate', {site_id:Number(db.site_id), database_id:Number(databaseId), confirmation});
    const jobId = res.data && res.data.job_id;
    hideModal();
    toast('Credential rotation queued' + (jobId ? ' (job #' + jobId + ')' : ''), 'success');
    const action = $('db-rotation-action');
    if (action) action.innerHTML = `<div class="db-action-box"><strong>Rotation job ${jobId ? '#' + esc(String(jobId)) : ''}</strong><div id="db-rotation-msg" class="db-action-message">Waiting for job progress...</div></div>`;
    if (jobId) {
      pollRotationJob(jobId, {
        lifecycle: activeDatabasesLifecycle,
        messageEl: 'db-rotation-msg',
        renderRunning: renderDatabaseRotationJob,
        renderFailed: renderDatabaseRotationFailure,
        renderCompleted: renderDatabaseRotationSuccess,
        onCompleted: () => { toast('Database password rotated successfully', 'success'); refreshDatabases(); openDatabaseDetail(databaseId); },
        onFailed: () => { refreshDatabases(); }
      });
    }
  } catch(e) {
    const apiErr = e.body && e.body.error && e.body.error.message;
    if (msg) msg.textContent = apiErr || e.api_message || e.message || 'Failed to queue rotation';
  }
}

function renderDatabaseRotationJob(jobId, job) {
  return `<div class="db-job-box"><div><strong>Job #${esc(String(jobId))}</strong>: ${esc(job.message || job.status || 'pending')}</div>${renderRotationJobTimeline(job)}</div>`;
}

function renderDatabaseRotationSuccess(jobId, job) {
  return `<div class="db-job-box"><div class="badge badge-ok">Rotation completed</div>${renderRotationJobTimeline(job)}</div>`;
}

function renderDatabaseRotationFailure(jobId, job) {
  return renderWordPressRotationDiagnostics(jobId, job);
}

const databasesPage = { mount: loadDatabases, unmount() { destroyDatabaseDrawer(); activeDatabasesLifecycle = null; } };
export { loadDatabases, databasesPage };
Object.assign(window, { normalizeDbValue, dbConnectionState, dbCredentialState, dbRuntimeState, dbOwnershipState, computeDatabaseHealthState, dbHealthLabel, dbBadge, dbHealthBadge, dbRuntimeBadge, dbConnectionBadge, dbCredentialBadge, dbOwnershipBadge, dbJsArg, dbDateValue, dbSortRank, getFilteredDatabases, databaseSummaryCards, dbSelect, databaseControlsHtml, toggleDatabaseSortDirection, resetDatabaseFilters, loadDatabases, refreshDatabases, renderDatabaseInventory, renderDatabaseTable, renderDatabaseCards, openDatabaseDetail, showDatabaseDrawer, closeDatabaseDrawer, renderDatabaseDetail, databaseDetailSection, databaseHealthSection, copyableValue, loadDatabaseRotationStatus, updateDatabaseRotationAction, databaseRotationCapability, renderDatabaseRotationAction, showDatabaseRotationConfirm, confirmDatabasePasswordRotation, renderDatabaseRotationJob, renderDatabaseRotationSuccess, renderDatabaseRotationFailure });
