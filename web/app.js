async function api(path) {
  const res = await fetch(path);
  return res.json();
}

function escape(str) {
  if (!str) return '';
  return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

async function loadDashboard() {
  const [health, sites, users, domains, databases, ssl, proxy, access] = await Promise.all([
    api('/api/health'), api('/api/sites'), api('/api/users'),
    api('/api/domains'), api('/api/databases'), api('/api/ssl'),
    api('/api/proxy'), api('/api/access-users')
  ]);

  document.getElementById('status-badge').textContent = health.data?.status === 'ok' ? 'Connected' : 'Error';
  document.getElementById('status-badge').className = 'status ' + (health.data?.status === 'ok' ? 'ok' : 'error');

  const cards = [
    { label: 'Sites', count: sites.data?.length || 0, color: '#3b82f6' },
    { label: 'Users', count: users.data?.length || 0, color: '#10b981' },
    { label: 'Domains', count: domains.data?.length || 0, color: '#f59e0b' },
    { label: 'Databases', count: databases.data?.length || 0, color: '#8b5cf6' },
    { label: 'SSL', count: ssl.data?.length || 0, color: '#ec4899' },
    { label: 'Proxy', count: proxy.data?.length || 0, color: '#06b6d4' },
    { label: 'Access', count: access.data?.length || 0, color: '#f97316' }
  ];

  document.getElementById('cards').innerHTML = cards.map(c =>
    `<div class="card"><h3>${c.label}</h3><div class="count${c.count === 0 ? ' zero' : ''}">${c.count}</div></div>`
  ).join('');
  document.getElementById('table-container').innerHTML = '';
  document.getElementById('page-title').textContent = 'Dashboard';
}

function buildTable(columns, rows) {
  if (!rows || rows.length === 0) return '<div class="empty">No data</div>';
  let html = '<table><thead><tr>' + columns.map(c => '<th>' + c.label + '</th>').join('') + '</tr></thead><tbody>';
  for (const row of rows) {
    html += '<tr>' + columns.map(c => '<td>' + escape(c.value(row)) + '</td>').join('') + '</tr>';
  }
  html += '</tbody></table>';
  return html;
}

async function loadSites() {
  const data = await api('/api/sites');
  document.getElementById('page-title').textContent = 'Sites';
  document.getElementById('cards').innerHTML = '';
  document.getElementById('table-container').innerHTML = buildTable(
    [{label:'ID',value:r=>r.id},{label:'Domain',value:r=>r.domain},{label:'Owner',value:r=>r.owner}],
    data.data || []
  );
}

async function loadUsers() {
  const data = await api('/api/users');
  document.getElementById('page-title').textContent = 'Users';
  document.getElementById('cards').innerHTML = '';
  document.getElementById('table-container').innerHTML = buildTable(
    [{label:'ID',value:r=>r.id},{label:'Username',value:r=>r.username},{label:'UID',value:r=>r.uid},{label:'Enabled',value:r=>r.enabled?'yes':'no'}],
    data.data || []
  );
}

async function loadDomains() {
  const data = await api('/api/domains');
  document.getElementById('page-title').textContent = 'Domains';
  document.getElementById('cards').innerHTML = '';
  document.getElementById('table-container').innerHTML = buildTable(
    [{label:'ID',value:r=>r.id},{label:'Domain',value:r=>r.domain},{label:'Site ID',value:r=>r.site_id},{label:'PHP',value:r=>r.php_version},{label:'SSL',value:r=>r.ssl_enabled?'yes':'no'}],
    data.data || []
  );
}

async function loadDatabases() {
  const data = await api('/api/databases');
  document.getElementById('page-title').textContent = 'Databases';
  document.getElementById('cards').innerHTML = '';
  document.getElementById('table-container').innerHTML = buildTable(
    [{label:'ID',value:r=>r.id},{label:'Name',value:r=>r.name},{label:'Engine',value:r=>r.engine},{label:'Site ID',value:r=>r.site_id},{label:'Enabled',value:r=>r.enabled?'yes':'no'}],
    data.data || []
  );
}

async function loadSsl() {
  const data = await api('/api/ssl');
  document.getElementById('page-title').textContent = 'SSL Certificates';
  document.getElementById('cards').innerHTML = '';
  document.getElementById('table-container').innerHTML = buildTable(
    [{label:'ID',value:r=>r.id},{label:'Domain',value:r=>r.domain},{label:'Provider',value:r=>r.provider},{label:'Status',value:r=>r.status},{label:'Expires',value:r=>r.expires_at}],
    data.data || []
  );
}

async function loadProxy() {
  const data = await api('/api/proxy');
  document.getElementById('page-title').textContent = 'Proxy Configs';
  document.getElementById('cards').innerHTML = '';
  document.getElementById('table-container').innerHTML = buildTable(
    [{label:'ID',value:r=>r.id},{label:'Domain',value:r=>r.domain},{label:'Provider',value:r=>r.provider},{label:'Status',value:r=>r.status},{label:'Enabled',value:r=>r.enabled?'yes':'no'}],
    data.data || []
  );
}

async function loadAccess() {
  const data = await api('/api/access-users');
  document.getElementById('page-title').textContent = 'Access Users';
  document.getElementById('cards').innerHTML = '';
  document.getElementById('table-container').innerHTML = buildTable(
    [{label:'ID',value:r=>r.id},{label:'Username',value:r=>r.username},{label:'Enabled',value:r=>r.enabled?'yes':'no'}],
    data.data || []
  );
}

document.addEventListener('DOMContentLoaded', () => {
  document.querySelectorAll('.nav-link').forEach(link => {
    link.addEventListener('click', e => {
      e.preventDefault();
      document.querySelectorAll('.nav-link').forEach(l => l.classList.remove('active'));
      link.classList.add('active');
      const page = link.dataset.page;
      if (page === 'dashboard') loadDashboard();
      else if (page === 'sites') loadSites();
      else if (page === 'users') loadUsers();
      else if (page === 'domains') loadDomains();
      else if (page === 'databases') loadDatabases();
      else if (page === 'ssl') loadSsl();
      else if (page === 'proxy') loadProxy();
      else if (page === 'access') loadAccess();
    });
  });
  loadDashboard();
});
